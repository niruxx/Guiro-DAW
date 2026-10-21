#pragma once

#include <cstdint>
#include <limits>

namespace guiro {

using Tick = int64_t;
using ObjId = uint32_t;

// Timeline resolution, same as FL Studio's default.
constexpr int kPPQ = 96;
constexpr Tick kStepTicks = kPPQ / 4;   // one 16th note
constexpr Tick kBeatTicks = kPPQ;
constexpr Tick kBarTicks = kPPQ * 4;
constexpr Tick kNever = std::numeric_limits<Tick>::max() / 4;

// Audio engine renders in sub-blocks of at most this many frames so that
// automation and note timing stay tight regardless of the backend period.
constexpr int kMaxBlock = 64;

constexpr int kNumInserts = 16;   // plus the master at index 0
constexpr int kNumFxSlots = 8;
constexpr int kNumTracks = 48;
constexpr int kNumPads = 16;
constexpr int kPadBaseKey = 36;   // C2 (GM drum map start)
constexpr int kRootKey = 60;      // C5 in FL terms - samples play at their original pitch

enum class ChannelKind : uint8_t { Sampler, Instrument, Pads, Audio };
enum class ClipKind : uint8_t { Pattern, Audio, Automation };
enum class FilterType : uint8_t { Off, LowPass, HighPass, BandPass };

// True on any thread that runs the realtime process callback (incl. offline render).
bool isAudioThread();
void setAudioThread(bool);

} // namespace guiro
