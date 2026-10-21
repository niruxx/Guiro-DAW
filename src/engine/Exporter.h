#pragma once

#include <QString>
#include <functional>

class QWidget;

namespace guiro {

class AudioEngine;

// Renders the whole song (song mode, loop ignored) to a WAV or FLAC file
// faster than realtime, with a modal progress dialog.
bool exportSong(AudioEngine* engine, const QString& path, QWidget* parent, QString* error);

// Headless variant used by `guiro --render`. `progress` receives 0..1 and may
// return false to cancel. Safe to call from any thread.
bool renderSong(AudioEngine* engine, const QString& path, const std::function<bool(double)>& progress, QString* error);

} // namespace guiro
