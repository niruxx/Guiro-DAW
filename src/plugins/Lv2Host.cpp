#include "plugins/Lv2Host.h"

#ifdef GUIRO_HAVE_LV2

#include "core/Types.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/port-props/port-props.h>
#include <lv2/urid/urid.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace guiro {

namespace {

// ---------------------------------------------------------------- URID map

struct UridMap {
    std::mutex mutex;
    std::unordered_map<std::string, LV2_URID> ids;
    std::vector<std::string> uris{""};

    LV2_URID map(const char* uri)
    {
        std::lock_guard lk(mutex);
        auto it = ids.find(uri);
        if (it != ids.end())
            return it->second;
        const LV2_URID id = LV2_URID(uris.size());
        uris.emplace_back(uri);
        ids.emplace(uri, id);
        return id;
    }
    const char* unmap(LV2_URID id)
    {
        std::lock_guard lk(mutex);
        return id < uris.size() ? uris[id].c_str() : nullptr;
    }
};

UridMap& urids()
{
    static UridMap m;
    return m;
}

LV2_URID_Map s_map{nullptr, [](LV2_URID_Map_Handle, const char* uri) { return urids().map(uri); }};
LV2_URID_Unmap s_unmap{nullptr, [](LV2_URID_Unmap_Handle, LV2_URID id) { return urids().unmap(id); }};

struct World {
    LilvWorld* world = nullptr;
    LilvNode *audio, *control, *atom, *cv, *input, *output, *midiEvent, *toggled, *integer, *enumeration,
        *instrumentClass, *notOnGui, *connectionOptional;

    World()
    {
        world = lilv_world_new();
        lilv_world_load_all(world);
        audio = lilv_new_uri(world, LV2_CORE__AudioPort);
        control = lilv_new_uri(world, LV2_CORE__ControlPort);
        atom = lilv_new_uri(world, LV2_ATOM__AtomPort);
        cv = lilv_new_uri(world, LV2_CORE__CVPort);
        input = lilv_new_uri(world, LV2_CORE__InputPort);
        output = lilv_new_uri(world, LV2_CORE__OutputPort);
        midiEvent = lilv_new_uri(world, LV2_MIDI__MidiEvent);
        toggled = lilv_new_uri(world, LV2_CORE__toggled);
        integer = lilv_new_uri(world, LV2_CORE__integer);
        enumeration = lilv_new_uri(world, LV2_CORE__enumeration);
        instrumentClass = lilv_new_uri(world, LV2_CORE__InstrumentPlugin);
        notOnGui = lilv_new_uri(world, LV2_PORT_PROPS__notOnGUI);
        connectionOptional = lilv_new_uri(world, LV2_CORE__connectionOptional);
    }
};

World& world()
{
    static World w;
    return w;
}

bool featuresSupported(const LilvPlugin* p)
{
    static const char* supported[] = {LV2_URID__map, LV2_URID__unmap, LV2_OPTIONS__options,
                                      LV2_BUF_SIZE__boundedBlockLength, LV2_BUF_SIZE__powerOf2BlockLength,
                                      LV2_CORE__hardRTCapable, LV2_CORE__inPlaceBroken, LV2_CORE__isLive};
    LilvNodes* req = lilv_plugin_get_required_features(p);
    bool ok = true;
    LILV_FOREACH (nodes, it, req) {
        const char* uri = lilv_node_as_uri(lilv_nodes_get(req, it));
        bool found = false;
        for (const char* s : supported)
            if (std::strcmp(uri, s) == 0)
                found = true;
        if (!found)
            ok = false;
    }
    lilv_nodes_free(req);
    return ok;
}

// ---------------------------------------------------------------- instance

class Lv2Instance : public PluginInstance {
public:
    explicit Lv2Instance(PluginDescriptor d) : PluginInstance(std::move(d)) {}

    ~Lv2Instance() override
    {
        deactivate();
    }

    bool init(QString* error)
    {
        World& w = world();
        LilvNode* uri = lilv_new_uri(w.world, m_desc.uid.toUtf8().constData());
        m_plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(w.world), uri);
        lilv_node_free(uri);
        if (!m_plugin) {
            *error = QStringLiteral("LV2 plugin not found");
            return false;
        }
        if (!featuresSupported(m_plugin)) {
            *error = QStringLiteral("The plugin requires LV2 features this host does not provide");
            return false;
        }

        const uint32_t n = lilv_plugin_get_num_ports(m_plugin);
        std::vector<float> mins(n), maxs(n), defs(n);
        lilv_plugin_get_port_ranges_float(m_plugin, mins.data(), maxs.data(), defs.data());
        m_ports.resize(n);
        m_controls.assign(n, 0.f);
        for (uint32_t i = 0; i < n; ++i) {
            const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);
            Port& p = m_ports[i];
            p.index = i;
            p.isInput = lilv_port_is_a(m_plugin, port, w.input);
            if (lilv_port_is_a(m_plugin, port, w.audio))
                p.type = Port::Audio;
            else if (lilv_port_is_a(m_plugin, port, w.control))
                p.type = Port::Control;
            else if (lilv_port_is_a(m_plugin, port, w.atom))
                p.type = Port::Atom;
            else if (lilv_port_is_a(m_plugin, port, w.cv))
                p.type = Port::CV;
            if (p.type == Port::Control) {
                p.min = std::isnan(mins[i]) ? 0.f : mins[i];
                p.max = std::isnan(maxs[i]) ? 1.f : maxs[i];
                p.def = std::isnan(defs[i]) ? p.min : defs[i];
                m_controls[i] = p.def;
                p.stepped = lilv_port_has_property(m_plugin, port, w.toggled) ||
                            lilv_port_has_property(m_plugin, port, w.integer) ||
                            lilv_port_has_property(m_plugin, port, w.enumeration);
                p.hidden = lilv_port_has_property(m_plugin, port, w.notOnGui);
                LilvNode* name = lilv_port_get_name(m_plugin, port);
                p.name = QString::fromUtf8(lilv_node_as_string(name));
                lilv_node_free(name);
                p.symbol = QString::fromUtf8(lilv_node_as_string(lilv_port_get_symbol(m_plugin, port)));
                if (p.isInput)
                    m_paramPorts.push_back(i);
            }
            if (p.type == Port::Atom && p.isInput && lilv_port_supports_event(m_plugin, port, w.midiEvent))
                p.midi = true;
            if (p.type == Port::Audio)
                (p.isInput ? m_audioIns : m_audioOuts).push_back(i);
        }
        return true;
    }

    bool activate(double sampleRate, int maxBlock) override
    {
        if (m_instance)
            return true;
        m_maxBlock = std::max(maxBlock, kMaxBlock);
        m_minBlock = 1;
        m_sampleRateF = float(sampleRate);
        const LV2_URID atomInt = urids().map(LV2_ATOM__Int);
        const LV2_URID atomFloat = urids().map(LV2_ATOM__Float);
        m_options[0] = {LV2_OPTIONS_INSTANCE, 0, urids().map(LV2_BUF_SIZE__maxBlockLength), sizeof(int32_t), atomInt, &m_maxBlock};
        m_options[1] = {LV2_OPTIONS_INSTANCE, 0, urids().map(LV2_BUF_SIZE__minBlockLength), sizeof(int32_t), atomInt, &m_minBlock};
        m_options[2] = {LV2_OPTIONS_INSTANCE, 0, urids().map("http://lv2plug.in/ns/ext/parameters#sampleRate"), sizeof(float), atomFloat, &m_sampleRateF};
        m_options[3] = {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr};

        m_featMap = {LV2_URID__map, &s_map};
        m_featUnmap = {LV2_URID__unmap, &s_unmap};
        m_featOptions = {LV2_OPTIONS__options, m_options};
        m_featBounded = {LV2_BUF_SIZE__boundedBlockLength, nullptr};
        m_features[0] = &m_featMap;
        m_features[1] = &m_featUnmap;
        m_features[2] = &m_featOptions;
        m_features[3] = &m_featBounded;
        m_features[4] = nullptr;

        m_instance = lilv_plugin_instantiate(m_plugin, sampleRate, m_features);
        if (!m_instance)
            return false;

        m_audioBufs.assign(m_ports.size(), std::vector<float>(size_t(m_maxBlock), 0.f));
        m_atomBufs.assign(m_ports.size(), std::vector<uint64_t>(kAtomCapacity / 8, 0));
        for (auto& p : m_ports) {
            switch (p.type) {
            case Port::Control:
                lilv_instance_connect_port(m_instance, p.index, &m_controls[p.index]);
                break;
            case Port::Audio:
            case Port::CV:
                lilv_instance_connect_port(m_instance, p.index, m_audioBufs[p.index].data());
                break;
            case Port::Atom:
                lilv_instance_connect_port(m_instance, p.index, m_atomBufs[p.index].data());
                break;
            default:
                lilv_instance_connect_port(m_instance, p.index, nullptr);
                break;
            }
        }
        lilv_instance_activate(m_instance);
        return true;
    }

    void deactivate() override
    {
        if (!m_instance)
            return;
        suspend();
        lilv_instance_deactivate(m_instance);
        lilv_instance_free(m_instance);
        m_instance = nullptr;
        resume();
    }

    void process(const float* const* in, float** out, int frames, const PluginNote* notes, int numNotes,
                 const TransportInfo&) override
    {
        if (!m_instance || !enterProcess())
            return;
        ParamChange pc;
        while (m_paramQueue.pop(pc))
            if (pc.id < m_controls.size())
                m_controls[pc.id] = float(pc.value);
        for (int i = 0; i < m_numAuto; ++i)
            if (m_auto[i].id < m_controls.size())
                m_controls[m_auto[i].id] = float(m_auto[i].value);
        m_numAuto = 0;

        const LV2_URID seqType = urids().map(LV2_ATOM__Sequence);
        const LV2_URID chunkType = urids().map(LV2_ATOM__Chunk);
        const LV2_URID midiType = urids().map(LV2_MIDI__MidiEvent);

        for (auto& p : m_ports) {
            if (p.type == Port::Atom) {
                auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomBufs[p.index].data());
                if (p.isInput) {
                    seq->atom.type = seqType;
                    seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
                    seq->body.unit = 0;
                    seq->body.pad = 0;
                    if (p.midi) {
                        for (int i = 0; i < numNotes; ++i) {
                            struct {
                                LV2_Atom_Event ev;
                                uint8_t msg[4];
                            } e{};
                            e.ev.time.frames = notes[i].frame;
                            e.ev.body.type = midiType;
                            e.ev.body.size = 3;
                            e.msg[0] = notes[i].on ? 0x90 : 0x80;
                            e.msg[1] = notes[i].key;
                            e.msg[2] = notes[i].on ? uint8_t(std::clamp(int(notes[i].velocity * 127.f), 1, 127)) : 0;
                            lv2_atom_sequence_append_event(seq, kAtomCapacity - sizeof(LV2_Atom), &e.ev);
                        }
                    }
                } else {
                    seq->atom.type = chunkType;
                    seq->atom.size = kAtomCapacity - sizeof(LV2_Atom);
                }
            }
        }

        for (size_t i = 0; i < m_audioIns.size(); ++i) {
            float* dst = m_audioBufs[m_audioIns[i]].data();
            if (in && i < 2)
                std::memcpy(dst, in[i], sizeof(float) * size_t(frames));
            else if (in && m_audioIns.size() == 1)
                for (int f = 0; f < frames; ++f)
                    dst[f] = 0.5f * (in[0][f] + in[1][f]);
            else
                std::memset(dst, 0, sizeof(float) * size_t(frames));
        }

        lilv_instance_run(m_instance, uint32_t(frames));

        if (m_audioOuts.size() >= 2) {
            std::memcpy(out[0], m_audioBufs[m_audioOuts[0]].data(), sizeof(float) * size_t(frames));
            std::memcpy(out[1], m_audioBufs[m_audioOuts[1]].data(), sizeof(float) * size_t(frames));
        } else if (m_audioOuts.size() == 1) {
            std::memcpy(out[0], m_audioBufs[m_audioOuts[0]].data(), sizeof(float) * size_t(frames));
            std::memcpy(out[1], m_audioBufs[m_audioOuts[0]].data(), sizeof(float) * size_t(frames));
        }
        leaveProcess();
    }

    int paramCount() const override { return int(m_paramPorts.size()); }
    PluginParamInfo paramInfo(int i) const override
    {
        if (i < 0 || i >= int(m_paramPorts.size()))
            return {};
        const Port& p = m_ports[m_paramPorts[size_t(i)]];
        PluginParamInfo info;
        info.id = p.index;
        info.name = p.name;
        info.min = p.min;
        info.max = p.max;
        info.def = p.def;
        info.stepped = p.stepped;
        info.hidden = p.hidden;
        return info;
    }
    double paramValue(uint32_t id) const override { return id < m_controls.size() ? m_controls[id] : 0.0; }
    void setParamValue(uint32_t id, double value) override
    {
        if (!m_instance && id < m_controls.size())
            m_controls[id] = float(value);
        else
            m_paramQueue.push({id, value});
    }

    QByteArray saveState() override
    {
        QJsonObject o;
        for (uint32_t idx : m_paramPorts)
            o.insert(m_ports[idx].symbol, double(m_controls[idx]));
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    bool loadState(const QByteArray& data) override
    {
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        if (o.isEmpty())
            return false;
        for (uint32_t idx : m_paramPorts) {
            const Port& p = m_ports[idx];
            if (o.contains(p.symbol))
                setParamValue(idx, o.value(p.symbol).toDouble());
        }
        return true;
    }

private:
    static constexpr uint32_t kAtomCapacity = 16384;

    struct Port {
        enum Type { Unknown, Audio, Control, Atom, CV } type = Unknown;
        uint32_t index = 0;
        bool isInput = false;
        bool midi = false;
        bool stepped = false;
        bool hidden = false;
        float min = 0.f, max = 1.f, def = 0.f;
        QString name, symbol;
    };

    const LilvPlugin* m_plugin = nullptr;
    LilvInstance* m_instance = nullptr;
    std::vector<Port> m_ports;
    std::vector<float> m_controls;
    std::vector<uint32_t> m_paramPorts;
    std::vector<uint32_t> m_audioIns, m_audioOuts;
    std::vector<std::vector<float>> m_audioBufs;
    std::vector<std::vector<uint64_t>> m_atomBufs;

    int32_t m_maxBlock = kMaxBlock;
    int32_t m_minBlock = 1;
    float m_sampleRateF = 48000.f;
    LV2_Options_Option m_options[4]{};
    LV2_Feature m_featMap{}, m_featUnmap{}, m_featOptions{}, m_featBounded{};
    const LV2_Feature* m_features[5]{};
};

} // namespace

QList<PluginDescriptor> lv2ScanAll()
{
    QList<PluginDescriptor> out;
    World& w = world();
    const LilvPlugins* plugins = lilv_world_get_all_plugins(w.world);
    LILV_FOREACH (plugins, it, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, it);
        if (!featuresSupported(p))
            continue;
        const uint32_t nAudioOut = lilv_plugin_get_num_ports_of_class(p, w.audio, w.output, nullptr);
        if (nAudioOut == 0)
            continue;
        PluginDescriptor d;
        d.format = PluginFormat::LV2;
        d.uid = QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(p)));
        LilvNode* name = lilv_plugin_get_name(p);
        d.name = name ? QString::fromUtf8(lilv_node_as_string(name)) : d.uid;
        lilv_node_free(name);
        if (LilvNode* author = lilv_plugin_get_author_name(p)) {
            d.vendor = QString::fromUtf8(lilv_node_as_string(author));
            lilv_node_free(author);
        }
        const LilvPluginClass* cls = lilv_plugin_get_class(p);
        d.category = QString::fromUtf8(lilv_node_as_string(lilv_plugin_class_get_label(cls)));
        const uint32_t nAudioIn = lilv_plugin_get_num_ports_of_class(p, w.audio, w.input, nullptr);
        d.instrument = lilv_node_equals(lilv_plugin_class_get_uri(cls), w.instrumentClass) || nAudioIn == 0;
        out.push_back(d);
    }
    return out;
}

PluginPtr lv2Create(const PluginDescriptor& d, QString* error)
{
    auto inst = std::make_shared<Lv2Instance>(d);
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
QList<PluginDescriptor> lv2ScanAll() { return {}; }
PluginPtr lv2Create(const PluginDescriptor&, QString* e)
{
    if (e)
        *e = QStringLiteral("LV2 support disabled");
    return nullptr;
}
} // namespace guiro

#endif
