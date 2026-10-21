#pragma once

#include "core/Param.h"
#include "core/Sample.h"
#include "core/Types.h"
#include "plugins/PluginInstance.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace guiro {

struct ChannelRt;
struct InsertRt;

struct Note {
    int key = kRootKey;
    Tick start = 0;
    Tick length = kStepTicks;
    float velocity = 0.78f;
};

struct PadSlot {
    SamplePtr sample;
    ParamPtr gain;
    ParamPtr pitch;
    QColor color;
};

struct Channel {
    ObjId id = 0;
    ChannelKind kind = ChannelKind::Sampler;
    QString name;
    QColor color;
    bool muted = false;
    int insert = 0;                 // mixer insert, 0 = master

    SamplePtr sample;               // Sampler / Audio
    std::array<PadSlot, kNumPads> pads; // Pads
    PluginPtr plugin;               // Instrument

    ParamPtr volume, pan, pitch, filterType, cutoff, resonance, attack, release;
    std::shared_ptr<ChannelRt> rt;
};

struct Pattern {
    ObjId id = 0;
    QString name;
    QColor color;
    int steps = 16;
    std::map<ObjId, std::vector<uint8_t>> stepData;   // channel -> velocity per step (0 = off)
    std::map<ObjId, std::vector<Note>> notes;          // channel -> piano roll notes

    Tick length() const;
    bool hasContentFor(ObjId channel) const;
    std::vector<uint8_t>& stepsFor(ObjId channel);
};

struct AutoPoint {
    Tick t = 0;
    float v = 0.f;   // normalized
};

struct AutomationClip {
    ObjId id = 0;
    QString name;
    QColor color;
    QString target;   // param path
    std::vector<AutoPoint> points;

    float valueAt(Tick t) const;
    void sortPoints();
};

struct Clip {
    ObjId id = 0;
    ClipKind kind = ClipKind::Pattern;
    ObjId ref = 0;          // pattern / channel / automation id
    int track = 0;
    Tick start = 0;
    Tick length = kBarTicks;
    Tick offset = 0;        // trim from source start
    Tick end() const { return start + length; }
};

struct Track {
    QString name;
    QColor color;
    bool muted = false;
};

struct FxSlot {
    PluginPtr plugin;
    bool bypass = false;
};

struct MixerInsert {
    int index = 0;
    QString name;
    QColor color;
    bool muted = false;
    bool solo = false;
    ParamPtr volume, pan, filterType, cutoff, resonance;
    std::array<FxSlot, kNumFxSlots> fx;
    std::shared_ptr<InsertRt> rt;
};

// The document. Lives on the GUI thread; the engine sees immutable snapshots
// built from it plus the lock-free Params it shares.
class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);
    ~Project() override;

    void clear();
    void createEmpty();     // blank project: one empty pattern, no channels
    void createDefault();   // demo song with the built-in kit

    ObjId newId() { return m_nextId++; }
    void bumpId(ObjId used) { if (used >= m_nextId) m_nextId = used + 1; }

    // --- tempo & global
    double bpm() const { return m_bpm.load(std::memory_order_relaxed); }
    void setBpm(double bpm);
    std::atomic<double>& bpmAtomic() { return m_bpm; }
    ParamPtr swing;
    ParamPtr masterPitch;

    // --- channels
    std::vector<std::shared_ptr<Channel>> channels;
    Channel* channel(ObjId id) const;
    int channelIndex(ObjId id) const;
    std::shared_ptr<Channel> createChannel(ChannelKind kind, const QString& name);
    Channel* addSampler(SamplePtr s, int atIndex = -1);
    Channel* addAudioChannel(SamplePtr s);
    Channel* addInstrument(PluginPtr p);
    Channel* addPads();
    void removeChannel(ObjId id);
    void moveChannel(ObjId id, int delta);
    ObjId cloneChannel(ObjId id);
    int nextFreeInsert() const;

    ObjId selectedChannel() const { return m_selectedChannel; }
    void setSelectedChannel(ObjId id);

    // --- patterns
    std::vector<std::shared_ptr<Pattern>> patterns;
    Pattern* pattern(ObjId id) const;
    Pattern* currentPattern() const { return pattern(m_currentPattern); }
    ObjId currentPatternId() const { return m_currentPattern; }
    void setCurrentPattern(ObjId id);
    Pattern* addPattern();
    void removePattern(ObjId id);
    ObjId clonePattern(ObjId id);

    // --- automation
    std::vector<std::shared_ptr<AutomationClip>> automations;
    AutomationClip* automation(ObjId id) const;
    AutomationClip* addAutomation(const QString& target);
    void removeAutomation(ObjId id);

    // --- playlist
    std::vector<Clip> clips;
    std::vector<Track> tracks;
    Clip* clip(ObjId id);
    Clip& addClip(ClipKind kind, ObjId ref, int track, Tick start, Tick length);
    void removeClip(ObjId id);
    Tick sourceLength(const Clip& c) const;
    Tick songLength() const;
    int firstFreeTrack(Tick start, Tick end) const;
    Tick loopStart = 0, loopEnd = 0;    // song-mode loop (end <= start: none)

    // --- mixer
    std::vector<std::shared_ptr<MixerInsert>> inserts;   // [0] master
    MixerInsert* insert(int i) const { return i >= 0 && i < int(inserts.size()) ? inserts[size_t(i)].get() : nullptr; }

    // --- automation targets
    ParamPtr resolveParam(const QString& path) const;
    QString describeTarget(const QString& path) const;
    // Plugin parameter targets are "plugin/<owner>/<paramId>" where owner is
    // "ch<id>" or "fx<insert>.<slot>".
    PluginPtr resolvePluginTarget(const QString& path, uint32_t* paramId) const;

    // --- document state
    QString filePath;
    bool isModified() const { return m_modified; }
    void setModified(bool m);

    // Notify listeners and schedule an engine snapshot rebuild.
    void notifyChannels();
    void notifyPatterns();
    void notifyPatternEdited(ObjId pattern);
    void notifyPlaylist();
    void notifyMixer();
    void notifyAutomation();
    void notifyStructure();     // everything

    static QColor paletteColor(int i);

signals:
    void channelsChanged();
    void patternsChanged();
    void patternEdited(ObjId pattern);
    void playlistChanged();
    void mixerChanged();
    void automationChanged();
    void currentPatternChanged(ObjId pattern);
    void selectedChannelChanged(ObjId channel);
    void tempoChanged(double bpm);
    void modifiedChanged(bool);
    void snapshotNeeded();
    void projectReset();

private:
    void initChannelParams(Channel& c);
    void initInsert(MixerInsert& m, int index);
    void requestSnapshot();

    std::atomic<double> m_bpm{130.0};
    ObjId m_nextId = 1;
    ObjId m_currentPattern = 0;
    ObjId m_selectedChannel = 0;
    bool m_modified = false;
    bool m_snapshotPending = false;
};

} // namespace guiro
