#include "plugins/Vst2Host.h"
#include "core/Types.h"
#include "plugins/Vst2Abi.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace guiro {

using namespace vst2;

namespace {

vst2::EntryProc loadEntry(const QString& path, QString* error)
{
    // Modules stay loaded for the session, like the other formats.
    static QHash<QString, void*> s_libs;
    void* lib = s_libs.value(path);
    if (!lib) {
        lib = dlopen(path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            if (error)
                *error = QString::fromLocal8Bit(dlerror());
            return nullptr;
        }
        s_libs.insert(path, lib);
    }
    auto entry = reinterpret_cast<EntryProc>(dlsym(lib, "VSTPluginMain"));
    if (!entry)
        entry = reinterpret_cast<EntryProc>(dlsym(lib, "main"));
    if (!entry && error)
        *error = QStringLiteral("Not a VST2 plugin (no VSTPluginMain)");
    return entry;
}

class Vst2Instance;
thread_local Vst2Instance* t_creating = nullptr;

class Vst2Instance : public PluginInstance {
public:
    explicit Vst2Instance(PluginDescriptor d) : PluginInstance(std::move(d)) {}

    ~Vst2Instance() override
    {
        *m_alive = false;
        if (!m_fx)
            return;
        closeEditor();
        deactivate();
        dispatch(effClose);
        m_fx = nullptr;
    }

    bool init(QString* error)
    {
        EntryProc entry = loadEntry(m_desc.path, error);
        if (!entry)
            return false;
        t_creating = this;
        m_fx = entry(&Vst2Instance::hostCallback);
        t_creating = nullptr;
        if (!m_fx || m_fx->magic != kMagic) {
            m_fx = nullptr;
            if (error)
                *error = QStringLiteral("The plugin did not return a valid VST2 effect");
            return false;
        }
        m_fx->resvd1 = reinterpret_cast<IntPtr>(this);
        dispatch(effOpen);
        if (dispatch(effGetPlugCategory) == kPlugCategShell) {
            if (error)
                *error = QStringLiteral("VST2 shell plugins (Waves etc.) are not supported");
            return false;
        }
        return true;
    }

    AEffect* effect() const { return m_fx; }

    IntPtr dispatch(Int32 op, Int32 index = 0, IntPtr value = 0, void* ptr = nullptr, float opt = 0.f) const
    {
        return m_fx ? m_fx->dispatcher(m_fx, op, index, value, ptr, opt) : 0;
    }

    QString stringOp(Int32 op, Int32 index = 0) const
    {
        char buf[256] = {};
        dispatch(op, index, 0, buf);
        return QString::fromLocal8Bit(buf).trimmed();
    }

    // ---- PluginInstance
    bool activate(double sampleRate, int maxBlock) override
    {
        if (m_active)
            return true;
        m_sampleRate = sampleRate;
        m_maxBlock = std::max(maxBlock, kMaxBlock);
        dispatch(effSetSampleRate, 0, 0, nullptr, float(sampleRate));
        dispatch(effSetBlockSize, 0, m_maxBlock);
        const int channels = std::max({m_fx->numInputs, m_fx->numOutputs, 2});
        m_scratch.assign(size_t(channels) * 2, std::vector<float>(size_t(m_maxBlock), 0.f));
        m_inPtrs.assign(size_t(std::max(m_fx->numInputs, 1)), nullptr);
        m_outPtrs.assign(size_t(std::max(m_fx->numOutputs, 1)), nullptr);
        dispatch(effMainsChanged, 0, 1);
        dispatch(effStartProcess);
        m_latency = m_fx->initialDelay;
        m_active = true;
        return true;
    }

    void deactivate() override
    {
        if (!m_active)
            return;
        suspend();
        dispatch(effStopProcess);
        dispatch(effMainsChanged, 0, 0);
        m_active = false;
        resume();
    }

    void process(const float* const* in, float** out, int frames, const PluginNote* notes, int numNotes,
                 const TransportInfo& t) override
    {
        if (!m_active || !m_fx->processReplacing || !enterProcess())
            return;

        ParamChange pc;
        while (m_paramQueue.pop(pc))
            m_fx->setParameter(m_fx, Int32(pc.id), float(pc.value));
        for (int i = 0; i < m_numAuto; ++i)
            m_fx->setParameter(m_fx, Int32(m_auto[i].id), float(m_auto[i].value));
        m_numAuto = 0;

        // Time info is handed out through audioMasterGetTime.
        m_time.sampleRate = m_sampleRate;
        m_time.samplePos = double(t.samplePos);
        m_time.ppqPos = t.beatPos;
        m_time.tempo = t.bpm;
        m_time.barStartPos = std::floor(t.beatPos / 4.0) * 4.0;
        m_time.timeSigNumerator = 4;
        m_time.timeSigDenominator = 4;
        m_time.flags = kVstPpqPosValid | kVstTempoValid | kVstBarsValid | kVstTimeSigValid |
                       (t.playing ? kVstTransportPlaying : 0) | (t.playing != m_wasPlaying ? kVstTransportChanged : 0);
        m_wasPlaying = t.playing;

        if (numNotes > 0) {
            const int n = std::min(numNotes, kMaxEvents);
            for (int i = 0; i < n; ++i) {
                VstMidiEvent& e = m_midi[i];
                std::memset(&e, 0, sizeof(e));
                e.type = kVstMidiType;
                e.byteSize = sizeof(VstMidiEvent);
                e.deltaFrames = Int32(notes[i].frame);
                e.midiData[0] = char(notes[i].on ? 0x90 : 0x80);
                e.midiData[1] = char(notes[i].key);
                e.midiData[2] = char(notes[i].on ? std::clamp(int(notes[i].velocity * 127.f), 1, 127) : 0);
                m_events.events[i] = reinterpret_cast<VstEvent*>(&e);
            }
            m_events.numEvents = n;
            m_events.reserved = 0;
            dispatch(effProcessEvents, 0, 0, &m_events);
        }

        size_t scratch = 0;
        for (int c = 0; c < m_fx->numInputs; ++c) {
            float* p = m_scratch[scratch++].data();
            if (in && c < 2)
                std::memcpy(p, in[c], sizeof(float) * size_t(frames));
            else
                std::memset(p, 0, sizeof(float) * size_t(frames));
            m_inPtrs[size_t(c)] = p;
        }
        for (int c = 0; c < m_fx->numOutputs; ++c) {
            m_outPtrs[size_t(c)] = c < 2 ? out[c] : m_scratch[scratch++].data();
        }
        if (m_fx->numOutputs > 0)
            m_fx->processReplacing(m_fx, m_inPtrs.data(), m_outPtrs.data(), frames);
        if (m_fx->numOutputs == 1)
            std::memcpy(out[1], out[0], sizeof(float) * size_t(frames));
        leaveProcess();
    }

    int paramCount() const override { return m_fx ? m_fx->numParams : 0; }

    PluginParamInfo paramInfo(int i) const override
    {
        PluginParamInfo p;
        p.id = uint32_t(i);
        p.name = stringOp(effGetParamName, i);
        if (p.name.isEmpty())
            p.name = QStringLiteral("Param %1").arg(i + 1);
        p.min = 0.0;
        p.max = 1.0;
        p.def = 0.0;
        return p;
    }

    double paramValue(uint32_t id) const override
    {
        return m_fx && int(id) < m_fx->numParams ? double(m_fx->getParameter(m_fx, Int32(id))) : 0.0;
    }

    QString paramText(uint32_t id, double) const override
    {
        const QString v = stringOp(effGetParamDisplay, Int32(id));
        const QString unit = stringOp(effGetParamLabel, Int32(id));
        return unit.isEmpty() ? v : v + QLatin1Char(' ') + unit;
    }

    void setParamValue(uint32_t id, double value) override
    {
        if (m_active)
            m_paramQueue.push({id, value});
        else if (m_fx && int(id) < m_fx->numParams)
            m_fx->setParameter(m_fx, Int32(id), float(value));
    }

    QByteArray saveState() override
    {
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        ds << quint32(0x47565332);   // "GVS2"
        if (m_fx->flags & effFlagsProgramChunks) {
            void* data = nullptr;
            const IntPtr size = dispatch(effGetChunk, 0, 0, &data);
            ds << quint8(0) << QByteArray(static_cast<const char*>(data), data ? qsizetype(size) : 0);
        } else {
            ds << quint8(1) << qint32(m_fx->numParams);
            for (int i = 0; i < m_fx->numParams; ++i)
                ds << m_fx->getParameter(m_fx, i);
        }
        ds << qint32(dispatch(effGetProgram));
        return out;
    }

    bool loadState(const QByteArray& data) override
    {
        QDataStream ds(data);
        quint32 magic = 0;
        quint8 kind = 0;
        ds >> magic >> kind;
        if (magic != 0x47565332)
            return false;
        suspend();
        if (kind == 0) {
            QByteArray chunk;
            ds >> chunk;
            qint32 program = 0;
            ds >> program;
            dispatch(effSetProgram, 0, program);
            if (!chunk.isEmpty())
                dispatch(effSetChunk, 0, chunk.size(), chunk.data());
        } else {
            qint32 n = 0;
            ds >> n;
            for (int i = 0; i < n && i < m_fx->numParams; ++i) {
                float v = 0.f;
                ds >> v;
                m_fx->setParameter(m_fx, i, v);
            }
        }
        resume();
        return true;
    }

    bool hasEditor() const override { return m_fx && (m_fx->flags & effFlagsHasEditor); }

    bool openEditor(QWindow* parent, QSize* size) override
    {
        if (!hasEditor() || m_editorOpen)
            return false;
        ERect* rect = nullptr;
        dispatch(effEditGetRect, 0, 0, &rect);   // some plugins need this before opening
        dispatch(effEditOpen, 0, 0, reinterpret_cast<void*>(parent->winId()));
        rect = nullptr;
        dispatch(effEditGetRect, 0, 0, &rect);
        if (rect && rect->right > rect->left && rect->bottom > rect->top)
            *size = QSize(rect->right - rect->left, rect->bottom - rect->top);
        else
            *size = QSize(640, 420);
        m_editorOpen = true;
        return true;
    }

    void closeEditor() override
    {
        if (!m_editorOpen)
            return;
        dispatch(effEditClose);
        m_editorOpen = false;
    }

    void idle() override
    {
        if (m_editorOpen)
            dispatch(effEditIdle);
        if (m_restartRequested.exchange(false) && m_active) {
            deactivate();
            activate(m_sampleRate, m_maxBlock);
        }
    }

    static IntPtr hostCallback(AEffect* fx, Int32 opcode, Int32 index, IntPtr value, void* ptr, float)
    {
        Vst2Instance* self = fx && fx->resvd1 ? reinterpret_cast<Vst2Instance*>(fx->resvd1) : t_creating;
        switch (opcode) {
        case audioMasterVersion:
            return 2400;
        case audioMasterCurrentId:
            return 0;
        case audioMasterIdle:
            return 1;
        case audioMasterGetTime:
            return self ? reinterpret_cast<IntPtr>(&self->m_time) : 0;
        case audioMasterIOChanged:
            if (self)
                self->m_restartRequested = true;
            return 1;
        case audioMasterSizeWindow:
            if (self) {
                std::weak_ptr<bool> alive = self->m_alive;
                const QSize s{int(index), int(value)};
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, alive, s] {
                    if (auto a = alive.lock(); a && *a && self->onEditorResizeRequest)
                        self->onEditorResizeRequest(s);
                }, Qt::QueuedConnection);
            }
            return 1;
        case audioMasterGetSampleRate:
            return self ? IntPtr(self->m_sampleRate) : 48000;
        case audioMasterGetBlockSize:
            return self ? self->m_maxBlock : kMaxBlock;
        case audioMasterGetCurrentProcessLevel:
            return isAudioThread() ? 2 : 1;
        case audioMasterGetAutomationState:
            return 1;
        case audioMasterGetVendorString:
        case audioMasterGetProductString:
            if (ptr)
                std::strcpy(static_cast<char*>(ptr), "Guiro");
            return 1;
        case audioMasterGetVendorVersion:
            return 100;
        case audioMasterCanDo: {
            if (!ptr)
                return 0;
            static const char* yes[] = {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents",
                                        "receiveVstMidiEvent", "sizeWindow", "supplyIdle"};
            for (const char* s : yes)
                if (std::strcmp(static_cast<const char*>(ptr), s) == 0)
                    return 1;
            return 0;
        }
        case audioMasterGetLanguage:
            return 1;
        case audioMasterUpdateDisplay:
        case audioMasterBeginEdit:
        case audioMasterEndEdit:
        case audioMasterAutomate:
            return 1;
        default:
            return 0;
        }
    }

private:
    static constexpr int kMaxEvents = 512;

    AEffect* m_fx = nullptr;
    bool m_active = false;
    bool m_editorOpen = false;
    bool m_wasPlaying = false;
    double m_sampleRate = 48000.0;
    int m_maxBlock = kMaxBlock;
    VstTimeInfo m_time{};
    VstMidiEvent m_midi[kMaxEvents]{};
    VstEventsN<kMaxEvents> m_events{};
    std::vector<std::vector<float>> m_scratch;
    std::vector<float*> m_inPtrs, m_outPtrs;
    std::atomic<bool> m_restartRequested{false};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace

QList<PluginDescriptor> vst2Scan(const QString& path)
{
    PluginDescriptor d;
    d.format = PluginFormat::VST2;
    d.path = path;
    auto inst = std::make_shared<Vst2Instance>(d);
    QString err;
    if (!inst->init(&err))
        return {};
    AEffect* fx = inst->effect();
    d.name = inst->stringOp(effGetEffectName);
    if (d.name.isEmpty())
        d.name = inst->stringOp(effGetProductString);
    if (d.name.isEmpty())
        d.name = QFileInfo(path).completeBaseName();
    d.vendor = inst->stringOp(effGetVendorString);
    const IntPtr cat = inst->dispatch(effGetPlugCategory);
    d.instrument = cat == kPlugCategSynth || (fx->flags & effFlagsIsSynth);
    d.category = d.instrument ? QStringLiteral("Instrument") : QStringLiteral("Effect");
    // The unique id alone is not reliable (many plugins use 0), so add the file name.
    d.uid = QStringLiteral("%1@%2").arg(fx->uniqueID).arg(QFileInfo(path).completeBaseName());
    return {d};
}

PluginPtr vst2Create(const PluginDescriptor& d, QString* error)
{
    auto inst = std::make_shared<Vst2Instance>(d);
    QString err;
    if (!inst->init(&err)) {
        if (error)
            *error = err;
        return nullptr;
    }
    return inst;
}

} // namespace guiro
