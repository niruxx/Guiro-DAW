#pragma once

#include "core/Types.h"

#include <QWidget>

class QPushButton;
class QLabel;

namespace guiro {

class Project;
class AudioEngine;
class Waveform;

// Per-channel sampler settings: sample, pitch, envelope and channel filter.
class ChannelSettings : public QWidget {
    Q_OBJECT
public:
    static void open(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent);
    ~ChannelSettings() override;

private:
    ChannelSettings(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent);
    void refresh();
    void loadSample(const QString& path);

    Project* m_project;
    AudioEngine* m_engine;
    ObjId m_id;
    Waveform* m_wave;
    QLabel* m_title;
    QPushButton* m_filterType;
};

} // namespace guiro
