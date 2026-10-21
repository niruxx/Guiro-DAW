#pragma once

#include "engine/SpscQueue.h"

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QWindow>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace guiro {

enum class PluginFormat { VST3, CLAP, LV2, VST2 };

QString formatName(PluginFormat f);
bool formatFromName(const QString& s, PluginFormat* out);

struct PluginDescriptor {
    PluginFormat format = PluginFormat::CLAP;
    QString uid;        // VST3 class id (hex), CLAP plugin id or LV2 URI
    QString path;       // module / bundle path (empty for LV2)
    QString name;
    QString vendor;
    QString category;
    bool instrument = false;
    bool bridged = false;   // Windows plugin running under Wine via yabridge

    QString key() const { return formatName(format) + QLatin1Char(':') + uid; }
};

struct PluginParamInfo {
    uint32_t id = 0;
    QString name;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    bool stepped = false;
    bool hidden = false;
};

struct PluginNote {
    uint32_t frame;
    bool on;
    uint8_t key;
    float velocity;
};

struct TransportInfo {
    bool playing = false;
    double bpm = 130.0;
    double beatPos = 0.0;      // position in quarter notes
    int64_t samplePos = 0;
};

// One loaded plugin. Lifetime and all non-process calls happen on the GUI thread;
// process() is called from the audio thread only.
class PluginInstance {
public:
    explicit PluginInstance(PluginDescriptor d) : m_desc(std::move(d)) {}
    virtual ~PluginInstance() = default;

    const PluginDescriptor& descriptor() const { return m_desc; }
    QString name() const { return m_desc.name; }
    bool isInstrument() const { return m_desc.instrument; }

    virtual bool activate(double sampleRate, int maxBlock) = 0;
    virtual void deactivate() = 0;

    // Stereo in/out (in may be null for instruments). Notes sorted by frame.
    virtual void process(const float* const* in, float** out, int frames,
                         const PluginNote* notes, int numNotes, const TransportInfo& t) = 0;

    // Parameters (plain values).
    virtual int paramCount() const = 0;
    virtual PluginParamInfo paramInfo(int index) const = 0;
    virtual double paramValue(uint32_t id) const = 0;
    virtual QString paramText(uint32_t id, double value) const { return QString::number(value, 'f', 3); }
    // From the GUI thread: queued for the audio thread and forwarded to the plugin.
    virtual void setParamValue(uint32_t id, double value) = 0;
    // From the audio thread (automation): applied on the next process call.
    void automate(uint32_t id, double value)
    {
        if (m_numAuto < kMaxAuto)
            m_auto[m_numAuto++] = {id, value};
    }

    virtual QByteArray saveState() = 0;
    virtual bool loadState(const QByteArray& data) = 0;

    // Native editor (X11 embedding).
    virtual bool hasEditor() const { return false; }
    virtual bool openEditor(QWindow* parent, QSize* size) { return false; }
    virtual void closeEditor() {}
    virtual bool editorCanResize() const { return false; }
    virtual void editorResized(QSize) {}
    std::function<void(QSize)> onEditorResizeRequest;

    // Called ~30x per second on the GUI thread.
    virtual void idle() {}

    int latency() const { return m_latency; }

    // Main thread: block the audio thread from entering process() (e.g. while
    // restarting or loading state). Cheap: the audio thread just outputs silence.
    void suspend()
    {
        m_suspended.store(true);
        while (m_inProcess.load())
            std::this_thread::yield();
    }
    void resume() { m_suspended.store(false); }

protected:
    bool enterProcess()
    {
        m_inProcess.store(true);
        if (m_suspended.load()) {
            m_inProcess.store(false);
            return false;
        }
        return true;
    }
    void leaveProcess() { m_inProcess.store(false); }

    std::atomic<bool> m_suspended{false};
    std::atomic<bool> m_inProcess{false};

    struct ParamChange {
        uint32_t id;
        double value;
    };
    static constexpr int kMaxAuto = 64;
    ParamChange m_auto[kMaxAuto];
    int m_numAuto = 0;
    SpscQueue<ParamChange, 1024> m_paramQueue;

    PluginDescriptor m_desc;
    int m_latency = 0;
};

using PluginPtr = std::shared_ptr<PluginInstance>;

} // namespace guiro
