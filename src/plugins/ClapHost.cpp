#include "plugins/ClapHost.h"

#ifdef GUIRO_HAVE_CLAP

#include "core/Types.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <clap/clap.h>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <vector>

namespace guiro {

namespace {

const clap_plugin_entry_t* loadEntry(const QString& path, QString* error)
{
    // Modules stay loaded for the session: unloading plugin code is a common
    // source of crashes and saves nothing meaningful.
    static QHash<QString, const clap_plugin_entry_t*> s_entries;
    if (auto it = s_entries.find(path); it != s_entries.end())
        return it.value();
    void* h = dlopen(path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        if (error)
            *error = QString::fromLocal8Bit(dlerror());
        return nullptr;
    }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!entry || !clap_version_is_compatible(entry->clap_version) ||
        !entry->init(path.toLocal8Bit().constData())) {
        if (error)
            *error = QStringLiteral("Not a compatible CLAP module");
        return nullptr;
    }
    s_entries.insert(path, entry);
    return entry;
}

bool isMainThread()
{
    return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
}

class ClapInstance;
ClapInstance* self(const clap_host_t* h);

// ---------------------------------------------------------------- event lists

union ClapEvent {
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_param_value_t param;
};

struct InEvents {
    static constexpr int kMax = 2048;
    ClapEvent events[kMax];
    const clap_event_header_t* sorted[kMax];
    uint32_t count = 0;
    clap_input_events_t list{this, &InEvents::size, &InEvents::get};

    static uint32_t size(const clap_input_events_t* l) { return static_cast<const InEvents*>(l->ctx)->count; }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i)
    {
        auto* self = static_cast<const InEvents*>(l->ctx);
        return i < self->count ? self->sorted[i] : nullptr;
    }
    ClapEvent* add()
    {
        if (count >= kMax)
            return nullptr;
        ClapEvent* e = &events[count];
        sorted[count] = &e->header;
        ++count;
        return e;
    }
    void sort()
    {
        std::stable_sort(sorted, sorted + count,
                         [](const clap_event_header_t* a, const clap_event_header_t* b) { return a->time < b->time; });
    }
};

struct OutEvents {
    clap_output_events_t list{this, &OutEvents::tryPush};
    static bool tryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
};

// ---------------------------------------------------------------- instance

class ClapInstance : public PluginInstance {
public:
    explicit ClapInstance(PluginDescriptor d) : PluginInstance(std::move(d))
    {
        m_host.clap_version = CLAP_VERSION;
        m_host.host_data = this;
        m_host.name = "Guiro";
        m_host.vendor = "Guiro";
        m_host.url = "https://github.com/niruxxdaboi/Guiro-DAW";
        m_host.version = "0.1.0";
        m_host.get_extension = &ClapInstance::hostGetExtension;
        m_host.request_restart = [](const clap_host_t* h) { self(h)->m_restartRequested = true; };
        m_host.request_process = [](const clap_host_t*) {};
        m_host.request_callback = [](const clap_host_t* h) { self(h)->m_callbackRequested = true; };
    }

    ~ClapInstance() override
    {
        *m_alive = false;
        closeEditor();
        deactivate();
        for (auto& [id, t] : m_timers)
            delete t;
        for (auto& [fd, n] : m_fds)
            delete n;
        if (m_plugin)
            m_plugin->destroy(m_plugin);
    }

    bool init(QString* error)
    {
        const clap_plugin_entry_t* entry = loadEntry(m_desc.path, error);
        if (!entry)
            return false;
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (!factory) {
            *error = QStringLiteral("Module has no plugin factory");
            return false;
        }
        m_plugin = factory->create_plugin(factory, &m_host, m_desc.uid.toUtf8().constData());
        if (!m_plugin) {
            *error = QStringLiteral("Plugin could not be created");
            return false;
        }
        if (!m_plugin->init(m_plugin)) {
            m_plugin->destroy(m_plugin);
            m_plugin = nullptr;
            *error = QStringLiteral("Plugin failed to initialize");
            return false;
        }
        auto ext = [this](const char* id) { return m_plugin->get_extension(m_plugin, id); };
        m_params = static_cast<const clap_plugin_params_t*>(ext(CLAP_EXT_PARAMS));
        m_state = static_cast<const clap_plugin_state_t*>(ext(CLAP_EXT_STATE));
        m_gui = static_cast<const clap_plugin_gui_t*>(ext(CLAP_EXT_GUI));
        m_audioPorts = static_cast<const clap_plugin_audio_ports_t*>(ext(CLAP_EXT_AUDIO_PORTS));
        m_timerExt = static_cast<const clap_plugin_timer_support_t*>(ext(CLAP_EXT_TIMER_SUPPORT));
        m_fdExt = static_cast<const clap_plugin_posix_fd_support_t*>(ext(CLAP_EXT_POSIX_FD_SUPPORT));
        m_latencyExt = static_cast<const clap_plugin_latency_t*>(ext(CLAP_EXT_LATENCY));
        scanPorts();
        scanParams();
        return true;
    }

    // ---- PluginInstance
    bool activate(double sampleRate, int maxBlock) override
    {
        if (m_active)
            return true;
        m_sampleRate = sampleRate;
        m_maxBlock = maxBlock;
        scanPorts();
        for (auto& b : m_scratch)
            b.assign(size_t(std::max(maxBlock, kMaxBlock)), 0.f);
        m_active = m_plugin->activate(m_plugin, sampleRate, 1, uint32_t(std::max(maxBlock, kMaxBlock)));
        if (m_latencyExt)
            m_latency = int(m_latencyExt->get(m_plugin));
        return m_active;
    }

    void deactivate() override
    {
        if (!m_active)
            return;
        suspend();
        if (m_processing) {
            // stop_processing belongs on the audio thread; nothing processes this
            // instance any more, so we impersonate it for the thread checker.
            const bool was = isAudioThread();
            setAudioThread(true);
            m_plugin->stop_processing(m_plugin);
            setAudioThread(was);
            m_processing = false;
        }
        m_plugin->deactivate(m_plugin);
        m_active = false;
        resume();
    }

    void process(const float* const* in, float** out, int frames, const PluginNote* notes, int numNotes,
                 const TransportInfo& t) override
    {
        if (!m_active || !enterProcess())
            return;
        if (!m_processing) {
            m_processing = m_plugin->start_processing(m_plugin);
            if (!m_processing) {
                leaveProcess();
                return;
            }
        }

        m_in.count = 0;
        ParamChange pc;
        while (m_paramQueue.pop(pc))
            pushParam(pc.id, pc.value);
        for (int i = 0; i < m_numAuto; ++i)
            pushParam(m_auto[i].id, m_auto[i].value);
        m_numAuto = 0;
        for (int i = 0; i < numNotes; ++i) {
            ClapEvent* e = m_in.add();
            if (!e)
                break;
            auto& n = e->note;
            n.header.size = sizeof(clap_event_note_t);
            n.header.time = notes[i].frame;
            n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            n.header.type = notes[i].on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
            n.header.flags = 0;
            n.note_id = -1;
            n.port_index = 0;
            n.channel = 0;
            n.key = notes[i].key;
            n.velocity = notes[i].on ? double(notes[i].velocity) : 0.0;
        }
        m_in.sort();

        clap_event_transport_t tr{};
        tr.header.size = sizeof(tr);
        tr.header.type = CLAP_EVENT_TRANSPORT;
        tr.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                   (t.playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        tr.tempo = t.bpm;
        tr.song_pos_beats = clap_beattime(std::llround(t.beatPos * double(CLAP_BEATTIME_FACTOR)));
        tr.bar_number = int32_t(t.beatPos / 4.0);
        tr.bar_start = clap_beattime(std::llround(double(tr.bar_number) * 4.0 * double(CLAP_BEATTIME_FACTOR)));
        tr.tsig_num = 4;
        tr.tsig_denom = 4;

        // Audio buffers: main ports get the host buffers, any extra ports get scratch.
        float* inPtrs[2] = {const_cast<float*>(in ? in[0] : m_zero.data()), const_cast<float*>(in ? in[1] : m_zero.data())};
        float* outPtrs[2] = {out[0], out[1]};

        size_t scratchIdx = 0;
        auto setup = [&](std::vector<clap_audio_buffer_t>& bufs, std::vector<std::vector<float*>>& ptrs,
                         const std::vector<uint32_t>& chans, float** main) {
            for (size_t p = 0; p < chans.size(); ++p) {
                auto& pp = ptrs[p];
                for (uint32_t c = 0; c < chans[p]; ++c) {
                    if (p == 0 && c < 2)
                        pp[c] = main[c];
                    else if (p == 0 && chans[p] == 1)
                        pp[c] = main[0];
                    else
                        pp[c] = m_scratch[std::min(scratchIdx++, m_scratch.size() - 1)].data();
                }
                bufs[p].data32 = pp.data();
                bufs[p].data64 = nullptr;
                bufs[p].channel_count = chans[p];
                bufs[p].latency = 0;
                bufs[p].constant_mask = 0;
            }
        };
        setup(m_inBufs, m_inPtrs, m_inChans, inPtrs);
        setup(m_outBufs, m_outPtrs, m_outChans, outPtrs);

        clap_process_t proc{};
        proc.steady_time = m_steady;
        proc.frames_count = uint32_t(frames);
        proc.transport = &tr;
        proc.audio_inputs = m_inBufs.data();
        proc.audio_inputs_count = uint32_t(m_inBufs.size());
        proc.audio_outputs = m_outBufs.data();
        proc.audio_outputs_count = uint32_t(m_outBufs.size());
        proc.in_events = &m_in.list;
        proc.out_events = &m_out.list;
        m_plugin->process(m_plugin, &proc);
        m_steady += frames;

        if (!m_outChans.empty() && m_outChans[0] == 1)
            std::memcpy(out[1], out[0], sizeof(float) * size_t(frames));
        leaveProcess();
    }

    int paramCount() const override { return int(m_paramInfos.size()); }
    PluginParamInfo paramInfo(int index) const override
    {
        return index >= 0 && index < int(m_paramInfos.size()) ? m_paramInfos[size_t(index)] : PluginParamInfo{};
    }
    double paramValue(uint32_t id) const override
    {
        double v = 0.0;
        if (m_params)
            m_params->get_value(m_plugin, id, &v);
        return v;
    }
    QString paramText(uint32_t id, double value) const override
    {
        char buf[256] = {};
        if (m_params && m_params->value_to_text(m_plugin, id, value, buf, sizeof(buf)))
            return QString::fromUtf8(buf);
        return QString::number(value, 'f', 3);
    }
    void setParamValue(uint32_t id, double value) override
    {
        m_paramQueue.push({id, value});
        if (!m_active && m_params) {
            // Not processing: flush directly so the change is not lost.
            InEvents ev;
            ClapEvent* e = ev.add();
            fillParam(e, id, value);
            m_params->flush(m_plugin, &ev.list, &m_out.list);
        }
    }

    QByteArray saveState() override
    {
        QByteArray data;
        if (!m_state)
            return data;
        clap_ostream_t os{&data, [](const clap_ostream_t* s, const void* buf, uint64_t size) -> int64_t {
                              static_cast<QByteArray*>(s->ctx)->append(static_cast<const char*>(buf), qsizetype(size));
                              return int64_t(size);
                          }};
        m_state->save(m_plugin, &os);
        return data;
    }

    bool loadState(const QByteArray& data) override
    {
        if (!m_state || data.isEmpty())
            return false;
        struct Reader {
            const QByteArray* d;
            qsizetype pos;
        } r{&data, 0};
        clap_istream_t is{&r, [](const clap_istream_t* s, void* buf, uint64_t size) -> int64_t {
                              auto* rd = static_cast<Reader*>(s->ctx);
                              const qsizetype n = std::min<qsizetype>(qsizetype(size), rd->d->size() - rd->pos);
                              if (n <= 0)
                                  return 0;
                              std::memcpy(buf, rd->d->constData() + rd->pos, size_t(n));
                              rd->pos += n;
                              return n;
                          }};
        suspend();
        const bool ok = m_state->load(m_plugin, &is);
        resume();
        scanParams();
        return ok;
    }

    bool hasEditor() const override { return m_gui && m_gui->is_api_supported(m_plugin, CLAP_WINDOW_API_X11, false); }

    bool openEditor(QWindow* parent, QSize* size) override
    {
        if (!hasEditor() || m_editorOpen)
            return false;
        if (!m_gui->create(m_plugin, CLAP_WINDOW_API_X11, false))
            return false;
        m_gui->set_scale(m_plugin, 1.0);
        uint32_t w = 640, h = 400;
        m_gui->get_size(m_plugin, &w, &h);
        clap_window_t win{};
        win.api = CLAP_WINDOW_API_X11;
        win.x11 = clap_xwnd(parent->winId());
        if (!m_gui->set_parent(m_plugin, &win)) {
            m_gui->destroy(m_plugin);
            return false;
        }
        m_gui->show(m_plugin);
        m_editorOpen = true;
        *size = QSize(int(w), int(h));
        return true;
    }

    void closeEditor() override
    {
        if (!m_editorOpen)
            return;
        m_gui->hide(m_plugin);
        m_gui->destroy(m_plugin);
        m_editorOpen = false;
    }

    bool editorCanResize() const override { return m_editorOpen && m_gui->can_resize(m_plugin); }

    void editorResized(QSize s) override
    {
        if (!editorCanResize())
            return;
        uint32_t w = uint32_t(s.width()), h = uint32_t(s.height());
        m_gui->adjust_size(m_plugin, &w, &h);
        m_gui->set_size(m_plugin, w, h);
    }

    void idle() override
    {
        if (m_callbackRequested.exchange(false))
            m_plugin->on_main_thread(m_plugin);
        if (m_restartRequested.exchange(false) && m_active) {
            const double sr = m_sampleRate;
            const int mb = m_maxBlock;
            deactivate();
            activate(sr, mb);
        }
        if (m_rescanParams.exchange(false))
            scanParams();
    }

    // ---- host-side extension implementations
    static const void* hostGetExtension(const clap_host_t*, const char* id)
    {
        static const clap_host_log_t log{[](const clap_host_t*, clap_log_severity sev, const char* msg) {
            if (sev >= CLAP_LOG_ERROR)
                qWarning("[clap] %s", msg);
        }};
        static const clap_host_thread_check_t threads{[](const clap_host_t*) { return isMainThread(); },
                                                      [](const clap_host_t*) { return isAudioThread(); }};
        static const clap_host_params_t params{
            [](const clap_host_t* h, clap_param_rescan_flags) { self(h)->m_rescanParams = true; },
            [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
            [](const clap_host_t*) {}};
        static const clap_host_state_t state{[](const clap_host_t*) {}};
        static const clap_host_latency_t latency{[](const clap_host_t* h) { self(h)->m_restartRequested = true; }};
        static const clap_host_audio_ports_t audioPorts{[](const clap_host_t*, uint32_t) { return false; },
                                                        [](const clap_host_t* h, uint32_t) { self(h)->m_restartRequested = true; }};
        static const clap_host_note_ports_t notePorts{
            [](const clap_host_t*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; },
            [](const clap_host_t*, uint32_t) {}};
        static const clap_host_gui_t gui{
            [](const clap_host_t*) {},
            [](const clap_host_t* h, uint32_t w, uint32_t hh) {
                ClapInstance* s = self(h);
                std::weak_ptr<bool> alive = s->m_alive;
                QMetaObject::invokeMethod(QCoreApplication::instance(), [s, alive, w, hh] {
                    if (auto a = alive.lock(); a && *a && s->onEditorResizeRequest)
                        s->onEditorResizeRequest(QSize(int(w), int(hh)));
                }, Qt::QueuedConnection);
                return true;
            },
            [](const clap_host_t*) { return true; },
            [](const clap_host_t*) { return true; },
            [](const clap_host_t*, bool) {}};
        static const clap_host_timer_support_t timers{
            [](const clap_host_t* h, uint32_t period, clap_id* id) { return self(h)->registerTimer(period, id); },
            [](const clap_host_t* h, clap_id id) { return self(h)->unregisterTimer(id); }};
        static const clap_host_posix_fd_support_t fds{
            [](const clap_host_t* h, int fd, clap_posix_fd_flags_t f) { return self(h)->registerFd(fd, f); },
            [](const clap_host_t* h, int fd, clap_posix_fd_flags_t f) {
                self(h)->unregisterFd(fd);
                return self(h)->registerFd(fd, f);
            },
            [](const clap_host_t* h, int fd) { return self(h)->unregisterFd(fd); }};

        const std::string_view e(id);
        if (e == CLAP_EXT_LOG) return &log;
        if (e == CLAP_EXT_THREAD_CHECK) return &threads;
        if (e == CLAP_EXT_PARAMS) return &params;
        if (e == CLAP_EXT_STATE) return &state;
        if (e == CLAP_EXT_LATENCY) return &latency;
        if (e == CLAP_EXT_AUDIO_PORTS) return &audioPorts;
        if (e == CLAP_EXT_NOTE_PORTS) return &notePorts;
        if (e == CLAP_EXT_GUI) return &gui;
        if (e == CLAP_EXT_TIMER_SUPPORT) return &timers;
        if (e == CLAP_EXT_POSIX_FD_SUPPORT) return &fds;
        return nullptr;
    }

    bool registerTimer(uint32_t period, clap_id* id)
    {
        if (!m_timerExt)
            return false;
        const clap_id tid = m_nextTimer++;
        auto* t = new QTimer;
        t->setInterval(int(std::max<uint32_t>(period, 8)));
        QObject::connect(t, &QTimer::timeout, [this, tid] { m_timerExt->on_timer(m_plugin, tid); });
        t->start();
        m_timers[tid] = t;
        *id = tid;
        return true;
    }

    bool unregisterTimer(clap_id id)
    {
        auto it = m_timers.find(id);
        if (it == m_timers.end())
            return false;
        it->second->deleteLater();
        m_timers.erase(it);
        return true;
    }

    bool registerFd(int fd, clap_posix_fd_flags_t flags)
    {
        if (!m_fdExt || m_fds.count(fd))
            return false;
        auto* n = new QSocketNotifier(qintptr(fd), QSocketNotifier::Read);
        QObject::connect(n, &QSocketNotifier::activated, [this, fd] {
            m_fdExt->on_fd(m_plugin, fd, CLAP_POSIX_FD_READ);
        });
        m_fds[fd] = n;
        return true;
    }

    bool unregisterFd(int fd)
    {
        auto it = m_fds.find(fd);
        if (it == m_fds.end())
            return false;
        it->second->setEnabled(false);
        it->second->deleteLater();
        m_fds.erase(it);
        return true;
    }

    std::atomic<bool> m_restartRequested{false};
    std::atomic<bool> m_callbackRequested{false};
    std::atomic<bool> m_rescanParams{false};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

private:
    void fillParam(ClapEvent* e, uint32_t id, double value)
    {
        auto& p = e->param;
        p.header.size = sizeof(clap_event_param_value_t);
        p.header.time = 0;
        p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        p.header.type = CLAP_EVENT_PARAM_VALUE;
        p.header.flags = 0;
        p.param_id = id;
        p.cookie = nullptr;
        p.note_id = -1;
        p.port_index = -1;
        p.channel = -1;
        p.key = -1;
        p.value = value;
    }

    void pushParam(uint32_t id, double value)
    {
        if (ClapEvent* e = m_in.add())
            fillParam(e, id, value);
    }

    void scanPorts()
    {
        m_inChans.clear();
        m_outChans.clear();
        if (m_audioPorts) {
            for (int dir = 0; dir < 2; ++dir) {
                const bool isIn = dir == 0;
                const uint32_t n = m_audioPorts->count(m_plugin, isIn);
                for (uint32_t i = 0; i < n; ++i) {
                    clap_audio_port_info_t info{};
                    if (m_audioPorts->get(m_plugin, i, isIn, &info))
                        (isIn ? m_inChans : m_outChans).push_back(info.channel_count);
                }
            }
        } else {
            m_outChans = {2};
        }
        m_inBufs.assign(m_inChans.size(), {});
        m_outBufs.assign(m_outChans.size(), {});
        m_inPtrs.clear();
        m_outPtrs.clear();
        size_t extra = 0;
        for (size_t i = 0; i < m_inChans.size(); ++i) {
            m_inPtrs.emplace_back(m_inChans[i], nullptr);
            extra += m_inChans[i];
        }
        for (size_t i = 0; i < m_outChans.size(); ++i) {
            m_outPtrs.emplace_back(m_outChans[i], nullptr);
            extra += m_outChans[i];
        }
        m_scratch.assign(std::max<size_t>(extra, 2), std::vector<float>(size_t(std::max(m_maxBlock, kMaxBlock)), 0.f));
        m_zero.assign(size_t(std::max(m_maxBlock, kMaxBlock)), 0.f);
    }

    void scanParams()
    {
        m_paramInfos.clear();
        if (!m_params)
            return;
        const uint32_t n = m_params->count(m_plugin);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t info{};
            if (!m_params->get_info(m_plugin, i, &info))
                continue;
            PluginParamInfo p;
            p.id = info.id;
            p.name = QString::fromUtf8(info.name);
            p.min = info.min_value;
            p.max = info.max_value;
            p.def = info.default_value;
            p.stepped = info.flags & CLAP_PARAM_IS_STEPPED;
            p.hidden = (info.flags & CLAP_PARAM_IS_HIDDEN) || (info.flags & CLAP_PARAM_IS_READONLY);
            m_paramInfos.push_back(p);
        }
    }

    clap_host_t m_host{};
    const clap_plugin_t* m_plugin = nullptr;
    const clap_plugin_params_t* m_params = nullptr;
    const clap_plugin_state_t* m_state = nullptr;
    const clap_plugin_gui_t* m_gui = nullptr;
    const clap_plugin_audio_ports_t* m_audioPorts = nullptr;
    const clap_plugin_timer_support_t* m_timerExt = nullptr;
    const clap_plugin_posix_fd_support_t* m_fdExt = nullptr;
    const clap_plugin_latency_t* m_latencyExt = nullptr;

    std::vector<PluginParamInfo> m_paramInfos;
    bool m_active = false;
    bool m_processing = false;
    bool m_editorOpen = false;
    double m_sampleRate = 48000.0;
    int m_maxBlock = kMaxBlock;
    int64_t m_steady = 0;

    std::vector<uint32_t> m_inChans, m_outChans;
    std::vector<clap_audio_buffer_t> m_inBufs, m_outBufs;
    std::vector<std::vector<float*>> m_inPtrs, m_outPtrs;
    std::vector<std::vector<float>> m_scratch;
    std::vector<float> m_zero;

    InEvents m_in;
    OutEvents m_out;

    std::map<clap_id, QTimer*> m_timers;
    clap_id m_nextTimer = 1;
    std::map<int, QSocketNotifier*> m_fds;
};

ClapInstance* self(const clap_host_t* h)
{
    return static_cast<ClapInstance*>(h->host_data);
}

} // namespace

QList<PluginDescriptor> clapScan(const QString& path)
{
    QList<PluginDescriptor> out;
    QString err;
    const clap_plugin_entry_t* entry = loadEntry(path, &err);
    if (!entry)
        return out;
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory)
        return out;
    const uint32_t n = factory->get_plugin_count(factory);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, i);
        if (!d || !d->id)
            continue;
        PluginDescriptor pd;
        pd.format = PluginFormat::CLAP;
        pd.uid = QString::fromUtf8(d->id);
        pd.path = path;
        pd.name = QString::fromUtf8(d->name);
        pd.vendor = d->vendor ? QString::fromUtf8(d->vendor) : QString();
        QStringList features;
        for (const char* const* f = d->features; f && *f; ++f)
            features << QString::fromUtf8(*f);
        pd.instrument = features.contains(QStringLiteral(CLAP_PLUGIN_FEATURE_INSTRUMENT));
        pd.category = features.join(QLatin1Char('|'));
        out.push_back(pd);
    }
    return out;
}

PluginPtr clapCreate(const PluginDescriptor& d, QString* error)
{
    auto inst = std::make_shared<ClapInstance>(d);
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
QList<PluginDescriptor> clapScan(const QString&) { return {}; }
PluginPtr clapCreate(const PluginDescriptor&, QString* e)
{
    if (e)
        *e = QStringLiteral("CLAP support disabled");
    return nullptr;
}
} // namespace guiro

#endif
