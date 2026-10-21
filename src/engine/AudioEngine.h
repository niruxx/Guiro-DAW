#pragma once

#include "core/Project.h"
#include "core/Types.h"
#include "engine/Snapshot.h"
#include "engine/SpscQueue.h"

#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include <vector>

namespace guiro {

class AudioBackend;

struct MidiMsg {
    uint32_t frame;
    uint8_t data[3];
    uint8_t size;
};

struct RecordedNote {
    ObjId channel;
    int key;
    float velocity;
    double startTick;
    double endTick;   // < 0 while held
};

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(Project* project, QObject* parent = nullptr);
    ~AudioEngine() override;

    static AudioEngine* instance() { return s_instance; }

    bool start();
    void shutdown();
    QString backendName() const;
    QString backendError() const { return m_backendError; }
    double sampleRate() const { return m_sampleRate; }
    int bufferSize() const;

    // ---- transport (GUI thread)
    void play();
    void stop();
    void togglePlay();
    bool isPlaying() const { return m_guiPlaying; }
    void setSongMode(bool song);
    bool songMode() const { return m_guiSongMode; }
    void setPosition(double tick);
    double position() const { return m_posMirror.load(std::memory_order_relaxed); }
    void setMetronome(bool on);
    bool metronome() const { return m_guiMetronome; }
    void setRecording(bool on) { m_recording = on; emit transportChanged(); }
    bool recording() const { return m_recording; }

    // ---- live input (GUI thread)
    void noteOn(ObjId channel, int key, float velocity);
    void noteOff(ObjId channel, int key);
    void previewSample(const SamplePtr& s);
    void stopPreview();
    void panic();
    void setMidiTarget(ObjId channel) { m_midiTarget.store(channel); }

    float cpuLoad() const { return m_load.load(std::memory_order_relaxed); }
    void readScope(float* dst, int n) const;
    bool takeRecordedNote(RecordedNote& out) { return m_recorded.pop(out); }

    // ---- audio thread
    void process(float* outL, float* outR, int frames, const MidiMsg* midi, int numMidi);

    // ---- offline rendering (export)
    bool beginOffline();
    void offlineProcess(float* outL, float* outR, int frames);
    double offlineTick() const { return m_pos; }
    Tick offlineSongEnd() const;
    void endOffline();

public slots:
    void rebuildSnapshot();

signals:
    void transportChanged();

private:
    struct Cmd {
        enum Type : uint8_t { Play, Stop, Seek, SongMode, NoteOn, NoteOff, Preview, StopPreview, Panic, Metronome } type;
        ObjId channel = 0;
        int key = 0;
        float vel = 0.f;
        double pos = 0.0;
        const Sample* sample = nullptr;
        bool flag = false;
    };

    void send(const Cmd& c);
    void drainCommands(Snapshot* s);
    void renderBlock(Snapshot* s, float* outL, float* outR, int n, const MidiMsg* midi, int numMidi, int midiBase);
    void scheduleRange(Snapshot* s, double tA, double tB, int frameBase, int n, double fpt);
    void schedulePattern(Snapshot* s, const SnapPattern& p, double clipStart, double clipEnd, double offset,
                         double tA, double tB, int frameBase, int n, double fpt);
    void renderAudioClip(Snapshot* s, const SnapClip& c, double tA, double tB, int frameBase, int n, double fpt);
    void scheduleEnds(Snapshot* s, double tA, double tB, int frameBase, int n, double fpt);
    void applyAutomation(Snapshot* s, double t);
    void broadcast(Snapshot* s, EvType type, uint32_t frame);
    void loopRange(Snapshot* s, double& start, double& end) const;
    void collectGarbage();

    Project* m_project;
    std::unique_ptr<AudioBackend> m_backend;
    QString m_backendError;
    double m_sampleRate = 48000.0;

    std::atomic<Snapshot*> m_snap{nullptr};
    struct Retired {
        Snapshot* snap;
        uint64_t cycle;
    };
    std::vector<Retired> m_retired;
    std::atomic<uint64_t> m_cycles{0};
    QTimer m_gcTimer;

    SpscQueue<Cmd, 2048> m_cmds;
    SpscQueue<RecordedNote, 1024> m_recorded;

    // audio-thread transport state
    bool m_playing = false;
    bool m_songMode = false;
    bool m_metronomeOn = false;
    bool m_exporting = false;
    double m_pos = 0.0;
    int64_t m_samplePos = 0;
    double m_recStart[128] = {};

    // GUI mirrors
    bool m_guiPlaying = false;
    bool m_guiSongMode = false;
    bool m_guiMetronome = false;
    std::atomic<bool> m_recording{false};
    std::atomic<double> m_posMirror{0.0};
    std::atomic<ObjId> m_midiTarget{0};
    std::atomic<bool> m_offline{false};
    std::atomic<float> m_load{0.f};

    // metronome click
    int m_clickRemain = 0;
    int m_clickDelay = 0;
    double m_clickPhase = 0.0;
    double m_clickFreq = 1500.0;

    // sample preview
    const Sample* m_preview = nullptr;
    double m_previewPos = 0.0;

    alignas(16) float m_fxIn[2][kMaxBlock];
    alignas(16) float m_fxOut[2][kMaxBlock];

    static constexpr int kScopeSize = 2048;
    float m_scope[kScopeSize] = {};
    std::atomic<uint32_t> m_scopeWrite{0};

    static AudioEngine* s_instance;
};

} // namespace guiro
