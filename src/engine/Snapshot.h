#pragma once

#include "core/Param.h"
#include "core/Project.h"
#include "core/Types.h"
#include "engine/Runtime.h"

#include <memory>
#include <vector>

namespace guiro {

// Everything the audio thread needs, flattened into plain vectors. A snapshot is
// immutable once published; the GUI builds a new one after every structural edit.

struct SnapNote {
    Tick start;
    Tick end;          // kNever for one-shots
    uint8_t key;
    float vel;
};

struct SnapPatternChannel {
    int channel;                  // index into Snapshot::channels
    std::vector<SnapNote> notes;  // sorted by start
};

struct SnapPattern {
    ObjId id = 0;
    Tick length = kBarTicks;
    std::vector<SnapPatternChannel> channels;
};

struct SnapChannel {
    ObjId id = 0;
    ChannelKind kind = ChannelKind::Sampler;
    ChannelRt* rt = nullptr;
    const Sample* sample = nullptr;
    std::array<const Sample*, kNumPads> pads{};
    std::array<Param*, kNumPads> padGain{};
    std::array<Param*, kNumPads> padPitch{};
    PluginInstance* plugin = nullptr;
    Param* volume = nullptr;
    Param* pan = nullptr;
    Param* pitch = nullptr;
    Param* filterType = nullptr;
    Param* cutoff = nullptr;
    Param* resonance = nullptr;
    Param* attack = nullptr;
    Param* release = nullptr;
    int insert = 0;
    bool muted = false;
};

struct AutoTarget {
    Param* param = nullptr;
    PluginInstance* plugin = nullptr;
    uint32_t pluginParam = 0;
    double pluginMin = 0.0, pluginMax = 1.0;
};

struct SnapAutomation {
    std::vector<AutoPoint> points;
    AutoTarget target;
    float valueAt(Tick t) const;
};

struct SnapClip {
    ClipKind kind;
    Tick start, end, offset;
    int index;     // pattern / channel / automation index
};

struct SnapInsert {
    int index = 0;
    InsertRt* rt = nullptr;
    Param* volume = nullptr;
    Param* pan = nullptr;
    Param* filterType = nullptr;
    Param* cutoff = nullptr;
    Param* resonance = nullptr;
    bool muted = false;
    bool solo = false;
    std::array<PluginInstance*, kNumFxSlots> fx{};
};

struct Snapshot {
    std::vector<SnapChannel> channels;
    std::vector<SnapPattern> patterns;
    std::vector<SnapAutomation> automations;
    std::vector<SnapClip> clips;           // sorted by start
    std::vector<SnapInsert> inserts;       // [0] = master
    int currentPattern = -1;
    Tick songEnd = kBarTicks;
    Tick loopStart = 0, loopEnd = 0;
    bool anySolo = false;
    float swing = 0.f;

    // Keeps channel/insert runtimes, plugins and params alive while in use.
    std::vector<std::shared_ptr<void>> keepAlive;

    int findChannel(ObjId id) const
    {
        for (size_t i = 0; i < channels.size(); ++i)
            if (channels[i].id == id)
                return int(i);
        return -1;
    }
};

Snapshot* buildSnapshot(const Project& p);

} // namespace guiro
