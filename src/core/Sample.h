#pragma once

#include <QString>
#include <QHash>
#include <memory>
#include <mutex>
#include <vector>

namespace guiro {

// Immutable stereo audio, resampled to the engine rate at load time.
struct Sample {
    QString path;
    QString name;
    std::vector<float> left;
    std::vector<float> right;
    int64_t frames = 0;
    double sampleRate = 48000.0;      // rate of the stored data (engine rate)
    double sourceRate = 48000.0;
    int sourceChannels = 2;

    // Min/max pairs for drawing, one per kOverviewStep frames (mono mix).
    static constexpr int kOverviewStep = 128;
    std::vector<float> overviewMin;
    std::vector<float> overviewMax;

    double seconds() const { return sampleRate > 0 ? double(frames) / sampleRate : 0.0; }
    void buildOverview();
};

using SamplePtr = std::shared_ptr<const Sample>;

// Samples are cached for the lifetime of the session. The audio thread keeps
// raw pointers to voice data, so a sample must never be freed while playing;
// the cache guarantees that.
class SampleCache {
public:
    static SampleCache& instance();

    SamplePtr load(const QString& path, QString* error = nullptr);
    SamplePtr find(const QString& path) const;
    void setTargetRate(double sr);
    double targetRate() const { return m_rate; }

    static bool isAudioFile(const QString& path);
    static QStringList nameFilters();

private:
    mutable std::mutex m_mutex;
    QHash<QString, SamplePtr> m_cache;
    double m_rate = 48000.0;
};

} // namespace guiro
