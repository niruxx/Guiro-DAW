#include "plugins/Vst3Host.h"

#ifdef GUIRO_HAVE_VST3

#include "core/Types.h"

#include <QBuffer>
#include <QDataStream>
#include <QHash>
#include <QSocketNotifier>
#include <QTimer>
#include <map>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace guiro {

namespace {

VST3::Hosting::Module::Ptr loadModule(const QString& path, QString* error)
{
    static QHash<QString, VST3::Hosting::Module::Ptr> s_modules;
    if (auto it = s_modules.find(path); it != s_modules.end())
        return it.value();
    std::string err;
    auto mod = VST3::Hosting::Module::create(path.toStdString(), err);
    if (!mod) {
        if (error)
            *error = QString::fromStdString(err);
        return nullptr;
    }
    s_modules.insert(path, mod);
    return mod;
}

FUnknown* hostContext()
{
    static IPtr<HostApplication> s_host = owned(new HostApplication);
    return s_host.get();
}

// Objects below are owned by the instance; refcounting is a no-op.
#define GUIRO_NOREF                                                   \
    uint32 PLUGIN_API addRef() override { return 1000; }              \
    uint32 PLUGIN_API release() override { return 1000; }

// ---------------------------------------------------------------- run loop (X11 editors)

class RunLoop : public Linux::IRunLoop {
public:
    ~RunLoop()
    {
        for (auto& [h, n] : m_handlers)
            delete n;
        for (auto& [h, t] : m_timers)
            delete t;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    GUIRO_NOREF

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override
    {
        auto* n = new QSocketNotifier(qintptr(fd), QSocketNotifier::Read);
        QObject::connect(n, &QSocketNotifier::activated, [handler, fd] { handler->onFDIsSet(fd); });
        m_handlers.insert({handler, n});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
    {
        auto range = m_handlers.equal_range(handler);
        for (auto it = range.first; it != range.second; ++it) {
            it->second->setEnabled(false);
            it->second->deleteLater();
        }
        m_handlers.erase(handler);
        return kResultOk;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval ms) override
    {
        auto* t = new QTimer;
        t->setInterval(int(std::max<Linux::TimerInterval>(ms, 8)));
        QObject::connect(t, &QTimer::timeout, [handler] { handler->onTimer(); });
        t->start();
        m_timers.insert({handler, t});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override
    {
        auto range = m_timers.equal_range(handler);
        for (auto it = range.first; it != range.second; ++it) {
            it->second->stop();
            it->second->deleteLater();
        }
        m_timers.erase(handler);
        return kResultOk;
    }

private:
    std::multimap<Linux::IEventHandler*, QSocketNotifier*> m_handlers;
    std::multimap<Linux::ITimerHandler*, QTimer*> m_timers;
};

class PlugFrame : public IPlugFrame {
public:
    std::function<void(int, int)> onResize;
    RunLoop runLoop;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = &runLoop;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    GUIRO_NOREF

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override
    {
        if (!view || !r)
            return kInvalidArgument;
        if (onResize)
            onResize(r->getWidth(), r->getHeight());
        view->onSize(r);
        return kResultOk;
    }
};

class ComponentHandler : public IComponentHandler {
public:
    std::function<void(ParamID, ParamValue)> onEdit;
    std::function<void(int32)> onRestart;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    GUIRO_NOREF

    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
    {
        if (onEdit)
            onEdit(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        if (onRestart)
            onRestart(flags);
        return kResultOk;
    }
};

// ---------------------------------------------------------------- instance

class Vst3Instance : public PluginInstance {
public:
    explicit Vst3Instance(PluginDescriptor d) : PluginInstance(std::move(d)), m_events(1024), m_inParams(0), m_outParams(0)
    {
        m_handler.onEdit = [this](ParamID id, ParamValue v) { m_paramQueue.push({id, v}); };
        m_handler.onRestart = [this](int32 flags) {
            if (flags & (kLatencyChanged | kIoChanged))
                m_restartRequested = true;
            if (flags & (kParamValuesChanged | kParamTitlesChanged))
                m_rescanParams = true;
        };
    }

    ~Vst3Instance() override
    {
        closeEditor();
        deactivate();
        if (m_controller)
            m_controller->setComponentHandler(nullptr);
        if (m_cpComponent && m_cpController) {
            m_cpComponent->disconnect(m_cpController);
            m_cpController->disconnect(m_cpComponent);
        }
        m_cpComponent = nullptr;
        m_cpController = nullptr;
        m_processor = nullptr;
        if (m_controller && !m_singleComponent)
            m_controller->terminate();
        m_controller = nullptr;
        if (m_component)
            m_component->terminate();
        m_component = nullptr;
    }

    bool init(QString* error)
    {
        m_module = loadModule(m_desc.path, error);
        if (!m_module)
            return false;
        const auto& factory = m_module->getFactory();
        auto uid = VST3::UID::fromString(m_desc.uid.toStdString());
        if (!uid) {
            *error = QStringLiteral("Invalid class id");
            return false;
        }
        m_component = factory.createInstance<IComponent>(*uid);
        if (!m_component || m_component->initialize(hostContext()) != kResultOk) {
            *error = QStringLiteral("Could not create the VST3 component");
            m_component = nullptr;
            return false;
        }

        IEditController* single = nullptr;
        if (m_component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&single)) == kResultTrue && single) {
            m_controller = owned(single);
            m_singleComponent = true;
        } else {
            TUID cid;
            if (m_component->getControllerClassId(cid) == kResultTrue) {
                m_controller = factory.createInstance<IEditController>(VST3::UID::fromTUID(cid));
                if (m_controller && m_controller->initialize(hostContext()) != kResultOk)
                    m_controller = nullptr;
            }
        }

        m_processor = FUnknownPtr<IAudioProcessor>(m_component);
        if (!m_processor) {
            *error = QStringLiteral("The component has no audio processor");
            return false;
        }

        if (m_controller && !m_singleComponent) {
            m_cpComponent = FUnknownPtr<IConnectionPoint>(m_component);
            m_cpController = FUnknownPtr<IConnectionPoint>(m_controller);
            if (m_cpComponent && m_cpController) {
                m_cpComponent->connect(m_cpController);
                m_cpController->connect(m_cpComponent);
            }
            MemoryStream stream;
            if (m_component->getState(&stream) == kResultOk) {
                stream.seek(0, IBStream::kIBSeekSet, nullptr);
                m_controller->setComponentState(&stream);
            }
        }
        if (m_controller)
            m_controller->setComponentHandler(&m_handler);

        setupBuses();
        scanParams();
        return true;
    }

    bool activate(double sampleRate, int maxBlock) override
    {
        if (m_active)
            return true;
        m_sampleRate = sampleRate;
        m_maxBlock = std::max(maxBlock, kMaxBlock);
        ProcessSetup setup{kRealtime, kSample32, m_maxBlock, sampleRate};
        if (m_processor->setupProcessing(setup) != kResultOk)
            return false;
        allocateBuffers();
        if (m_component->setActive(true) != kResultOk)
            return false;
        m_processor->setProcessing(true);
        m_latency = int(m_processor->getLatencySamples());
        m_active = true;
        return true;
    }

    void deactivate() override
    {
        if (!m_active)
            return;
        suspend();
        m_processor->setProcessing(false);
        m_component->setActive(false);
        m_active = false;
        resume();
    }

    void process(const float* const* in, float** out, int frames, const PluginNote* notes, int numNotes,
                 const TransportInfo& t) override
    {
        if (!m_active || !enterProcess())
            return;

        m_inParams.clearQueue();
        m_outParams.clearQueue();
        m_events.clear();

        auto addParam = [&](ParamID id, ParamValue v) {
            int32 idx = 0;
            if (IParamValueQueue* q = m_inParams.addParameterData(id, idx)) {
                int32 pidx = 0;
                q->addPoint(0, v, pidx);
            }
        };
        ParamChange pc;
        while (m_paramQueue.pop(pc))
            addParam(pc.id, pc.value);
        for (int i = 0; i < m_numAuto; ++i) {
            addParam(m_auto[i].id, m_auto[i].value);
            m_automated.push({m_auto[i].id, m_auto[i].value});
        }
        m_numAuto = 0;

        for (int i = 0; i < numNotes; ++i) {
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = int32(notes[i].frame);
            if (notes[i].on) {
                e.type = Event::kNoteOnEvent;
                e.noteOn.channel = 0;
                e.noteOn.pitch = notes[i].key;
                e.noteOn.velocity = notes[i].velocity;
                e.noteOn.noteId = -1;
            } else {
                e.type = Event::kNoteOffEvent;
                e.noteOff.channel = 0;
                e.noteOff.pitch = notes[i].key;
                e.noteOff.velocity = 0.f;
                e.noteOff.noteId = -1;
            }
            m_events.addEvent(e);
        }

        ProcessContext ctx{};
        ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid |
                    ProcessContext::kTimeSigValid | (t.playing ? ProcessContext::kPlaying : 0);
        ctx.sampleRate = m_sampleRate;
        ctx.projectTimeSamples = t.samplePos;
        ctx.projectTimeMusic = t.beatPos;
        ctx.tempo = t.bpm;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
        ctx.barPositionMusic = std::floor(t.beatPos / 4.0) * 4.0;

        // Main buses map to the host buffers, everything else to scratch.
        for (size_t b = 0; b < m_inBuses.size(); ++b) {
            auto& bus = m_inBuses[b];
            for (int32 c = 0; c < bus.numChannels; ++c) {
                float* p = m_scratch[size_t(m_inScratch[b] + c)].data();
                if (b == 0 && in)
                    p = const_cast<float*>(in[std::min(c, 1)]);
                else
                    std::fill_n(p, frames, 0.f);
                m_inPtrs[b][size_t(c)] = p;
            }
            bus.channelBuffers32 = m_inPtrs[b].data();
            bus.silenceFlags = 0;
        }
        for (size_t b = 0; b < m_outBuses.size(); ++b) {
            auto& bus = m_outBuses[b];
            for (int32 c = 0; c < bus.numChannels; ++c) {
                float* p = (b == 0 && c < 2) ? out[c] : m_scratch[size_t(m_outScratch[b] + c)].data();
                m_outPtrs[b][size_t(c)] = p;
            }
            bus.channelBuffers32 = m_outPtrs[b].data();
            bus.silenceFlags = 0;
        }

        ProcessData data;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = frames;
        data.numInputs = int32(m_inBuses.size());
        data.numOutputs = int32(m_outBuses.size());
        data.inputs = m_inBuses.empty() ? nullptr : m_inBuses.data();
        data.outputs = m_outBuses.empty() ? nullptr : m_outBuses.data();
        data.inputParameterChanges = &m_inParams;
        data.outputParameterChanges = &m_outParams;
        data.inputEvents = &m_events;
        data.outputEvents = nullptr;
        data.processContext = &ctx;
        m_processor->process(data);

        if (!m_outBuses.empty() && m_outBuses[0].numChannels == 1)
            std::copy_n(out[0], frames, out[1]);
        leaveProcess();
    }

    int paramCount() const override { return int(m_paramInfos.size()); }
    PluginParamInfo paramInfo(int i) const override
    {
        return i >= 0 && i < int(m_paramInfos.size()) ? m_paramInfos[size_t(i)] : PluginParamInfo{};
    }
    double paramValue(uint32_t id) const override
    {
        return m_controller ? m_controller->getParamNormalized(id) : 0.0;
    }
    QString paramText(uint32_t id, double value) const override
    {
        String128 s{};
        if (m_controller && m_controller->getParamStringByValue(id, value, s) == kResultOk)
            return QString::fromStdString(Steinberg::Vst::StringConvert::convert(s));
        return QString::number(value, 'f', 3);
    }
    void setParamValue(uint32_t id, double value) override
    {
        if (m_controller)
            m_controller->setParamNormalized(id, value);
        m_paramQueue.push({id, value});
    }

    QByteArray saveState() override
    {
        QByteArray comp, ctrl;
        {
            MemoryStream s;
            if (m_component->getState(&s) == kResultOk)
                comp = QByteArray(s.getData(), qsizetype(s.getSize()));
        }
        if (m_controller && !m_singleComponent) {
            MemoryStream s;
            if (m_controller->getState(&s) == kResultOk)
                ctrl = QByteArray(s.getData(), qsizetype(s.getSize()));
        }
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        ds << quint32(0x47565354) << comp << ctrl;
        return out;
    }

    bool loadState(const QByteArray& data) override
    {
        QDataStream ds(data);
        quint32 magic = 0;
        QByteArray comp, ctrl;
        ds >> magic >> comp >> ctrl;
        if (magic != 0x47565354)
            return false;
        suspend();
        if (!comp.isEmpty()) {
            MemoryStream s(comp.data(), comp.size());
            m_component->setState(&s);
            if (m_controller) {
                MemoryStream s2(comp.data(), comp.size());
                m_controller->setComponentState(&s2);
            }
        }
        if (!ctrl.isEmpty() && m_controller) {
            MemoryStream s(ctrl.data(), ctrl.size());
            m_controller->setState(&s);
        }
        resume();
        scanParams();
        return true;
    }

    bool hasEditor() const override { return m_controller != nullptr; }

    bool openEditor(QWindow* parent, QSize* size) override
    {
        if (!m_controller || m_view)
            return false;
        IPlugView* view = m_controller->createView(ViewType::kEditor);
        if (!view)
            return false;
        if (view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) {
            view->release();
            return false;
        }
        m_frame.onResize = [this](int w, int h) {
            if (onEditorResizeRequest)
                onEditorResizeRequest(QSize(w, h));
        };
        view->setFrame(&m_frame);
        if (view->attached(reinterpret_cast<void*>(parent->winId()), kPlatformTypeX11EmbedWindowID) != kResultOk) {
            view->setFrame(nullptr);
            view->release();
            return false;
        }
        ViewRect r;
        if (view->getSize(&r) == kResultOk)
            *size = QSize(r.getWidth(), r.getHeight());
        else
            *size = QSize(640, 420);
        m_view = view;
        return true;
    }

    void closeEditor() override
    {
        if (!m_view)
            return;
        m_view->removed();
        m_view->setFrame(nullptr);
        m_view->release();
        m_view = nullptr;
    }

    bool editorCanResize() const override { return m_view && m_view->canResize() == kResultTrue; }

    void editorResized(QSize s) override
    {
        if (!m_view)
            return;
        ViewRect r(0, 0, s.width(), s.height());
        if (m_view->checkSizeConstraint(&r) == kResultTrue || true)
            m_view->onSize(&r);
    }

    void idle() override
    {
        ParamChange pc;
        while (m_automated.pop(pc))
            if (m_controller)
                m_controller->setParamNormalized(pc.id, pc.value);
        if (m_restartRequested.exchange(false) && m_active) {
            deactivate();
            setupBuses();
            activate(m_sampleRate, m_maxBlock);
        }
        if (m_rescanParams.exchange(false))
            scanParams();
    }

private:
    void setupBuses()
    {
        const int32 nIn = m_component->getBusCount(kAudio, kInput);
        const int32 nOut = m_component->getBusCount(kAudio, kOutput);
        std::vector<SpeakerArrangement> inArr(static_cast<size_t>(nIn)), outArr(static_cast<size_t>(nOut));
        for (int32 i = 0; i < nIn; ++i)
            m_processor->getBusArrangement(kInput, i, inArr[size_t(i)]);
        for (int32 i = 0; i < nOut; ++i)
            m_processor->getBusArrangement(kOutput, i, outArr[size_t(i)]);
        if (nIn > 0)
            inArr[0] = SpeakerArr::kStereo;
        if (nOut > 0)
            outArr[0] = SpeakerArr::kStereo;
        m_processor->setBusArrangements(inArr.data(), nIn, outArr.data(), nOut);

        for (int32 i = 0; i < nIn; ++i)
            m_component->activateBus(kAudio, kInput, i, i == 0);
        for (int32 i = 0; i < nOut; ++i)
            m_component->activateBus(kAudio, kOutput, i, i == 0);
        const int32 nEvIn = m_component->getBusCount(kEvent, kInput);
        for (int32 i = 0; i < nEvIn; ++i)
            m_component->activateBus(kEvent, kInput, i, i == 0);

        m_inBuses.assign(size_t(nIn), AudioBusBuffers{});
        m_outBuses.assign(size_t(nOut), AudioBusBuffers{});
        for (int32 i = 0; i < nIn; ++i) {
            SpeakerArrangement a = 0;
            m_processor->getBusArrangement(kInput, i, a);
            m_inBuses[size_t(i)].numChannels = SpeakerArr::getChannelCount(a);
        }
        for (int32 i = 0; i < nOut; ++i) {
            SpeakerArrangement a = 0;
            m_processor->getBusArrangement(kOutput, i, a);
            m_outBuses[size_t(i)].numChannels = SpeakerArr::getChannelCount(a);
        }
        allocateBuffers();
    }

    void allocateBuffers()
    {
        int total = 0;
        m_inScratch.clear();
        m_outScratch.clear();
        m_inPtrs.clear();
        m_outPtrs.clear();
        for (auto& b : m_inBuses) {
            m_inScratch.push_back(total);
            total += b.numChannels;
            m_inPtrs.emplace_back(size_t(std::max(b.numChannels, 1)), nullptr);
        }
        for (auto& b : m_outBuses) {
            m_outScratch.push_back(total);
            total += b.numChannels;
            m_outPtrs.emplace_back(size_t(std::max(b.numChannels, 1)), nullptr);
        }
        m_scratch.assign(size_t(std::max(total, 1)), std::vector<float>(size_t(m_maxBlock), 0.f));
    }

    void scanParams()
    {
        m_paramInfos.clear();
        if (!m_controller)
            return;
        const int32 n = m_controller->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            ParameterInfo info{};
            if (m_controller->getParameterInfo(i, info) != kResultOk)
                continue;
            PluginParamInfo p;
            p.id = info.id;
            p.name = QString::fromStdString(Steinberg::Vst::StringConvert::convert(info.title));
            p.min = 0.0;
            p.max = 1.0;
            p.def = info.defaultNormalizedValue;
            p.stepped = info.stepCount > 0;
            p.hidden = !(info.flags & ParameterInfo::kCanAutomate) || (info.flags & ParameterInfo::kIsReadOnly) ||
                       (info.flags & ParameterInfo::kIsHidden);
            m_paramInfos.push_back(p);
        }
        suspend();
        m_inParams.setMaxParameters(std::max<int32>(n, 16));
        m_outParams.setMaxParameters(std::max<int32>(n, 16));
        resume();
    }

    VST3::Hosting::Module::Ptr m_module;
    IPtr<IComponent> m_component;
    IPtr<IEditController> m_controller;
    FUnknownPtr<IAudioProcessor> m_processor;
    FUnknownPtr<IConnectionPoint> m_cpComponent;
    FUnknownPtr<IConnectionPoint> m_cpController;
    bool m_singleComponent = false;
    bool m_active = false;
    double m_sampleRate = 48000.0;
    int m_maxBlock = kMaxBlock;

    ComponentHandler m_handler;
    PlugFrame m_frame;
    IPlugView* m_view = nullptr;

    EventList m_events;
    ParameterChanges m_inParams;
    ParameterChanges m_outParams;
    SpscQueue<ParamChange, 1024> m_automated;

    std::vector<AudioBusBuffers> m_inBuses, m_outBuses;
    std::vector<int> m_inScratch, m_outScratch;
    std::vector<std::vector<float*>> m_inPtrs, m_outPtrs;
    std::vector<std::vector<float>> m_scratch;
    std::vector<PluginParamInfo> m_paramInfos;

    std::atomic<bool> m_restartRequested{false};
    std::atomic<bool> m_rescanParams{false};
};

} // namespace

QList<PluginDescriptor> vst3Scan(const QString& path)
{
    QList<PluginDescriptor> out;
    QString err;
    auto mod = loadModule(path, &err);
    if (!mod)
        return out;
    const auto& factory = mod->getFactory();
    const QString factoryVendor = QString::fromStdString(factory.info().vendor());
    for (const auto& ci : factory.classInfos()) {
        if (ci.category() != kVstAudioEffectClass)
            continue;
        PluginDescriptor d;
        d.format = PluginFormat::VST3;
        d.uid = QString::fromStdString(ci.ID().toString());
        d.path = path;
        d.name = QString::fromStdString(ci.name());
        d.vendor = ci.vendor().empty() ? factoryVendor : QString::fromStdString(ci.vendor());
        d.category = QString::fromStdString(ci.subCategoriesString());
        d.instrument = d.category.contains(QLatin1String("Instrument"), Qt::CaseInsensitive);
        out.push_back(d);
    }
    return out;
}

PluginPtr vst3Create(const PluginDescriptor& d, QString* error)
{
    auto inst = std::make_shared<Vst3Instance>(d);
    QString err;
    if (!inst->init(&err)) {
        if (error)
            *error = err;
        return nullptr;
    }
    return inst;
}

} // namespace guiro

#else

namespace guiro {
QList<PluginDescriptor> vst3Scan(const QString&) { return {}; }
PluginPtr vst3Create(const PluginDescriptor&, QString* e)
{
    if (e)
        *e = QStringLiteral("VST3 support disabled");
    return nullptr;
}
} // namespace guiro

#endif
