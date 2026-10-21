#include "core/DefaultKit.h"
#include "engine/Dsp.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>
#include <functional>
#include <sndfile.h>
#include <vector>

namespace guiro {

namespace {

constexpr int kRate = 48000;
constexpr double kTwoPi = 6.283185307179586;

struct Rng {
    uint32_t s = 0x9E3779B9u;
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s) / 2147483648.f - 1.f;
    }
};

using Buf = std::vector<float>;   // interleaved stereo

Buf alloc(double seconds) { return Buf(size_t(seconds * kRate) * 2, 0.f); }
size_t frames(const Buf& b) { return b.size() / 2; }

void normalize(Buf& b, float target = 0.9f)
{
    float peak = 0.f;
    for (float v : b)
        peak = std::max(peak, std::abs(v));
    if (peak < 1e-6f)
        return;
    const float g = target / peak;
    for (float& v : b)
        v *= g;
    // 3 ms fade out to avoid clicks at the end.
    const size_t n = frames(b);
    const size_t fade = std::min<size_t>(n, kRate * 3 / 1000);
    for (size_t i = 0; i < fade; ++i) {
        const float f = float(i) / float(fade);
        b[(n - 1 - i) * 2] *= f;
        b[(n - 1 - i) * 2 + 1] *= f;
    }
}

void mono(Buf& b, size_t i, float v)
{
    b[i * 2] += v;
    b[i * 2 + 1] += v;
}

float metallic(double t)
{
    static const double f[6] = {263.0, 400.0, 421.0, 474.0, 587.0, 845.0};
    float s = 0.f;
    for (double x : f)
        s += std::fmod(t * x * 1.9, 1.0) < 0.5 ? 1.f : -1.f;
    return s / 6.f;
}

Buf kick()
{
    Buf b = alloc(0.6);
    Rng r;
    double ph = 0.0;
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const double f = 45.0 + 115.0 * std::exp(-t * 26.0);
        ph += kTwoPi * f / kRate;
        float v = float(std::sin(ph) * std::exp(-t * 6.0));
        v += r.next() * float(std::exp(-t * 500.0)) * 0.35f;
        mono(b, i, std::tanh(1.8f * v) / std::tanh(1.8f));
    }
    return b;
}

Buf snare()
{
    Buf b = alloc(0.35);
    Rng r;
    dsp::Svf hp;
    hp.setup(1800.f, 0.1f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        float tone = float(std::sin(kTwoPi * 185.0 * t) * std::exp(-t * 24.0) * 0.6 +
                           std::sin(kTwoPi * 330.0 * t) * std::exp(-t * 32.0) * 0.3);
        float n = hp.tick(0, r.next(), FilterType::HighPass) * float(std::exp(-t * 15.0)) * 0.8f;
        mono(b, i, tone + n);
    }
    return b;
}

Buf clap()
{
    Buf b = alloc(0.45);
    Rng r;
    dsp::Svf bp;
    bp.setup(1250.f, 0.35f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        double env = 0.0;
        for (double s : {0.0, 0.011, 0.023})
            if (t >= s)
                env += std::exp(-(t - s) * 170.0);
        if (t >= 0.031)
            env += std::exp(-(t - 0.031) * 14.0) * 0.8;
        const float n = bp.tick(0, r.next(), FilterType::BandPass);
        b[i * 2] += n * float(env);
        b[i * 2 + 1] += n * float(env) * 0.92f;
    }
    return b;
}

Buf hat(double decay, double len)
{
    Buf b = alloc(len);
    Rng r;
    dsp::Svf hp;
    hp.setup(7200.f, 0.2f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const float src = metallic(t) * 0.7f + r.next() * 0.45f;
        mono(b, i, hp.tick(0, src, FilterType::HighPass) * float(std::exp(-t * decay)));
    }
    return b;
}

Buf rim()
{
    Buf b = alloc(0.09);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        mono(b, i, float(std::sin(kTwoPi * 1700.0 * t) * std::exp(-t * 90.0) +
                         std::sin(kTwoPi * 480.0 * t) * std::exp(-t * 60.0) * 0.6));
    }
    return b;
}

Buf shaker()
{
    Buf b = alloc(0.2);
    Rng r;
    dsp::Svf hp;
    hp.setup(5200.f, 0.3f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const double env = std::min(1.0, t / 0.015) * std::exp(-std::max(0.0, t - 0.015) * 26.0);
        const float n = hp.tick(0, r.next(), FilterType::HighPass) * float(env);
        b[i * 2] += n;
        b[i * 2 + 1] += n * 0.85f;
    }
    return b;
}

Buf cowbell()
{
    Buf b = alloc(0.4);
    dsp::Svf bp;
    bp.setup(820.f, 0.5f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const float sq = (std::fmod(t * 540.0, 1.0) < 0.5 ? 0.5f : -0.5f) + (std::fmod(t * 800.0, 1.0) < 0.5 ? 0.5f : -0.5f);
        mono(b, i, bp.tick(0, sq, FilterType::BandPass) * float(std::exp(-t * 11.0)));
    }
    return b;
}

Buf tom(double base, double sweep)
{
    Buf b = alloc(0.55);
    double ph = 0.0;
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        ph += kTwoPi * (base + sweep * std::exp(-t * 18.0)) / kRate;
        mono(b, i, float(std::sin(ph) * std::exp(-t * 7.0)));
    }
    return b;
}

Buf crash()
{
    Buf b = alloc(2.2);
    Rng r;
    dsp::Svf hp;
    hp.setup(4800.f, 0.15f, kRate);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const float src = metallic(t * 1.31) * 0.5f + r.next() * 0.7f;
        const float v = hp.tick(0, src, FilterType::HighPass) * float(std::exp(-t * 2.1));
        b[i * 2] += v;
        b[i * 2 + 1] += hp.tick(1, src, FilterType::HighPass) * float(std::exp(-t * 2.0));
    }
    return b;
}

Buf perc()
{
    Buf b = alloc(0.3);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        const double mod = 2.2 * std::sin(kTwoPi * 910.0 * t) * std::exp(-t * 30.0);
        mono(b, i, float(std::sin(kTwoPi * 610.0 * t + mod) * std::exp(-t * 17.0)));
    }
    return b;
}

Buf bass808()
{
    Buf b = alloc(1.6);
    double ph = 0.0;
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        ph += kTwoPi * (65.406 + 38.0 * std::exp(-t * 38.0)) / kRate;
        const float v = float(std::sin(ph) * std::exp(-t * 1.5));
        mono(b, i, std::tanh(2.2f * v));
    }
    return b;
}

Buf pluck()
{
    Buf b = alloc(1.4);
    Rng r;
    const double f = 261.626;
    const int n = int(kRate / f);
    std::vector<float> line(static_cast<size_t>(n));
    for (auto& v : line)
        v = r.next();
    // Pre-filter the excitation for a softer attack.
    for (int k = 0; k < 2; ++k)
        for (int i = 1; i < n; ++i)
            line[size_t(i)] = 0.5f * (line[size_t(i)] + line[size_t(i - 1)]);
    size_t idx = 0;
    float prev = 0.f;
    for (size_t i = 0; i < frames(b); ++i) {
        const float cur = line[idx];
        const float next = 0.4985f * (cur + prev) + 0.0f;
        prev = cur;
        line[idx] = next * 1.0f;
        idx = (idx + 1) % size_t(n);
        const double t = double(i) / kRate;
        b[i * 2] += cur * float(std::exp(-t * 1.2));
        b[i * 2 + 1] += cur * float(std::exp(-t * 1.25));
    }
    return b;
}

Buf chord(const std::vector<double>& freqs, double len, double attack, double decayRate, float cutoffStart,
          float cutoffEnd, double cutoffRate)
{
    Buf b = alloc(len);
    dsp::Svf lp;
    std::vector<double> ph(freqs.size() * 2, 0.0);
    for (size_t i = 0; i < frames(b); ++i) {
        const double t = double(i) / kRate;
        float l = 0.f, r = 0.f;
        for (size_t k = 0; k < freqs.size(); ++k) {
            const double d = 1.0 + 0.0035 * double(k % 2 ? 1 : -1);
            ph[k * 2] = std::fmod(ph[k * 2] + freqs[k] * d / kRate, 1.0);
            ph[k * 2 + 1] = std::fmod(ph[k * 2 + 1] + freqs[k] / d / kRate, 1.0);
            l += float(ph[k * 2] * 2.0 - 1.0);
            r += float(ph[k * 2 + 1] * 2.0 - 1.0);
        }
        const float cut = cutoffEnd + (cutoffStart - cutoffEnd) * float(std::exp(-t * cutoffRate));
        lp.setup(cut, 0.25f, kRate);
        double env = attack > 0 ? std::min(1.0, t / attack) : 1.0;
        env *= std::exp(-t * decayRate);
        const double fadeStart = len - 0.25;
        if (t > fadeStart)
            env *= std::max(0.0, 1.0 - (t - fadeStart) / 0.25);
        b[i * 2] += lp.tick(0, l, FilterType::LowPass) * float(env);
        b[i * 2 + 1] += lp.tick(1, r, FilterType::LowPass) * float(env);
    }
    return b;
}

bool writeWav(const QString& path, const Buf& b)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    SF_INFO info{};
    info.samplerate = kRate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* f = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!f)
        return false;
    sf_writef_float(f, b.data(), sf_count_t(frames(b)));
    sf_close(f);
    return true;
}

} // namespace

QString DefaultKit::packsDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/Guiro/Packs");
}

std::map<QString, QString> DefaultKit::ensure()
{
    struct Def {
        const char* name;
        const char* folder;
        std::function<Buf()> make;
    };
    const double C3 = 130.813, Eb3 = 155.563, G3 = 195.998, Bb3 = 233.082;
    const double C4 = 261.626, Eb4 = 311.127, G4 = 391.995, C5 = 523.251;
    const std::vector<Def> defs = {
        {"Kick", "Drums", kick},
        {"Snare", "Drums", snare},
        {"Clap", "Drums", clap},
        {"Rim", "Drums", rim},
        {"Hat Closed", "Drums", [] { return hat(55.0, 0.14); }},
        {"Hat Open", "Drums", [] { return hat(6.5, 0.7); }},
        {"Shaker", "Drums", shaker},
        {"Cowbell", "Drums", cowbell},
        {"Tom Low", "Drums", [] { return tom(98.0, 70.0); }},
        {"Tom High", "Drums", [] { return tom(165.0, 90.0); }},
        {"Crash", "Drums", crash},
        {"Perc", "Drums", perc},
        {"808", "Bass", bass808},
        {"Pluck", "Synth", pluck},
        {"Stab", "Synth", [=] { return chord({C4, Eb4, G4, C5}, 0.9, 0.0, 3.5, 7000.f, 500.f, 9.0); }},
        {"Pad", "Synth", [=] { return chord({C3, Eb3, G3, Bb3}, 3.2, 0.45, 0.25, 1800.f, 1400.f, 0.5); }},
    };

    std::map<QString, QString> out;
    const QString root = packsDir() + QStringLiteral("/Guiro Basics/");
    for (const auto& d : defs) {
        const QString path = root + QString::fromLatin1(d.folder) + QLatin1Char('/') + QString::fromLatin1(d.name) + QStringLiteral(".wav");
        if (!QFileInfo::exists(path)) {
            Buf b = d.make();
            normalize(b);
            writeWav(path, b);
        }
        out[QString::fromLatin1(d.name)] = path;
    }
    return out;
}

} // namespace guiro
