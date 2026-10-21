#pragma once

#include "core/Sample.h"
#include "core/Types.h"
#include "engine/Dsp.h"
#include "plugins/PluginInstance.h"

#include <array>
#include <atomic>

namespace guiro {

enum class EvType : uint8_t {
    NoteOn,
    NoteOff,            // release live notes with this key
    ReleaseSequenced,   // release every sequenced (non-live, finite) note
    ReleaseEnded,       // release sequenced notes whose end tick has passed (uses endTick as "now")
    AllOff,             // stop everything (transport stop)
};

struct ChEvent {
    uint32_t frame = 0;
    EvType type = EvType::NoteOn;
    uint8_t key = 0;
    bool live = false;
    float vel = 0.f;
    Tick endTick = kNever;
};

struct Voice {
    const Sample* sample = nullptr;
    double pos = 0.0;
    double inc = 1.0;
    float gain = 1.f;
    float env = 0.f;
    float attackStep = 1.f;
    float releaseCoef = 0.99f;
    enum Stage : uint8_t { Off, Attack, Sustain, Release } stage = Off;
    uint8_t key = 0;
    bool live = false;
    Tick endTick = kNever;
    uint32_t age = 0;
};

// Per-channel realtime state. Owned by the Channel, touched only by the audio thread
// (except the atomics read by the UI).
struct ChannelRt {
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxEvents = 512;

    std::array<Voice, kMaxVoices> voices{};
    uint32_t ageCounter = 0;

    ChEvent events[kMaxEvents];
    int numEvents = 0;

    // Plugin note tracking (instrument channels).
    bool held[128] = {};
    bool heldLive[128] = {};
    Tick heldEnd[128] = {};
    PluginNote pluginNotes[kMaxEvents];
    int numPluginNotes = 0;

    alignas(16) float buf[2][kMaxBlock];
    dsp::Svf filter;
    float lastCutoff = 20000.f;
    float gainL = 0.f, gainR = 0.f;
    bool gainInit = false;

    std::atomic<float> activity{0.f};   // peak level, read by the UI for the channel LED

    void push(const ChEvent& e)
    {
        if (numEvents < kMaxEvents)
            events[numEvents++] = e;
    }
    void sortEvents();
    bool hasActiveVoices() const;
};

struct InsertRt {
    alignas(16) float buf[2][kMaxBlock];
    dsp::Svf filter;
    float lastCutoff = 20000.f;
    float gainL = 0.f, gainR = 0.f;
    bool gainInit = false;
    std::atomic<float> peakL{0.f};
    std::atomic<float> peakR{0.f};
};

struct SnapChannel;

// Renders all sampler voices of a channel (Sampler or Pads kind) into rt.buf,
// applying the queued events at their frame offsets.
void renderSamplerChannel(ChannelRt& rt, const SnapChannel& ch, int n, float sr);
// Converts queued events into plugin notes and calls the instrument.
void renderInstrumentChannel(ChannelRt& rt, const SnapChannel& ch, int n, const TransportInfo& t);

void storePeak(std::atomic<float>& a, float v);

} // namespace guiro
