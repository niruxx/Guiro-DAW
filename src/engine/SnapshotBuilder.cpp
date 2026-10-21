#include "engine/Snapshot.h"

#include <algorithm>
#include <unordered_map>

namespace guiro {

float SnapAutomation::valueAt(Tick t) const
{
    if (points.empty())
        return 0.5f;
    if (t <= points.front().t)
        return points.front().v;
    if (t >= points.back().t)
        return points.back().v;
    // Binary search for the segment.
    size_t lo = 0, hi = points.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (points[mid].t <= t)
            lo = mid;
        else
            hi = mid;
    }
    const auto& a = points[lo];
    const auto& b = points[hi];
    if (b.t == a.t)
        return b.v;
    return a.v + (b.v - a.v) * float(t - a.t) / float(b.t - a.t);
}

Snapshot* buildSnapshot(const Project& p)
{
    auto* s = new Snapshot;
    auto keep = [s](std::shared_ptr<void> ptr) {
        if (ptr)
            s->keepAlive.push_back(std::move(ptr));
    };
    auto raw = [&](const ParamPtr& pp) -> Param* {
        keep(pp);
        return pp.get();
    };

    // ---- channels
    std::unordered_map<ObjId, int> chanIndex;
    s->channels.reserve(p.channels.size());
    for (const auto& c : p.channels) {
        SnapChannel sc;
        sc.id = c->id;
        sc.kind = c->kind;
        sc.rt = c->rt.get();
        keep(c->rt);
        sc.sample = c->sample.get();
        keep(std::const_pointer_cast<Sample>(c->sample));
        for (int i = 0; i < kNumPads; ++i) {
            const auto& pad = c->pads[size_t(i)];
            sc.pads[size_t(i)] = pad.sample.get();
            keep(std::const_pointer_cast<Sample>(pad.sample));
            sc.padGain[size_t(i)] = raw(pad.gain);
            sc.padPitch[size_t(i)] = raw(pad.pitch);
        }
        sc.plugin = c->plugin.get();
        keep(c->plugin);
        sc.volume = raw(c->volume);
        sc.pan = raw(c->pan);
        sc.pitch = raw(c->pitch);
        sc.filterType = raw(c->filterType);
        sc.cutoff = raw(c->cutoff);
        sc.resonance = raw(c->resonance);
        sc.attack = raw(c->attack);
        sc.release = raw(c->release);
        sc.insert = std::clamp(c->insert, 0, int(p.inserts.size()) - 1);
        sc.muted = c->muted;
        chanIndex[c->id] = int(s->channels.size());
        s->channels.push_back(sc);
    }

    // ---- mixer
    for (const auto& m : p.inserts) {
        SnapInsert si;
        si.index = m->index;
        si.rt = m->rt.get();
        keep(m->rt);
        si.volume = raw(m->volume);
        si.pan = raw(m->pan);
        si.filterType = raw(m->filterType);
        si.cutoff = raw(m->cutoff);
        si.resonance = raw(m->resonance);
        si.muted = m->muted;
        si.solo = m->solo && m->index != 0;
        s->anySolo = s->anySolo || si.solo;
        for (int k = 0; k < kNumFxSlots; ++k) {
            const auto& fx = m->fx[size_t(k)];
            if (fx.plugin && !fx.bypass) {
                si.fx[size_t(k)] = fx.plugin.get();
                keep(fx.plugin);
            }
        }
        s->inserts.push_back(si);
    }

    // ---- patterns
    const float swing = p.swing ? p.swing->value() : 0.f;
    s->swing = swing;
    std::unordered_map<ObjId, int> patIndex;
    for (const auto& pat : p.patterns) {
        SnapPattern sp;
        sp.id = pat->id;
        sp.length = pat->length();
        for (const auto& c : p.channels) {
            if (c->kind == ChannelKind::Audio)
                continue;
            SnapPatternChannel pc;
            pc.channel = chanIndex[c->id];
            const bool oneShot = c->kind != ChannelKind::Instrument;
            if (auto it = pat->stepData.find(c->id); it != pat->stepData.end()) {
                const auto& steps = it->second;
                for (int i = 0; i < std::min<int>(pat->steps, int(steps.size())); ++i) {
                    if (!steps[size_t(i)])
                        continue;
                    Tick start = Tick(i) * kStepTicks;
                    if (i % 2 == 1)
                        start += Tick(swing * float(kStepTicks) * 0.5f);
                    SnapNote n;
                    n.start = start;
                    n.end = oneShot ? kNever : start + kStepTicks;
                    n.key = uint8_t(c->kind == ChannelKind::Pads ? kPadBaseKey : kRootKey);
                    n.vel = float(steps[size_t(i)]) / 127.f;
                    pc.notes.push_back(n);
                }
            }
            if (auto it = pat->notes.find(c->id); it != pat->notes.end()) {
                for (const auto& note : it->second) {
                    SnapNote n;
                    n.start = note.start;
                    n.end = c->kind == ChannelKind::Pads ? kNever : note.start + std::max<Tick>(1, note.length);
                    n.key = uint8_t(std::clamp(note.key, 0, 127));
                    n.vel = std::clamp(note.velocity, 0.f, 1.f);
                    pc.notes.push_back(n);
                }
            }
            if (pc.notes.empty())
                continue;
            std::stable_sort(pc.notes.begin(), pc.notes.end(),
                             [](const SnapNote& a, const SnapNote& b) { return a.start < b.start; });
            sp.channels.push_back(std::move(pc));
        }
        patIndex[pat->id] = int(s->patterns.size());
        s->patterns.push_back(std::move(sp));
    }
    if (auto it = patIndex.find(p.currentPatternId()); it != patIndex.end())
        s->currentPattern = it->second;

    // ---- automation
    auto clearFlag = [](const ParamPtr& pp) { if (pp) pp->automated.store(false); };
    for (const auto& c : p.channels) {
        for (auto* pp : {&c->volume, &c->pan, &c->pitch, &c->filterType, &c->cutoff, &c->resonance, &c->attack, &c->release})
            clearFlag(*pp);
    }
    for (const auto& m : p.inserts)
        for (auto* pp : {&m->volume, &m->pan, &m->filterType, &m->cutoff, &m->resonance})
            clearFlag(*pp);
    clearFlag(p.swing);

    std::unordered_map<ObjId, int> autoIndex;
    for (const auto& a : p.automations) {
        SnapAutomation sa;
        sa.points = a->points;
        if (ParamPtr pp = p.resolveParam(a->target)) {
            sa.target.param = raw(pp);
            pp->automated.store(true);
        } else {
            uint32_t pid = 0;
            if (PluginPtr pl = p.resolvePluginTarget(a->target, &pid)) {
                keep(pl);
                sa.target.plugin = pl.get();
                sa.target.pluginParam = pid;
                for (int i = 0; i < pl->paramCount(); ++i) {
                    const auto info = pl->paramInfo(i);
                    if (info.id == pid) {
                        sa.target.pluginMin = info.min;
                        sa.target.pluginMax = info.max;
                        break;
                    }
                }
            }
        }
        autoIndex[a->id] = int(s->automations.size());
        s->automations.push_back(std::move(sa));
    }

    // ---- playlist
    for (const auto& c : p.clips) {
        if (c.track >= 0 && c.track < int(p.tracks.size()) && p.tracks[size_t(c.track)].muted)
            continue;
        SnapClip sc{c.kind, c.start, c.end(), c.offset, -1};
        switch (c.kind) {
        case ClipKind::Pattern:
            if (auto it = patIndex.find(c.ref); it != patIndex.end())
                sc.index = it->second;
            break;
        case ClipKind::Audio:
            if (auto it = chanIndex.find(c.ref); it != chanIndex.end())
                sc.index = it->second;
            break;
        case ClipKind::Automation:
            if (auto it = autoIndex.find(c.ref); it != autoIndex.end())
                sc.index = it->second;
            break;
        }
        if (sc.index >= 0)
            s->clips.push_back(sc);
    }
    std::stable_sort(s->clips.begin(), s->clips.end(), [](const SnapClip& a, const SnapClip& b) { return a.start < b.start; });

    s->songEnd = p.songLength();
    s->loopStart = p.loopStart;
    s->loopEnd = p.loopEnd;
    return s;
}

} // namespace guiro
