#pragma once

#include "core/Types.h"
#include "plugins/PluginInstance.h"

#include <QObject>
#include <QTimer>

namespace guiro {

class Project;
class AudioEngine;

// Global UI coordination: a shared animation clock and cross-panel requests,
// so widgets do not need to know about the main window.
class UiHub : public QObject {
    Q_OBJECT
public:
    static UiHub* instance();
    void init(Project* project, AudioEngine* engine);

    Project* project() const { return m_project; }
    AudioEngine* engine() const { return m_engine; }

    // Clipboard-ish "brush" state shared between pickers and editors.
    Tick lastNoteLength = kStepTicks;

signals:
    void frame();
    void automationRequested(const QString& target);
    void pluginWindowRequested(PluginPtr plugin, const QString& title);
    void channelSettingsRequested(ObjId channel);
    void pianoRollRequested(ObjId channel);
    void padsRequested(ObjId channel);
    void statusMessage(const QString& message, int timeoutMs = 3000);
    void paramEdited();

private:
    UiHub() = default;
    Project* m_project = nullptr;
    AudioEngine* m_engine = nullptr;
    QTimer m_timer;
};

} // namespace guiro
