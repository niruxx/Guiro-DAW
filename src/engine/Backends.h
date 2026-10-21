#pragma once

#include <QString>
#include <atomic>
#include <thread>
#include <vector>

typedef struct _jack_client jack_client_t;
typedef struct _jack_port jack_port_t;

namespace guiro {

class AudioEngine;
struct MidiMsg;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual bool open(AudioEngine* engine, QString* error) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual int bufferSize() const = 0;
    virtual QString name() const = 0;
    virtual bool isRunning() const = 0;
};

// JACK client - on modern distros this transparently runs on PipeWire.
class JackBackend : public AudioBackend {
public:
    ~JackBackend() override;
    bool open(AudioEngine* engine, QString* error) override;
    void close() override;
    double sampleRate() const override { return m_rate; }
    int bufferSize() const override { return m_buffer.load(); }
    QString name() const override;
    bool isRunning() const override { return m_client && !m_shutdown.load(); }

private:
    static int processCb(unsigned nframes, void* arg);
    static int bufferSizeCb(unsigned nframes, void* arg);
    static void shutdownCb(void* arg);

    AudioEngine* m_engine = nullptr;
    jack_client_t* m_client = nullptr;
    jack_port_t* m_outL = nullptr;
    jack_port_t* m_outR = nullptr;
    jack_port_t* m_midiIn = nullptr;
    double m_rate = 48000.0;
    std::atomic<int> m_buffer{256};
    std::atomic<bool> m_shutdown{false};
    std::vector<MidiMsg>* m_midiBuf = nullptr;
};

// Silent fallback that keeps the engine (and the UI transport) running when no
// audio server is reachable.
class DummyBackend : public AudioBackend {
public:
    ~DummyBackend() override;
    bool open(AudioEngine* engine, QString* error) override;
    void close() override;
    double sampleRate() const override { return 48000.0; }
    int bufferSize() const override { return 512; }
    QString name() const override { return QStringLiteral("Silent (no audio server)"); }
    bool isRunning() const override { return m_running.load(); }

private:
    AudioEngine* m_engine = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace guiro
