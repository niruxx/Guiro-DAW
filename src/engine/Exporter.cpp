#include "engine/Exporter.h"
#include "engine/AudioEngine.h"

#include <QApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QtConcurrent>
#include <atomic>
#include <cmath>
#include <sndfile.h>
#include <vector>

namespace guiro {

namespace {

bool renderLoop(AudioEngine* engine, SNDFILE* f, int sr, const std::function<bool(double)>& progress)
{
    const Tick songEnd = engine->offlineSongEnd();
    constexpr int kChunk = 1024;
    std::vector<float> l(kChunk), r(kChunk), inter(kChunk * 2);
    const int64_t maxTail = int64_t(sr) * 4;
    int64_t tail = 0;
    int64_t silentRun = 0;
    while (true) {
        engine->offlineProcess(l.data(), r.data(), kChunk);
        for (int i = 0; i < kChunk; ++i) {
            inter[size_t(i) * 2] = l[size_t(i)];
            inter[size_t(i) * 2 + 1] = r[size_t(i)];
        }
        sf_writef_float(f, inter.data(), kChunk);
        const double pos = engine->offlineTick();
        double prog;
        if (pos < double(songEnd)) {
            prog = pos / double(songEnd) * 0.95;
        } else {
            // Render a tail so reverbs / releases ring out, stop on silence.
            tail += kChunk;
            float pk = 0.f;
            for (int i = 0; i < kChunk; ++i)
                pk = std::max(pk, std::max(std::abs(l[size_t(i)]), std::abs(r[size_t(i)])));
            silentRun = pk < 1e-4f ? silentRun + kChunk : 0;
            prog = 0.95 + 0.05 * double(tail) / double(maxTail);
            if (tail >= maxTail || silentRun >= sr / 4)
                return true;
        }
        if (progress && !progress(prog))
            return false;
    }
}

SNDFILE* openOut(AudioEngine* engine, const QString& path, int* sr, QString* error)
{
    const bool flac = QFileInfo(path).suffix().compare(QLatin1String("flac"), Qt::CaseInsensitive) == 0;
    SF_INFO info{};
    info.samplerate = int(engine->sampleRate());
    info.channels = 2;
    info.format = flac ? (SF_FORMAT_FLAC | SF_FORMAT_PCM_24) : (SF_FORMAT_WAV | SF_FORMAT_PCM_24);
    SNDFILE* f = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!f) {
        if (error)
            *error = QString::fromUtf8(sf_strerror(nullptr));
        return nullptr;
    }
    sf_command(f, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    *sr = info.samplerate;
    return f;
}

} // namespace

bool renderSong(AudioEngine* engine, const QString& path, const std::function<bool(double)>& progress, QString* error)
{
    int sr = 0;
    SNDFILE* f = openOut(engine, path, &sr, error);
    if (!f)
        return false;
    engine->beginOffline();
    const bool ok = renderLoop(engine, f, sr, progress);
    sf_close(f);
    engine->endOffline();
    if (!ok) {
        QFile::remove(path);
        if (error)
            *error = QObject::tr("Export cancelled");
    }
    return ok;
}

bool exportSong(AudioEngine* engine, const QString& path, QWidget* parent, QString* error)
{
    if (engine->isPlaying())
        engine->stop();
    QApplication::processEvents();

    std::atomic<double> progress{0.0};
    std::atomic<bool> cancel{false};
    QProgressDialog dlg(QObject::tr("Rendering song…"), QObject::tr("Cancel"), 0, 1000, parent);
    dlg.setWindowTitle(QObject::tr("Export"));
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setValue(0);

    bool ok = false;
    auto job = QtConcurrent::run([&] {
        ok = renderSong(engine, path, [&](double p) {
            progress = p;
            return !cancel.load();
        }, error);
    });
    while (!job.isFinished()) {
        dlg.setValue(int(progress.load() * 1000.0));
        if (dlg.wasCanceled())
            cancel = true;
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        QThread::msleep(15);
    }
    job.waitForFinished();
    dlg.setValue(1000);
    return ok;
}

} // namespace guiro
