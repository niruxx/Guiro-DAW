#include "engine/Runtime.h"
#include "engine/Snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace guiro {

void storePeak(std::atomic<float>& a, float v)
{
    if (v > a.load(std::memory_order_relaxed))
        a.store(v, std::memory_order_relaxed);
}

void ChannelRt::sortEvents()
{
    // Stable insertion sort by frame (event counts are small).
    for (int i = 1; i < numEvents; ++i) {
        ChEvent e = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].frame > e.frame) {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = e;
    }
}

bool ChannelRt::hasActiveVoices() const
{
    for (const auto& v : voices)
        if (v.stage != Voice::Off)
            return true;
    return false;
}

namespace {

inline float hermite(const float* d, int64_t frames, int64_t i, float f)
{
    auto at = [&](int64_t k) { return (k >= 0 && k < frames) ? d[k] : 0.f; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void renderVoices(ChannelRt& rt, int from, int to)
{
    if (to <= from)
        return;
    for (auto& v : rt.voices) {
        if (v.stage == Voice::Off || !v.sample)
            continue;
        const Sample& s = *v.sample;
        const float* L = s.left.data();
        const float* R = s.right.data();
        for (int i = from; i < to; ++i) {
            const int64_t idx = int64_t(v.pos);
            if (idx >= s.frames) {
                v.stage = Voice::Off;
                break;
            }
            switch (v.stage) {
            case Voice::Attack:
                v.env += v.attackStep;
                if (v.env >= 1.f) {
                    v.env = 1.f;
                    v.stage = Voice::Sustain;
                }
                break;
            case Voice::Release:
                v.env *= v.releaseCoef;
                if (v.env < 0.0003f) {
                    v.stage = Voice::Off;
                }
                break;
            default:
                break;
            }
            if (v.stage == Voice::Off)
                break;
            const float frac = float(v.pos - double(idx));
            const float g = v.gain * v.env;
            rt.buf[0][i] += hermite(L, s.frames, idx, frac) * g;
            rt.buf[1][i] += hermite(R, s.frames, idx, frac) * g;
            v.pos += v.inc;
        }
    }
}

void releaseVoice(Voice& v, float coef)
{
    if (v.stage == Voice::Attack || v.stage == Voice::Sustain) {
        v.stage = Voice::Release;
        v.releaseCoef = coef;
    }
}

float releaseCoefFor(float ms, float sr)
{
    const float samples = std::max(1.f, ms * 0.001f * sr);
    return std::exp(std::log(0.0003f) / samples);
}

void startVoice(ChannelRt& rt, const SnapChannel& ch, const ChEvent& e, float sr)
{
    const Sample* sample = nullptr;
    float gain = e.vel;
    float semis = ch.pitch ? ch.pitch->value() : 0.f;

    if (ch.kind == ChannelKind::Pads) {
        const int pad = int(e.key) - kPadBaseKey;
        if (pad < 0 || pad >= kNumPads)
            return;
        sample = ch.pads[size_t(pad)];
        if (ch.padGain[size_t(pad)])
            gain *= ch.padGain[size_t(pad)]->value();
        if (ch.padPitch[size_t(pad)])
            semis += ch.padPitch[size_t(pad)]->value();
    } else {
        sample = ch.sample;
        semis += float(int(e.key) - kRootKey);
    }
    if (!sample || sample->frames == 0)
        return;

    // Free voice, else steal the oldest.
    Voice* target = nullptr;
    for (auto& v : rt.voices) {
        if (v.stage == Voice::Off) {
            target = &v;
            break;
        }
    }
    if (!target) {
        target = &rt.voices[0];
        for (auto& v : rt.voices)
            if (v.age < target->age)
                target = &v;
    }

    const float attackMs = ch.attack ? ch.attack->value() : 0.f;
    const float releaseMs = ch.release ? ch.release->value() : 40.f;
    Voice& v = *target;
    v.sample = sample;
    v.pos = 0.0;
    v.inc = std::pow(2.0, double(semis) / 12.0) * sample->sampleRate / double(sr);
    v.gain = gain;
    v.env = 0.f;
    v.attackStep = 1.f / std::max(1.f, attackMs * 0.001f * sr + 32.f);  // >= ~0.7ms to avoid clicks
    v.releaseCoef = releaseCoefFor(releaseMs, sr);
    v.stage = Voice::Attack;
    v.key = e.key;
    v.live = e.live;
    v.endTick = e.endTick;
    v.age = ++rt.ageCounter;
}

void applySamplerEvent(ChannelRt& rt, const SnapChannel& ch, const ChEvent& e, float sr)
{
    const float relMs = ch.release ? ch.release->value() : 40.f;
    const float rel = releaseCoefFor(relMs, sr);
    switch (e.type) {
    case EvType::NoteOn:
        startVoice(rt, ch, e, sr);
        break;
    case EvType::NoteOff:
        for (auto& v : rt.voices)
            if (v.live && v.key == e.key)
                releaseVoice(v, rel);
        break;
    case EvType::ReleaseSequenced:
        for (auto& v : rt.voices)
            if (!v.live && v.endTick != kNever)
                releaseVoice(v, rel);
        break;
    case EvType::ReleaseEnded:
        for (auto& v : rt.voices)
            if (!v.live && v.endTick <= e.endTick)
                releaseVoice(v, rel);
        break;
    case EvType::AllOff: {
        const float fast = releaseCoefFor(6.f, sr);
        for (auto& v : rt.voices)
            if (v.stage != Voice::Off) {
                v.stage = Voice::Release;
                v.releaseCoef = std::min(v.releaseCoef, fast);
            }
        break;
    }
    }
}

} // namespace

void renderSamplerChannel(ChannelRt& rt, const SnapChannel& ch, int n, float sr)
{
    rt.sortEvents();
    int pos = 0;
    for (int i = 0; i < rt.numEvents; ++i) {
        const ChEvent& e = rt.events[i];
        const int f = std::min<int>(int(e.frame), n);
        renderVoices(rt, pos, f);
        pos = std::max(pos, f);
        applySamplerEvent(rt, ch, e, sr);
    }
    renderVoices(rt, pos, n);
    rt.numEvents = 0;
}

void renderInstrumentChannel(ChannelRt& rt, const SnapChannel& ch, int n, const TransportInfo& t)
{
    rt.sortEvents();
    int& np = rt.numPluginNotes;
    np = 0;
    auto off = [&](uint32_t frame, int key) {
        if (np < ChannelRt::kMaxEvents)
            rt.pluginNotes[np++] = {frame, false, uint8_t(key), 0.f};
        rt.held[key] = false;
        rt.heldLive[key] = false;
    };
    for (int i = 0; i < rt.numEvents; ++i) {
        const ChEvent& e = rt.events[i];
        const uint32_t f = std::min<uint32_t>(e.frame, uint32_t(n - 1));
        switch (e.type) {
        case EvType::NoteOn:
            if (rt.held[e.key])
                off(f, e.key);
            if (np < ChannelRt::kMaxEvents)
                rt.pluginNotes[np++] = {f, true, e.key, e.vel};
            rt.held[e.key] = true;
            rt.heldLive[e.key] = e.live;
            rt.heldEnd[e.key] = e.endTick;
            break;
        case EvType::NoteOff:
            if (rt.held[e.key] && rt.heldLive[e.key])
                off(f, e.key);
            break;
        case EvType::ReleaseSequenced:
            for (int k = 0; k < 128; ++k)
                if (rt.held[k] && !rt.heldLive[k])
                    off(f, k);
            break;
        case EvType::ReleaseEnded:
            if (rt.held[e.key] && !rt.heldLive[e.key] && rt.heldEnd[e.key] <= e.endTick)
                off(f, e.key);
            break;
        case EvType::AllOff:
            for (int k = 0; k < 128; ++k)
                if (rt.held[k])
                    off(f, k);
            break;
        }
    }
    rt.numEvents = 0;

    float* out[2] = {rt.buf[0], rt.buf[1]};
    if (ch.plugin)
        ch.plugin->process(nullptr, out, n, rt.pluginNotes, np, t);
}

} // namespace guiro
