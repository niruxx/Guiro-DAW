#include "engine/Backends.h"
#include "engine/AudioEngine.h"

#include <chrono>
#include <cstring>
#include <string>
#include <jack/jack.h>
#include <jack/midiport.h>

namespace guiro {

// ---------------------------------------------------------------- JACK

JackBackend::~JackBackend()
{
    close();
}

QString JackBackend::name() const
{
    return QStringLiteral("JACK / PipeWire");
}

bool JackBackend::open(AudioEngine* engine, QString* error)
{
    m_engine = engine;
    jack_status_t status{};
    m_client = jack_client_open("Guiro", JackNoStartServer, &status);
    if (!m_client) {
        if (error)
            *error = QStringLiteral("Could not connect to a JACK/PipeWire server (status 0x%1)").arg(int(status), 0, 16);
        return false;
    }
    m_rate = jack_get_sample_rate(m_client);
    m_buffer = int(jack_get_buffer_size(m_client));
    m_midiBuf = new std::vector<MidiMsg>();
    m_midiBuf->reserve(1024);

    m_outL = jack_port_register(m_client, "out_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    m_outR = jack_port_register(m_client, "out_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    m_midiIn = jack_port_register(m_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    jack_set_process_callback(m_client, &JackBackend::processCb, this);
    jack_set_buffer_size_callback(m_client, &JackBackend::bufferSizeCb, this);
    jack_on_shutdown(m_client, &JackBackend::shutdownCb, this);

    if (jack_activate(m_client) != 0) {
        if (error)
            *error = QStringLiteral("Could not activate the JACK client");
        jack_client_close(m_client);
        m_client = nullptr;
        return false;
    }

    // Auto-connect to the default playback device and every hardware MIDI source.
    // Prefer hardware outputs; fall back to any other client's playback inputs
    // (e.g. a PipeWire virtual or null sink when no device is physical).
    const char** ports = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);
    if (!ports || !ports[0]) {
        if (ports)
            jack_free(ports);
        ports = jack_get_ports(m_client, "playback", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    }
    if (ports) {
        std::vector<const char*> targets;
        const std::string self = std::string(jack_get_client_name(m_client)) + ":";
        for (int i = 0; ports[i]; ++i)
            if (std::string(ports[i]).rfind(self, 0) != 0)
                targets.push_back(ports[i]);
        if (!targets.empty()) {
            jack_connect(m_client, jack_port_name(m_outL), targets[0]);
            jack_connect(m_client, jack_port_name(m_outR), targets.size() > 1 ? targets[1] : targets[0]);
        }
        jack_free(ports);
    }
    if (const char** ports = jack_get_ports(m_client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput)) {
        for (int i = 0; ports[i]; ++i)
            jack_connect(m_client, ports[i], jack_port_name(m_midiIn));
        jack_free(ports);
    }
    return true;
}

void JackBackend::close()
{
    if (m_client) {
        jack_deactivate(m_client);
        jack_client_close(m_client);
        m_client = nullptr;
    }
    delete m_midiBuf;
    m_midiBuf = nullptr;
}

int JackBackend::processCb(unsigned nframes, void* arg)
{
    auto* self = static_cast<JackBackend*>(arg);
    auto* L = static_cast<float*>(jack_port_get_buffer(self->m_outL, nframes));
    auto* R = static_cast<float*>(jack_port_get_buffer(self->m_outR, nframes));

    auto& midi = *self->m_midiBuf;
    midi.clear();
    if (void* mb = jack_port_get_buffer(self->m_midiIn, nframes)) {
        const uint32_t count = jack_midi_get_event_count(mb);
        for (uint32_t i = 0; i < count && midi.size() < midi.capacity(); ++i) {
            jack_midi_event_t ev;
            if (jack_midi_event_get(&ev, mb, i) != 0 || ev.size == 0 || ev.size > 3)
                continue;
            MidiMsg m{};
            m.frame = ev.time;
            m.size = uint8_t(ev.size);
            std::memcpy(m.data, ev.buffer, ev.size);
            midi.push_back(m);
        }
    }
    self->m_engine->process(L, R, int(nframes), midi.data(), int(midi.size()));
    return 0;
}

int JackBackend::bufferSizeCb(unsigned nframes, void* arg)
{
    static_cast<JackBackend*>(arg)->m_buffer = int(nframes);
    return 0;
}

void JackBackend::shutdownCb(void* arg)
{
    static_cast<JackBackend*>(arg)->m_shutdown = true;
}

// ---------------------------------------------------------------- Dummy

DummyBackend::~DummyBackend()
{
    close();
}

bool DummyBackend::open(AudioEngine* engine, QString*)
{
    m_engine = engine;
    m_running = true;
    m_thread = std::thread([this] {
        constexpr int kFrames = 512;
        std::vector<float> l(kFrames), r(kFrames);
        const auto period = std::chrono::microseconds(int64_t(kFrames) * 1000000 / 48000);
        auto next = std::chrono::steady_clock::now();
        while (m_running.load()) {
            m_engine->process(l.data(), r.data(), kFrames, nullptr, 0);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });
    return true;
}

void DummyBackend::close()
{
    if (m_running.exchange(false) && m_thread.joinable())
        m_thread.join();
}

} // namespace guiro
