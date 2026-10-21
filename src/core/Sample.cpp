#include "core/Sample.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <samplerate.h>
#include <sndfile.h>

namespace guiro {

void Sample::buildOverview()
{
    const int64_t n = (frames + kOverviewStep - 1) / kOverviewStep;
    overviewMin.assign(size_t(n), 0.f);
    overviewMax.assign(size_t(n), 0.f);
    for (int64_t b = 0; b < n; ++b) {
        float lo = 0.f, hi = 0.f;
        const int64_t s = b * kOverviewStep;
        const int64_t e = std::min<int64_t>(frames, s + kOverviewStep);
        for (int64_t i = s; i < e; ++i) {
            const float v = 0.5f * (left[size_t(i)] + right[size_t(i)]);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        overviewMin[size_t(b)] = lo;
        overviewMax[size_t(b)] = hi;
    }
}

SampleCache& SampleCache::instance()
{
    static SampleCache c;
    return c;
}

void SampleCache::setTargetRate(double sr)
{
    std::lock_guard lk(m_mutex);
    if (sr != m_rate) {
        m_rate = sr;
        // Already-loaded samples stay at their old rate; voices compensate via sampleRate.
    }
}

SamplePtr SampleCache::find(const QString& path) const
{
    std::lock_guard lk(m_mutex);
    return m_cache.value(QFileInfo(path).absoluteFilePath());
}

bool SampleCache::isAudioFile(const QString& path)
{
    static const QStringList exts = {"wav", "flac", "ogg", "oga", "aif", "aiff", "mp3", "opus", "w64", "caf"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QStringList SampleCache::nameFilters()
{
    return {"*.wav", "*.flac", "*.ogg", "*.oga", "*.aif", "*.aiff", "*.mp3", "*.opus", "*.w64", "*.caf",
            "*.WAV", "*.FLAC", "*.OGG", "*.AIF", "*.AIFF", "*.MP3"};
}

SamplePtr SampleCache::load(const QString& rawPath, QString* error)
{
    const QString path = QFileInfo(rawPath).absoluteFilePath();
    double target;
    {
        std::lock_guard lk(m_mutex);
        if (auto it = m_cache.find(path); it != m_cache.end())
            return it.value();
        target = m_rate;
    }

    SF_INFO info{};
    SNDFILE* f = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
    if (!f) {
        if (error)
            *error = QString::fromUtf8(sf_strerror(nullptr));
        return nullptr;
    }
    if (info.frames <= 0 || info.channels <= 0 || info.frames > int64_t(48000) * 60 * 30) {
        sf_close(f);
        if (error)
            *error = QStringLiteral("Unsupported or empty audio file");
        return nullptr;
    }

    std::vector<float> interleaved(size_t(info.frames) * size_t(info.channels));
    const sf_count_t got = sf_readf_float(f, interleaved.data(), info.frames);
    sf_close(f);

    const int64_t frames = got;
    const int ch = info.channels;

    // Down-mix / up-mix to interleaved stereo first.
    std::vector<float> stereo(size_t(frames) * 2);
    for (int64_t i = 0; i < frames; ++i) {
        const float* src = &interleaved[size_t(i) * size_t(ch)];
        stereo[size_t(i) * 2] = src[0];
        stereo[size_t(i) * 2 + 1] = ch > 1 ? src[1] : src[0];
    }
    interleaved.clear();
    interleaved.shrink_to_fit();

    auto s = std::make_shared<Sample>();
    s->path = path;
    s->name = QFileInfo(path).completeBaseName();
    s->sourceRate = info.samplerate;
    s->sourceChannels = ch;

    std::vector<float> data;
    if (std::abs(double(info.samplerate) - target) > 0.5) {
        const double ratio = target / double(info.samplerate);
        const int64_t outFrames = int64_t(std::ceil(double(frames) * ratio)) + 16;
        data.resize(size_t(outFrames) * 2);
        SRC_DATA d{};
        d.data_in = stereo.data();
        d.input_frames = long(frames);
        d.data_out = data.data();
        d.output_frames = long(outFrames);
        d.src_ratio = ratio;
        if (src_simple(&d, SRC_SINC_MEDIUM_QUALITY, 2) == 0) {
            data.resize(size_t(d.output_frames_gen) * 2);
            s->sampleRate = target;
        } else {
            data = std::move(stereo);
            s->sampleRate = info.samplerate;
        }
    } else {
        data = std::move(stereo);
        s->sampleRate = info.samplerate;
    }

    s->frames = int64_t(data.size() / 2);
    s->left.resize(size_t(s->frames));
    s->right.resize(size_t(s->frames));
    for (int64_t i = 0; i < s->frames; ++i) {
        s->left[size_t(i)] = data[size_t(i) * 2];
        s->right[size_t(i)] = data[size_t(i) * 2 + 1];
    }
    s->buildOverview();

    std::lock_guard lk(m_mutex);
    if (!m_cache.contains(path))
        m_cache.insert(path, s);
    return m_cache.value(path);
}

} // namespace guiro
