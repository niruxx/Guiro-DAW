#pragma once

#include "core/Types.h"
#include "plugins/PluginInstance.h"

#include <QMainWindow>
#include <QTimer>
#include <map>

class QDockWidget;
class QLabel;
class QMenu;

namespace guiro {

class Project;
class AudioEngine;
class TopBar;
class Browser;
class ChannelRack;
class PianoRoll;
class Playlist;
class Mixer;
class PadsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

    bool openProject(const QString& path);
    void showPanel(const QString& name);   // browser, rack, pads, piano, mixer

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void buildMenus();
    QDockWidget* makeDock(const QString& title, const QString& objectName, QWidget* content, const QString& icon);
    void showHint(const QString& text);
    bool maybeSave();
    bool save();
    bool saveAs();
    void newProject();
    void openDialog();
    void exportAudio();
    void updateTitle();
    void addRecent(const QString& path);
    void rebuildRecent();
    void createAutomation(const QString& target);
    void addEffectToSelected(const PluginDescriptor& d);
    void idlePlugins();
    void drainRecording();
    void showAbout();
    void showShortcuts();

    Project* m_project;
    AudioEngine* m_engine;
    TopBar* m_top;
    Browser* m_browser;
    ChannelRack* m_rack;
    PianoRoll* m_pianoRoll;
    Playlist* m_playlist;
    Mixer* m_mixer;
    PadsPanel* m_pads;
    QDockWidget* m_dBrowser;
    class Workspace* m_workspace;
    QMenu* m_recentMenu = nullptr;
    QTimer m_idle;
    std::map<std::pair<ObjId, int>, std::pair<double, float>> m_pendingRec;
};

} // namespace guiro
