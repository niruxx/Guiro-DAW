#pragma once

// Minimal clean-room description of the VST 2.4 binary interface: just the
// structures, opcodes and constants a host needs. VST2 plugins (native Linux
// .so files and Windows .dll files bridged through yabridge) are loaded with it.

#include <cstdint>

namespace guiro::vst2 {

using Int32 = int32_t;
using IntPtr = intptr_t;

constexpr Int32 kMagic = ('V' << 24) | ('s' << 16) | ('t' << 8) | 'P';

struct AEffect;
using HostCallback = IntPtr (*)(AEffect*, Int32 opcode, Int32 index, IntPtr value, void* ptr, float opt);
using DispatcherProc = IntPtr (*)(AEffect*, Int32 opcode, Int32 index, IntPtr value, void* ptr, float opt);
using ProcessProc = void (*)(AEffect*, float** inputs, float** outputs, Int32 frames);
using ProcessDoubleProc = void (*)(AEffect*, double** inputs, double** outputs, Int32 frames);
using SetParameterProc = void (*)(AEffect*, Int32 index, float value);
using GetParameterProc = float (*)(AEffect*, Int32 index);
using EntryProc = AEffect* (*)(HostCallback);

struct AEffect {
    Int32 magic;
    DispatcherProc dispatcher;
    ProcessProc process;   // deprecated accumulating process
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    Int32 numPrograms;
    Int32 numParams;
    Int32 numInputs;
    Int32 numOutputs;
    Int32 flags;
    IntPtr resvd1;   // reserved for the host
    IntPtr resvd2;
    Int32 initialDelay;
    Int32 realQualities;
    Int32 offQualities;
    float ioRatio;
    void* object;
    void* user;
    Int32 uniqueID;
    Int32 version;
    ProcessProc processReplacing;
    ProcessDoubleProc processDoubleReplacing;
    char future[56];
};

enum Flags : Int32 {
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsProgramChunks = 1 << 5,
    effFlagsIsSynth = 1 << 8,
};

// Plugin opcodes (host -> plugin)
enum PluginOpcode : Int32 {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effGetPlugCategory = 35,
    effGetEffectName = 45,
    effGetVendorString = 47,
    effGetProductString = 48,
    effCanDo = 51,
    effGetVstVersion = 58,
    effStartProcess = 71,
    effStopProcess = 72,
};

// Host opcodes (plugin -> host)
enum HostOpcode : Int32 {
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterCurrentId = 2,
    audioMasterIdle = 3,
    audioMasterGetTime = 7,
    audioMasterProcessEvents = 8,
    audioMasterIOChanged = 13,
    audioMasterSizeWindow = 15,
    audioMasterGetSampleRate = 16,
    audioMasterGetBlockSize = 17,
    audioMasterGetCurrentProcessLevel = 23,
    audioMasterGetAutomationState = 24,
    audioMasterGetVendorString = 32,
    audioMasterGetProductString = 33,
    audioMasterGetVendorVersion = 34,
    audioMasterCanDo = 37,
    audioMasterGetLanguage = 38,
    audioMasterUpdateDisplay = 42,
    audioMasterBeginEdit = 43,
    audioMasterEndEdit = 44,
};

enum PlugCategory : Int32 {
    kPlugCategUnknown = 0,
    kPlugCategEffect = 1,
    kPlugCategSynth = 2,
    kPlugCategShell = 10,
    kPlugCategGenerator = 11,
};

struct ERect {
    int16_t top, left, bottom, right;
};

constexpr Int32 kVstMidiType = 1;

struct VstEvent {
    Int32 type;
    Int32 byteSize;
    Int32 deltaFrames;
    Int32 flags;
    char data[16];
};

struct VstMidiEvent {
    Int32 type;
    Int32 byteSize;
    Int32 deltaFrames;
    Int32 flags;
    Int32 noteLength;
    Int32 noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};
static_assert(sizeof(VstMidiEvent) == 32);

// Variable-length in the real ABI: `events` really has numEvents entries.
template <int N>
struct VstEventsN {
    Int32 numEvents;
    IntPtr reserved;
    VstEvent* events[N];
};

enum TimeInfoFlags : Int32 {
    kVstTransportChanged = 1,
    kVstTransportPlaying = 1 << 1,
    kVstNanosValid = 1 << 8,
    kVstPpqPosValid = 1 << 9,
    kVstTempoValid = 1 << 10,
    kVstBarsValid = 1 << 11,
    kVstTimeSigValid = 1 << 13,
};

struct VstTimeInfo {
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    Int32 timeSigNumerator;
    Int32 timeSigDenominator;
    Int32 smpteOffset;
    Int32 smpteFrameRate;
    Int32 samplesToNextClock;
    Int32 flags;
};

} // namespace guiro::vst2
