#include "engine/AudioEngine.h"
#include "engine/Backends.h"
#include "engine/Dsp.h"

#include <QThread>
#include <QElapsedTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace guiro {

AudioEngine* AudioEngine::s_instance = nullptr;

AudioEngine::AudioEngine(Project* project, QObject* parent) : QObject(parent), m_project(project)
{
    s_instance = this;
    connect(project, &Project::snapshotNeeded, this, &AudioEngine::rebuildSnapshot);
    m_gcTimer.setInterval(100);
    connect(&m_gcTimer, &QTimer::timeout, this, &AudioEngine::collectGarbage);
    m_gcTimer.start();
}

AudioEngine::~AudioEngine()
{
    shutdown();
    if (Snapshot* s = m_snap.exchange(nullptr))
        delete s;
    for (auto& r : m_retired)
        delete r.snap;
    m_retired.clear();
    if (s_instance == this)
        s_instance = nullptr;
}

bool AudioEngine::start()
{
    QString err;
    if (!qEnvironmentVariableIsSet("GUIRO_NO_JACK")) {
        auto jack = std::make_unique<JackBackend>();
        if (jack->open(this, &err)) {
            m_backend = std::move(jack);
        } else {
            m_backendError = err;
        }
    }
    if (!m_backend) {
        auto dummy = std::make_unique<DummyBackend>();
        dummy->open(this, &err);
        m_backend = std::move(dummy);
    }
    m_sampleRate = m_backend->sampleRate();
    SampleCache::instance().setTargetRate(m_sampleRate);
    return dynamic_cast<JackBackend*>(m_backend.get()) != nullptr;
}

void AudioEngine::shutdown()
{
    if (m_backend) {
        m_backend->close();
        m_backend.reset();
    }
}

QString AudioEngine::backendName() const
{
    return m_backend ? m_backend->name() : QStringLiteral("None");
}

int AudioEngine::bufferSize() const
{
    return m_backend ? m_backend->bufferSize() : 0;
}

// ---------------------------------------------------------------- snapshots

void AudioEngine::rebuildSnapshot()
{
    Snapshot* fresh = buildSnapshot(*m_project);
    Snapshot* old = m_snap.exchange(fresh, std::memory_order_acq_rel);
    if (old)
        m_retired.push_back({old, m_cycles.load(std::memory_order_acquire)});
    collectGarbage();
}

void AudioEngine::collectGarbage()
{
    const uint64_t now = m_cycles.load(std::memory_order_acquire);
    const bool running = m_backend && m_backend->isRunning();
    auto it = std::remove_if(m_retired.begin(), m_retired.end(), [&](const Retired& r) {
        if (!running || now >= r.cycle + 2) {
            delete r.snap;
            return true;
        }
        return false;
    });
    m_retired.erase(it, m_retired.end());
}

// ---------------------------------------------------------------- GUI-side transport

void AudioEngine::send(const Cmd& c)
{
    m_cmds.push(c);
}

void AudioEngine::play()
{
    m_guiPlaying = true;
    send({Cmd::Play});
    emit transportChanged();
}

void AudioEngine::stop()
{
    m_guiPlaying = false;
    send({Cmd::Stop});
    m_posMirror.store(0.0);
    emit transportChanged();
}

void AudioEngine::togglePlay()
{
    if (m_guiPlaying)
        stop();
    else
        play();
}

void AudioEngine::setSongMode(bool song)
{
    m_guiSongMode = song;
    Cmd c{Cmd::SongMode};
    c.flag = song;
    send(c);
    m_posMirror.store(0.0);
    emit transportChanged();
}

void AudioEngine::setPosition(double tick)
{
    Cmd c{Cmd::Seek};
    c.pos = std::max(0.0, tick);
    send(c);
    m_posMirror.store(c.pos);
}

void AudioEngine::setMetronome(bool on)
{
    m_guiMetronome = on;
    Cmd c{Cmd::Metronome};
    c.flag = on;
    send(c);
    emit transportChanged();
}

void AudioEngine::noteOn(ObjId channel, int key, float velocity)
{
    Cmd c{Cmd::NoteOn};
    c.channel = channel;
    c.key = key;
    c.vel = velocity;
    send(c);
}

void AudioEngine::noteOff(ObjId channel, int key)
{
    Cmd c{Cmd::NoteOff};
    c.channel = channel;
    c.key = key;
    send(c);
}

void AudioEngine::previewSample(const SamplePtr& s)
{
    Cmd c{Cmd::Preview};
    c.sample = s.get();   // kept alive by the sample cache
    send(c);
}

void AudioEngine::stopPreview()
{
    send({Cmd::StopPreview});
}

void AudioEngine::panic()
{
    send({Cmd::Panic});
}

void AudioEngine::readScope(float* dst, int n) const
{
    const uint32_t w = m_scopeWrite.load(std::memory_order_acquire);
    n = std::min(n, kScopeSize);
    for (int i = 0; i < n; ++i)
        dst[i] = m_scope[(w - uint32_t(n) + uint32_t(i)) & (kScopeSize - 1)];
}

// ---------------------------------------------------------------- audio thread

void AudioEngine::broadcast(Snapshot* s, EvType type, uint32_t frame)
{
    if (!s)
        return;
    for (auto& ch : s->channels) {
        ChEvent e;
        e.frame = frame;
        e.type = type;
        ch.rt->push(e);
    }
}

void AudioEngine::drainCommands(Snapshot* s)
{
    Cmd c;
    while (m_cmds.pop(c)) {
        switch (c.type) {
        case Cmd::Play:
            m_playing = true;
            break;
        case Cmd::Stop:
            m_playing = false;
            m_pos = 0.0;
            m_samplePos = 0;
            broadcast(s, EvType::AllOff, 0);
            break;
        case Cmd::Seek:
            m_pos = c.pos;
            m_samplePos = int64_t(c.pos * m_sampleRate * 60.0 / (m_project->bpm() * kPPQ));
            broadcast(s, EvType::ReleaseSequenced, 0);
            break;
        case Cmd::SongMode:
            m_songMode = c.flag;
            m_pos = 0.0;
            broadcast(s, EvType::ReleaseSequenced, 0);
            break;
        case Cmd::Metronome:
            m_metronomeOn = c.flag;
            break;
        case Cmd::NoteOn:
        case Cmd::NoteOff: {
            if (!s)
                break;
            const int idx = s->findChannel(c.channel);
            if (idx < 0)
                break;
            ChEvent e;
            e.frame = 0;
            e.type = c.type == Cmd::NoteOn ? EvType::NoteOn : EvType::NoteOff;
            e.key = uint8_t(std::clamp(c.key, 0, 127));
            e.vel = c.vel;
            e.live = true;
            e.endTick = kNever;
            s->channels[size_t(idx)].rt->push(e);
            break;
        }
        case Cmd::Preview:
            m_preview = c.sample;
            m_previewPos = 0.0;
            break;
        case Cmd::StopPreview:
            m_preview = nullptr;
            break;
        case Cmd::Panic:
            broadcast(s, EvType::AllOff, 0);
            m_preview = nullptr;
            break;
        }
    }
}

void AudioEngine::process(float* outL, float* outR, int frames, const MidiMsg* midi, int numMidi)
{
    setAudioThread(true);
    if (m_offline.load(std::memory_order_acquire)) {
        std::memset(outL, 0, sizeof(float) * size_t(frames));
        std::memset(outR, 0, sizeof(float) * size_t(frames));
        m_cycles.fetch_add(1, std::memory_order_release);
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    Snapshot* s = m_snap.load(std::memory_order_acquire);
    drainCommands(s);

    int done = 0;
    while (done < frames) {
        const int n = std::min(kMaxBlock, frames - done);
        renderBlock(s, outL + done, outR + done, n, midi, numMidi, done);
        done += n;
    }
    m_posMirror.store(m_pos, std::memory_order_relaxed);
    m_cycles.fetch_add(1, std::memory_order_release);

    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double avail = double(frames) / m_sampleRate;
    const float load = float(used / std::max(avail, 1e-6));
    const float prev = m_load.load(std::memory_order_relaxed);
    m_load.store(prev + (load - prev) * 0.1f, std::memory_order_relaxed);
}

void AudioEngine::loopRange(Snapshot* s, double& start, double& end) const
{
    if (m_exporting) {
        start = 0.0;
        end = double(kNever);
        return;
    }
    if (m_songMode) {
        if (s->loopEnd > s->loopStart) {
            start = double(s->loopStart);
            end = double(s->loopEnd);
        } else {
            start = 0.0;
            end = double(s->songEnd);
        }
    } else {
        start = 0.0;
        end = s->currentPattern >= 0 ? double(s->patterns[size_t(s->currentPattern)].length) : double(kBarTicks);
    }
}

void AudioEngine::renderBlock(Snapshot* s, float* outL, float* outR, int n, const MidiMsg* midi, int numMidi, int midiBase)
{
    if (!s) {
        std::memset(outL, 0, sizeof(float) * size_t(n));
        std::memset(outR, 0, sizeof(float) * size_t(n));
        return;
    }
    const float sr = float(m_sampleRate);

    for (auto& ins : s->inserts) {
        std::memset(ins.rt->buf[0], 0, sizeof(float) * size_t(n));
        std::memset(ins.rt->buf[1], 0, sizeof(float) * size_t(n));
    }
    for (auto& ch : s->channels) {
        std::memset(ch.rt->buf[0], 0, sizeof(float) * size_t(n));
        std::memset(ch.rt->buf[1], 0, sizeof(float) * size_t(n));
    }

    // ---- live MIDI input to the selected channel
    const ObjId midiTarget = m_midiTarget.load(std::memory_order_relaxed);
    const int midiChan = midiTarget ? s->findChannel(midiTarget) : -1;
    for (int i = 0; i < numMidi; ++i) {
        const MidiMsg& m = midi[i];
        if (int(m.frame) < midiBase || int(m.frame) >= midiBase + n || midiChan < 0 || m.size < 3)
            continue;
        const uint8_t st = m.data[0] & 0xF0;
        const bool on = st == 0x90 && m.data[2] > 0;
        const bool off = st == 0x80 || (st == 0x90 && m.data[2] == 0);
        if (!on && !off)
            continue;
        ChEvent e;
        e.frame = uint32_t(int(m.frame) - midiBase);
        e.type = on ? EvType::NoteOn : EvType::NoteOff;
        e.key = m.data[1] & 0x7F;
        e.vel = float(m.data[2]) / 127.f;
        e.live = true;
        s->channels[size_t(midiChan)].rt->push(e);
        if (m_playing && m_recording) {
            if (on) {
                m_recStart[e.key] = m_pos;
                m_recorded.push({midiTarget, e.key, e.vel, m_pos, -1.0});
            } else {
                m_recorded.push({midiTarget, e.key, 0.f, m_recStart[e.key], m_pos});
            }
        }
    }

    // ---- sequencer
    TransportInfo ti;
    const double bpm = m_project->bpm();
    ti.bpm = bpm;
    if (m_playing) {
        const double fpt = double(sr) * 60.0 / (bpm * kPPQ);
        const double ticks = double(n) / fpt;
        double loopS, loopE;
        loopRange(s, loopS, loopE);
        if (m_pos >= loopE || m_pos < 0.0)
            m_pos = loopS;
        double t0 = m_pos;
        double t1 = t0 + ticks;
        ti.playing = true;
        ti.beatPos = t0 / kPPQ;
        ti.samplePos = m_samplePos;

        applyAutomation(s, t0);

        auto clickAt = [&](double a, double b, int frameBase) {
            if (!m_metronomeOn)
                return;
            const double first = std::ceil(a / kBeatTicks) * kBeatTicks;
            for (double beat = first; beat < b; beat += kBeatTicks) {
                m_clickDelay = std::max(0, frameBase + int((beat - a) * fpt));
                const bool accent = std::fmod(beat, double(kBarTicks)) < 0.5;
                m_clickFreq = accent ? 2100.0 : 1400.0;
                m_clickRemain = int(sr * 0.035f);
                m_clickPhase = 0.0;
            }
        };

        if (t1 >= loopE) {
            const int f1 = std::clamp(int((loopE - t0) * fpt), 0, n - 1);
            scheduleRange(s, t0, loopE, 0, n, fpt);
            clickAt(t0, loopE, 0);
            broadcast(s, EvType::ReleaseSequenced, uint32_t(f1));
            const double rem = t1 - loopE;
            scheduleRange(s, loopS, loopS + rem, f1, n, fpt);
            clickAt(loopS, loopS + rem, f1);
            m_pos = loopS + rem;
        } else {
            scheduleRange(s, t0, t1, 0, n, fpt);
            clickAt(t0, t1, 0);
            m_pos = t1;
        }
        m_samplePos += n;
    }

    // ---- channels
    for (auto& ch : s->channels) {
        ChannelRt& rt = *ch.rt;
        switch (ch.kind) {
        case ChannelKind::Sampler:
        case ChannelKind::Pads:
        case ChannelKind::Audio:
            renderSamplerChannel(rt, ch, n, sr);
            break;
        case ChannelKind::Instrument:
            renderInstrumentChannel(rt, ch, n, ti);
            break;
        }

        const auto ft = FilterType(int(ch.filterType->value()));
        const float cut = ch.cutoff->value();
        rt.filter.process(rt.buf[0], rt.buf[1], n, ft, rt.lastCutoff, cut, ch.resonance->value(), sr);
        rt.lastCutoff = cut;

        float gl, gr;
        dsp::panGains(ch.pan->value(), gl, gr);
        const float vol = ch.muted ? 0.f : ch.volume->value();
        gl *= vol;
        gr *= vol;
        if (!rt.gainInit) {
            rt.gainL = gl;
            rt.gainR = gr;
            rt.gainInit = true;
        }
        dsp::applyGainRamp(rt.buf[0], rt.buf[1], n, rt.gainL, rt.gainR, gl, gr);
        rt.gainL = gl;
        rt.gainR = gr;

        const float pk = std::max(dsp::peakOf(rt.buf[0], n), dsp::peakOf(rt.buf[1], n));
        storePeak(rt.activity, pk);

        InsertRt& dst = *s->inserts[size_t(ch.insert)].rt;
        for (int i = 0; i < n; ++i) {
            dst.buf[0][i] += rt.buf[0][i];
            dst.buf[1][i] += rt.buf[1][i];
        }
    }

    // ---- mixer inserts, then master
    auto processInsert = [&](SnapInsert& ins) {
        InsertRt& rt = *ins.rt;
        for (PluginInstance* fx : ins.fx) {
            if (!fx)
                continue;
            std::memcpy(m_fxIn[0], rt.buf[0], sizeof(float) * size_t(n));
            std::memcpy(m_fxIn[1], rt.buf[1], sizeof(float) * size_t(n));
            std::memset(m_fxOut[0], 0, sizeof(float) * size_t(n));
            std::memset(m_fxOut[1], 0, sizeof(float) * size_t(n));
            const float* in[2] = {m_fxIn[0], m_fxIn[1]};
            float* out[2] = {m_fxOut[0], m_fxOut[1]};
            fx->process(in, out, n, nullptr, 0, ti);
            std::memcpy(rt.buf[0], m_fxOut[0], sizeof(float) * size_t(n));
            std::memcpy(rt.buf[1], m_fxOut[1], sizeof(float) * size_t(n));
        }
        const auto ft = FilterType(int(ins.filterType->value()));
        const float cut = ins.cutoff->value();
        rt.filter.process(rt.buf[0], rt.buf[1], n, ft, rt.lastCutoff, cut, ins.resonance->value(), sr);
        rt.lastCutoff = cut;

        float gl, gr;
        dsp::panGains(ins.pan->value(), gl, gr);
        const bool silenced = ins.muted || (ins.index != 0 && s->anySolo && !ins.solo);
        const float vol = silenced ? 0.f : ins.volume->value();
        gl *= vol;
        gr *= vol;
        if (!rt.gainInit) {
            rt.gainL = gl;
            rt.gainR = gr;
            rt.gainInit = true;
        }
        dsp::applyGainRamp(rt.buf[0], rt.buf[1], n, rt.gainL, rt.gainR, gl, gr);
        rt.gainL = gl;
        rt.gainR = gr;
    };

    InsertRt& master = *s->inserts[0].rt;
    for (size_t i = 1; i < s->inserts.size(); ++i) {
        SnapInsert& ins = s->inserts[i];
        processInsert(ins);
        storePeak(ins.rt->peakL, dsp::peakOf(ins.rt->buf[0], n));
        storePeak(ins.rt->peakR, dsp::peakOf(ins.rt->buf[1], n));
        for (int k = 0; k < n; ++k) {
            master.buf[0][k] += ins.rt->buf[0][k];
            master.buf[1][k] += ins.rt->buf[1][k];
        }
    }
    processInsert(s->inserts[0]);

    // ---- metronome & preview (post master fader, so they are always audible)
    if (m_clickRemain > 0) {
        const int total = int(sr * 0.035f);
        for (int i = 0; i < n && m_clickRemain > 0; ++i) {
            if (m_clickDelay > 0) {
                --m_clickDelay;
                continue;
            }
            --m_clickRemain;
            const float env = float(m_clickRemain) / float(total);
            const float v = float(std::sin(m_clickPhase)) * env * env * 0.35f;
            m_clickPhase += 2.0 * 3.14159265 * m_clickFreq / sr;
            master.buf[0][i] += v;
            master.buf[1][i] += v;
        }
    }
    if (m_preview) {
        const Sample& p = *m_preview;
        const double inc = p.sampleRate / double(sr);
        for (int i = 0; i < n; ++i) {
            const int64_t idx = int64_t(m_previewPos);
            if (idx >= p.frames) {
                m_preview = nullptr;
                break;
            }
            master.buf[0][i] += p.left[size_t(idx)] * 0.8f;
            master.buf[1][i] += p.right[size_t(idx)] * 0.8f;
            m_previewPos += inc;
        }
    }

    storePeak(master.peakL, dsp::peakOf(master.buf[0], n));
    storePeak(master.peakR, dsp::peakOf(master.buf[1], n));

    uint32_t w = m_scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        float l = master.buf[0][i];
        float r = master.buf[1][i];
        if (!std::isfinite(l))
            l = 0.f;
        if (!std::isfinite(r))
            r = 0.f;
        outL[i] = std::clamp(l, -4.f, 4.f);
        outR[i] = std::clamp(r, -4.f, 4.f);
        m_scope[w & (kScopeSize - 1)] = 0.5f * (l + r);
        ++w;
    }
    m_scopeWrite.store(w, std::memory_order_release);
}

void AudioEngine::applyAutomation(Snapshot* s, double t)
{
    if (!m_songMode && !m_exporting)
        return;
    for (const SnapClip& c : s->clips) {
        if (c.start > t)
            break;
        if (c.kind != ClipKind::Automation || t >= double(c.end))
            continue;
        const SnapAutomation& a = s->automations[size_t(c.index)];
        const float v = a.valueAt(Tick(t) - c.start + c.offset);
        if (a.target.param)
            a.target.param->setNormalized(v);
        else if (a.target.plugin)
            a.target.plugin->automate(a.target.pluginParam,
                                      a.target.pluginMin + (a.target.pluginMax - a.target.pluginMin) * double(v));
    }
}

void AudioEngine::scheduleRange(Snapshot* s, double tA, double tB, int frameBase, int n, double fpt)
{
    if (tB <= tA)
        return;
    if (m_songMode || m_exporting) {
        for (const SnapClip& c : s->clips) {
            if (double(c.start) >= tB)
                break;
            if (double(c.end) <= tA)
                continue;
            if (c.kind == ClipKind::Pattern)
                schedulePattern(s, s->patterns[size_t(c.index)], double(c.start), double(c.end), double(c.offset), tA,
                                tB, frameBase, n, fpt);
            else if (c.kind == ClipKind::Audio)
                renderAudioClip(s, c, tA, tB, frameBase, n, fpt);
        }
    } else if (s->currentPattern >= 0) {
        const SnapPattern& p = s->patterns[size_t(s->currentPattern)];
        schedulePattern(s, p, 0.0, double(p.length), 0.0, tA, tB, frameBase, n, fpt);
    }
    scheduleEnds(s, tA, tB, frameBase, n, fpt);
}

void AudioEngine::schedulePattern(Snapshot* s, const SnapPattern& p, double clipStart, double clipEnd, double offset,
                                  double tA, double tB, int frameBase, int n, double fpt)
{
    const double L = double(std::max<Tick>(1, p.length));
    const double la = tA - clipStart + offset;
    const double lb = std::min(tB, clipEnd) - clipStart + offset;
    if (lb <= la)
        return;
    const int64_t kFirst = int64_t(std::floor(la / L));
    const int64_t kLast = int64_t(std::floor((lb - 1e-9) / L));
    for (int64_t k = kFirst; k <= kLast; ++k) {
        const double base = double(k) * L;
        for (const auto& pc : p.channels) {
            SnapChannel& ch = s->channels[size_t(pc.channel)];
            for (const SnapNote& note : pc.notes) {
                const double ns = base + double(note.start);
                if (ns < la)
                    continue;
                if (ns >= lb)
                    break;
                const double absStart = clipStart + ns - offset;
                ChEvent e;
                e.frame = uint32_t(std::clamp(frameBase + int((absStart - tA) * fpt), 0, n - 1));
                e.type = EvType::NoteOn;
                e.key = note.key;
                e.vel = note.vel;
                e.live = false;
                if (note.end == kNever) {
                    e.endTick = kNever;
                } else {
                    const double absEnd = std::min(clipStart + base + double(note.end) - offset, clipEnd);
                    e.endTick = Tick(absEnd);
                }
                ch.rt->push(e);
            }
        }
    }
}

void AudioEngine::scheduleEnds(Snapshot* s, double tA, double tB, int frameBase, int n, double fpt)
{
    for (auto& ch : s->channels) {
        ChannelRt& rt = *ch.rt;
        auto frameOf = [&](Tick end) {
            return uint32_t(std::clamp(frameBase + int((double(end) - tA) * fpt), 0, n - 1));
        };
        if (ch.kind == ChannelKind::Instrument) {
            for (int k = 0; k < 128; ++k) {
                if (rt.held[k] && !rt.heldLive[k] && rt.heldEnd[k] != kNever && double(rt.heldEnd[k]) < tB) {
                    ChEvent e;
                    e.type = EvType::ReleaseEnded;
                    e.key = uint8_t(k);
                    e.endTick = rt.heldEnd[k];
                    e.frame = frameOf(rt.heldEnd[k]);
                    rt.push(e);
                }
            }
        } else {
            for (const Voice& v : rt.voices) {
                if ((v.stage == Voice::Attack || v.stage == Voice::Sustain) && !v.live && v.endTick != kNever &&
                    double(v.endTick) < tB) {
                    ChEvent e;
                    e.type = EvType::ReleaseEnded;
                    e.endTick = v.endTick;
                    e.frame = frameOf(v.endTick);
                    rt.push(e);
                }
            }
        }
    }
}

void AudioEngine::renderAudioClip(Snapshot* s, const SnapClip& c, double tA, double tB, int frameBase, int n,
                                  double fpt)
{
    SnapChannel& ch = s->channels[size_t(c.index)];
    const Sample* smp = ch.sample;
    if (!smp || smp->frames == 0)
        return;
    const double segStart = std::max(tA, double(c.start));
    const double segEnd = std::min(tB, double(c.end));
    if (segEnd <= segStart)
        return;
    const int f0 = std::clamp(frameBase + int((segStart - tA) * fpt), 0, n);
    const int f1 = std::clamp(frameBase + int(std::ceil((segEnd - tA) * fpt)), 0, n);
    const double ratio = smp->sampleRate / m_sampleRate;
    double src = (segStart - double(c.start) + double(c.offset)) * fpt * ratio;
    float* L = ch.rt->buf[0];
    float* R = ch.rt->buf[1];
    for (int f = f0; f < f1; ++f) {
        const int64_t idx = int64_t(src);
        if (idx < 0) {
            src += ratio;
            continue;
        }
        if (idx + 1 >= smp->frames)
            break;
        const float fr = float(src - double(idx));
        L[f] += smp->left[size_t(idx)] + (smp->left[size_t(idx + 1)] - smp->left[size_t(idx)]) * fr;
        R[f] += smp->right[size_t(idx)] + (smp->right[size_t(idx + 1)] - smp->right[size_t(idx)]) * fr;
        src += ratio;
    }
}

// ---------------------------------------------------------------- offline

bool AudioEngine::beginOffline()
{
    const uint64_t start = m_cycles.load();
    m_offline.store(true, std::memory_order_release);
    // Wait until the realtime thread has certainly left the render path.
    QElapsedTimer t;
    t.start();
    while (m_backend && m_backend->isRunning() && m_cycles.load() < start + 2 && t.elapsed() < 2000)
        QThread::msleep(2);

    Snapshot* s = m_snap.load();
    drainCommands(s);
    broadcast(s, EvType::AllOff, 0);
    m_exporting = true;
    m_playing = true;
    m_pos = 0.0;
    m_samplePos = 0;
    m_preview = nullptr;
    m_clickRemain = 0;
    return true;
}

Tick AudioEngine::offlineSongEnd() const
{
    Snapshot* s = m_snap.load();
    return s ? s->songEnd : kBarTicks;
}

void AudioEngine::offlineProcess(float* outL, float* outR, int frames)
{
    setAudioThread(true);
    Snapshot* s = m_snap.load(std::memory_order_acquire);
    const bool metro = m_metronomeOn;
    m_metronomeOn = false;
    int done = 0;
    while (done < frames) {
        const int n = std::min(kMaxBlock, frames - done);
        renderBlock(s, outL + done, outR + done, n, nullptr, 0, 0);
        done += n;
    }
    m_metronomeOn = metro;
    m_cycles.fetch_add(1, std::memory_order_release);
}

void AudioEngine::endOffline()
{
    Snapshot* s = m_snap.load();
    broadcast(s, EvType::AllOff, 0);
    m_exporting = false;
    m_playing = false;
    m_pos = 0.0;
    m_guiPlaying = false;
    m_posMirror.store(0.0);
    m_offline.store(false, std::memory_order_release);
    emit transportChanged();
}

} // namespace guiro
