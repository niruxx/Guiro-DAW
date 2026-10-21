#include "ui/MainWindow.h"
#include "core/Project.h"
#include "core/ProjectIO.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "plugins/PluginManager.h"
#include "ui/Browser.h"
#include "ui/ChannelRack.h"
#include "ui/ChannelSettings.h"
#include "ui/DockTitle.h"
#include "ui/Icons.h"
#include "ui/Mixer.h"
#include "ui/PadsPanel.h"
#include "ui/PianoRoll.h"
#include "ui/Playlist.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"
#include "ui/UiHub.h"
#include "ui/WineBridge.h"
#include "ui/Workspace.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QMdiSubWindow>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QInputDialog>
#include <QLineEdit>
#include <QVBoxLayout>
#include <functional>

namespace guiro {

MainWindow::MainWindow(Project* project, AudioEngine* engine, QWidget* parent)
    : QMainWindow(parent), m_project(project), m_engine(engine)
{
    setWindowIcon(QIcon(QStringLiteral(":/guiro.svg")));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    resize(1600, 960);

    m_top = new TopBar(project, engine, this);
    setMenuWidget(m_top);

    m_browser = new Browser(project, engine, this);
    m_playlist = new Playlist(project, engine, this);
    m_rack = new ChannelRack(project, engine, this);
    m_pianoRoll = new PianoRoll(project, engine, this);
    m_mixer = new Mixer(project, engine, this);
    m_pads = new PadsPanel(project, engine, this);
    for (QWidget* w : std::initializer_list<QWidget*>{m_playlist, m_browser, m_rack, m_pads, m_pianoRoll, m_mixer}) {
        w->setAutoFillBackground(true);
        QPalette pal = w->palette();
        pal.setColor(QPalette::Window, theme::bg1);
        w->setPalette(pal);
    }

    // FL-style workspace: the browser is docked on the left, everything else is a
    // window floating in the workspace and toggled with the familiar F-keys.
    m_workspace = new Workspace(this);
    setCentralWidget(m_workspace);
    QAction* aPlaylist = m_workspace->addPane("playlist", tr("Playlist"), "playlist", m_playlist, QKeySequence(Qt::Key_F5));
    QAction* aRack = m_workspace->addPane("rack", tr("Channel rack"), "rack", m_rack, QKeySequence(Qt::Key_F6));
    QAction* aPiano = m_workspace->addPane("piano", tr("Piano roll"), "piano", m_pianoRoll, QKeySequence(Qt::Key_F7));
    QAction* aPads = m_workspace->addPane("pads", tr("Sound pads"), "pads", m_pads, QKeySequence(Qt::CTRL | Qt::Key_F8));
    QAction* aMixer = m_workspace->addPane("mixer", tr("Mixer"), "mixer", m_mixer, QKeySequence(Qt::Key_F9));

    m_dBrowser = makeDock(tr("Browser"), "browser", m_browser, "browser");
    addDockWidget(Qt::LeftDockWidgetArea, m_dBrowser);
    resizeDocks({m_dBrowser}, {250}, Qt::Horizontal);
    QAction* aBrowser = m_dBrowser->toggleViewAction();
    aBrowser->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F8));
    aBrowser->setToolTip(tr("Browser (Alt+F8)"));

    m_top->addPanelToggle(aPlaylist, "playlist");
    m_top->addPanelToggle(aRack, "rack");
    m_top->addPanelToggle(aPiano, "piano");
    m_top->addPanelToggle(aMixer, "mixer");
    m_top->addPanelToggle(aPads, "pads");
    m_top->addPanelToggle(aBrowser, "browser");

    buildMenus();

    if (!engine->backendError().isEmpty())
        showHint(engine->backendError() + tr(" — running silently."));
    connect(UiHub::instance(), &UiHub::statusMessage, this, [this](const QString& m, int) { showHint(m); });
    qApp->installEventFilter(this);

    // Cross-panel requests
    auto* hub = UiHub::instance();
    connect(hub, &UiHub::automationRequested, this, &MainWindow::createAutomation);
    connect(hub, &UiHub::pluginWindowRequested, this,
            [this](PluginPtr p, const QString& title) { PluginWindow::open(m_project, p, title, this); });
    connect(hub, &UiHub::channelSettingsRequested, this, [this](ObjId id) {
        Channel* c = m_project->channel(id);
        if (c && c->kind == ChannelKind::Instrument && c->plugin)
            PluginWindow::open(m_project, c->plugin, c->name, this);
        else
            ChannelSettings::open(m_project, m_engine, id, this);
    });
    connect(hub, &UiHub::pianoRollRequested, this, [this](ObjId id) {
        if (id)
            m_project->setSelectedChannel(id);
        m_pianoRoll->showChannel(m_project->selectedChannel());
        m_workspace->showPane("piano");
    });
    connect(hub, &UiHub::padsRequested, this, [this](ObjId) { m_workspace->showPane("pads"); });

    connect(m_browser, &Browser::sampleActivated, this, [this](const QString& path) {
        if (SamplePtr s = SampleCache::instance().load(path))
            m_project->setSelectedChannel(m_project->addSampler(s)->id);
    });
    connect(m_browser, &Browser::pluginActivated, this, [this](const PluginDescriptor& d) {
        if (d.instrument)
            addPluginChannel(m_project, d, this);
        else
            addEffectToSelected(d);
    });

    connect(project, &Project::modifiedChanged, this, &MainWindow::updateTitle);
    connect(project, &Project::projectReset, this, &MainWindow::updateTitle);
    connect(project, &Project::selectedChannelChanged, this, [this](ObjId id) { m_engine->setMidiTarget(id); });
    m_engine->setMidiTarget(project->selectedChannel());

    m_idle.setInterval(33);
    connect(&m_idle, &QTimer::timeout, this, &MainWindow::idlePlugins);
    connect(&m_idle, &QTimer::timeout, this, &MainWindow::drainRecording);
    m_idle.start();

    QSettings s;
    if (s.contains("window/geometry"))
        restoreGeometry(s.value("window/geometry").toByteArray());
    if (s.contains("window/state2"))
        restoreState(s.value("window/state2").toByteArray());
    m_workspace->restoreLayout(s);
    updateTitle();
}

QDockWidget* MainWindow::makeDock(const QString& title, const QString& objectName, QWidget* content, const QString& icon)
{
    auto* d = new QDockWidget(title, this);
    d->setObjectName(objectName);
    d->setTitleBarWidget(new DockTitle(d, icon));
    d->setWidget(content);
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    return d;
}

void MainWindow::showHint(const QString& text)
{
    m_top->setHint(text);
}

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    // FL-style hint bar: whatever the mouse is over explains itself at the top-left,
    // instead of popping up tooltips.
    if (e->type() == QEvent::Enter) {
        for (auto* w = qobject_cast<QWidget*>(o); w && w != this; w = w->parentWidget()) {
            if (!w->toolTip().isEmpty()) {
                showHint(QString(w->toolTip()).replace('\n', QStringLiteral(" · ")));
                break;
            }
        }
    } else if (e->type() == QEvent::ToolTip && qobject_cast<QWidget*>(o) && static_cast<QWidget*>(o)->window() == this) {
        return true;
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::buildMenus()
{
    QMenuBar* mb = m_top->menuBar();

    QMenu* file = mb->addMenu(tr("&FILE"));
    file->addAction(icons::icon("new"), tr("&New project"), QKeySequence::New, this, &MainWindow::newProject);
    file->addAction(icons::icon("open"), tr("&Open…"), QKeySequence::Open, this, &MainWindow::openDialog);
    m_recentMenu = file->addMenu(tr("Open &recent"));
    file->addAction(icons::icon("song"), tr("Open demo project"), this, [this] {
        if (!maybeSave())
            return;
        m_engine->stop();
        PluginWindow::closeAll();
        m_project->createDefault();
    });
    rebuildRecent();
    file->addSeparator();
    file->addAction(icons::icon("save"), tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save &as…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(icons::icon("export"), tr("&Export song (WAV / FLAC)…"), QKeySequence(Qt::CTRL | Qt::Key_R), this,
                    &MainWindow::exportAudio);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = mb->addMenu(tr("&EDIT"));
    edit->addAction(tr("Panic — all notes off"), QKeySequence(Qt::CTRL | Qt::Key_Period), this, [this] { m_engine->panic(); });
    edit->addAction(tr("Clear loop region"), this, [this] {
        m_project->loopStart = m_project->loopEnd = 0;
        m_project->notifyPlaylist();
    });

    QMenu* add = mb->addMenu(tr("&ADD"));
    add->addAction(icons::icon("wave"), tr("Sampler channel…"), this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Load samples"), QString(),
                                                                tr("Audio files (%1)").arg(SampleCache::nameFilters().join(' ')));
        for (const QString& f : files)
            if (SamplePtr s = SampleCache::instance().load(f))
                m_project->addSampler(s);
        m_workspace->showPane("rack");
    });
    add->addAction(icons::icon("pads"), tr("Pad bank"), this, [this] {
        Channel* c = m_project->addPads();
        m_project->setSelectedChannel(c->id);
        emit UiHub::instance()->padsRequested(c->id);
    });
    QMenu* inst = add->addMenu(icons::icon("instrument"), tr("Instrument plugin"));
    connect(inst, &QMenu::aboutToShow, this, [this, inst] {
        inst->clear();
        int n = 0;
        for (const auto& d : PluginManager::instance().plugins())
            if (d.instrument) {
                ++n;
                inst->addAction(QStringLiteral("%1  ·  %2").arg(d.name, formatName(d.format)), this,
                                [this, d] { addPluginChannel(m_project, d, this); });
            }
        if (!n)
            inst->addAction(tr("No instruments found"))->setEnabled(false);
    });

    QMenu* pats = mb->addMenu(tr("&PATTERNS"));
    pats->addAction(icons::icon("plus"), tr("New pattern"), QKeySequence(Qt::Key_F4), this,
                    [this] { m_project->setCurrentPattern(m_project->addPattern()->id); });
    pats->addAction(tr("Clone pattern"), QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_C), this, [this] {
        if (ObjId id = m_project->clonePattern(m_project->currentPatternId()))
            m_project->setCurrentPattern(id);
    });
    pats->addAction(icons::icon("edit"), tr("Rename pattern…"), QKeySequence(Qt::Key_F2), this, [this] {
        Pattern* p = m_project->currentPattern();
        if (!p)
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename pattern"), tr("Name"), QLineEdit::Normal, p->name, &ok);
        if (ok && !n.isEmpty()) {
            p->name = n;
            m_project->notifyPatterns();
        }
    });
    pats->addAction(icons::icon("trash"), tr("Delete pattern"), this,
                    [this] { m_project->removePattern(m_project->currentPatternId()); });

    QMenu* view = mb->addMenu(tr("&VIEW"));
    for (const char* key : {"playlist", "rack", "piano", "mixer", "pads"})
        view->addAction(m_workspace->action(QString::fromLatin1(key)));
    view->addAction(m_dBrowser->toggleViewAction());
    view->addSeparator();
    view->addAction(tr("Close all windows"), QKeySequence(Qt::Key_F12), this, [this] { m_workspace->closeAll(); });
    view->addAction(tr("Reset window layout"), this, [this] {
        m_dBrowser->show();
        m_workspace->resetLayout();
    });

    QMenu* options = mb->addMenu(tr("&OPTIONS"));
    QAction* metro = options->addAction(icons::icon("metronome"), tr("Metronome"), QKeySequence(Qt::CTRL | Qt::Key_M), this,
                                        [this] { m_engine->setMetronome(!m_engine->metronome()); });
    (void)metro;
    options->addAction(tr("Audio settings…"), this, [this] {
        QMessageBox::information(this, tr("Audio settings"),
                                 tr("<b>Backend:</b> %1<br><b>Sample rate:</b> %2 Hz<br><b>Buffer:</b> %3 frames<br><br>"
                                    "Guiro uses the JACK API, which runs on PipeWire. Change the sample rate and buffer size "
                                    "system-wide, e.g. <code>pw-metadata -n settings 0 clock.force-quantum 256</code>.")
                                     .arg(m_engine->backendName())
                                     .arg(int(m_engine->sampleRate()))
                                     .arg(m_engine->bufferSize()));
    });
    options->addSeparator();
    options->addAction(icons::icon("plugin"), tr("Windows plugins (Wine)…"), this, [this] { (new WineBridgeDialog(this))->show(); });
    options->addAction(tr("Plugin search paths…"), this, [this] {
        QString text;
        for (auto f : {PluginFormat::VST2, PluginFormat::VST3, PluginFormat::CLAP, PluginFormat::LV2})
            text += QStringLiteral("<b>%1</b><br>%2<br><br>").arg(formatName(f), PluginManager::searchPaths(f).join("<br>"));
        QMessageBox::information(this, tr("Plugin search paths"), text);
    });

    QMenu* tools = mb->addMenu(tr("&TOOLS"));
    tools->addAction(icons::icon("refresh"), tr("Rescan plugins"), this, [] { PluginManager::instance().rescan(); });
    tools->addAction(icons::icon("export"), tr("Export song…"), this, &MainWindow::exportAudio);

    QMenu* help = mb->addMenu(tr("&HELP"));
    help->addAction(tr("Keyboard shortcuts"), this, &MainWindow::showShortcuts);
    help->addAction(tr("About Guiro"), this, &MainWindow::showAbout);

    // Transport keys (no menu entry, like FL).
    auto key = [this](const QKeySequence& ks, std::function<void()> fn) {
        auto* a = new QAction(this);
        a->setShortcut(ks);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    };
    key(QKeySequence(Qt::Key_Space), [this] { m_engine->togglePlay(); });
    key(QKeySequence(Qt::Key_L), [this] { m_engine->setSongMode(!m_engine->songMode()); });
    key(QKeySequence(Qt::Key_R), [this] { m_engine->setRecording(!m_engine->recording()); });

    // Register every shortcut on the window so it works whichever widget has focus.
    std::function<void(QMenu*)> registerShortcuts = [&](QMenu* m) {
        for (QAction* a : m->actions()) {
            if (a->menu())
                registerShortcuts(a->menu());
            else if (!a->shortcut().isEmpty())
                addAction(a);
        }
    };
    for (QAction* a : mb->actions())
        if (a->menu())
            registerShortcuts(a->menu());
}

void MainWindow::updateTitle()
{
    const QString name = m_project->filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_project->filePath).completeBaseName();
    setWindowTitle(QStringLiteral("%1%2 — Guiro").arg(name, m_project->isModified() ? QStringLiteral(" •") : QString()));
}

bool MainWindow::maybeSave()
{
    if (!m_project->isModified())
        return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to the current project?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return save();
    return true;
}

bool MainWindow::save()
{
    if (m_project->filePath.isEmpty())
        return saveAs();
    QString err;
    if (!ProjectIO::save(*m_project, m_project->filePath, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_project->setModified(false);
    addRecent(m_project->filePath);
    showHint(tr("Saved %1").arg(m_project->filePath));
    return true;
}

bool MainWindow::saveAs()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), dir + "/Untitled.guiro", tr("Guiro projects (*.guiro)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".guiro")))
        path += QLatin1String(".guiro");
    m_project->filePath = path;
    return save();
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_engine->stop();
    PluginWindow::closeAll();
    m_project->createEmpty();
}

void MainWindow::openDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), QString(), tr("Guiro projects (*.guiro)"));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::showPanel(const QString& name)
{
    if (name == QLatin1String("browser"))
        m_dBrowser->show();
    else
        m_workspace->showPane(name);
}

bool MainWindow::openProject(const QString& path)
{
    m_engine->stop();
    PluginWindow::closeAll();
    QString err;
    QStringList warnings;
    if (!ProjectIO::load(*m_project, path, &err, &warnings)) {
        QMessageBox::critical(this, tr("Open failed"), tr("Could not open %1:\n%2").arg(path, err));
        m_project->createEmpty();
        return false;
    }
    addRecent(path);
    if (!warnings.isEmpty())
        QMessageBox::warning(this, tr("Project loaded with warnings"), warnings.join('\n'));
    return true;
}

void MainWindow::addRecent(const QString& path)
{
    QSettings s;
    QStringList list = s.value("recent").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10)
        list.removeLast();
    s.setValue("recent", list);
    rebuildRecent();
}

void MainWindow::rebuildRecent()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList list = QSettings().value("recent").toStringList();
    for (const QString& p : list)
        m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] {
            if (maybeSave())
                openProject(p);
        })->setToolTip(p);
    if (list.isEmpty())
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
}

void MainWindow::exportAudio()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    const QString base = m_project->filePath.isEmpty() ? QStringLiteral("Guiro Song") : QFileInfo(m_project->filePath).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, tr("Export song"), dir + "/" + base + ".wav",
                                                tr("WAV audio (*.wav);;FLAC audio (*.flac)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".wav", Qt::CaseInsensitive) && !path.endsWith(".flac", Qt::CaseInsensitive))
        path += ".wav";
    QString err;
    if (exportSong(m_engine, path, this, &err))
        showHint(tr("Exported %1").arg(path));
    else
        QMessageBox::warning(this, tr("Export"), err);
}

void MainWindow::createAutomation(const QString& target)
{
    if (target.isEmpty())
        return;
    AutomationClip* a = m_project->addAutomation(target);
    const Tick at = m_engine->songMode() ? (Tick(m_engine->position()) / kBarTicks) * kBarTicks : 0;
    const int track = m_project->firstFreeTrack(at, at + kBarTicks * 4);
    m_project->addClip(ClipKind::Automation, a->id, track, at, kBarTicks * 4);
    showHint(tr("Automation clip created for %1 — edit it in the playlist (double-click adds points)").arg(a->name));
    m_workspace->showPane("playlist");
}

void MainWindow::addEffectToSelected(const PluginDescriptor& d)
{
    Channel* c = m_project->channel(m_project->selectedChannel());
    MixerInsert* m = m_project->insert(c ? c->insert : 0);
    if (!m)
        return;
    for (auto& slot : m->fx) {
        if (slot.plugin)
            continue;
        QString err;
        slot.plugin = PluginManager::instance().create(d, &err);
        if (!slot.plugin) {
            QMessageBox::warning(this, tr("Plugin"), err);
            return;
        }
        m_project->notifyMixer();
        PluginWindow::open(m_project, slot.plugin, m->name, this);
        showHint(tr("%1 added to %2").arg(d.name, m->name));
        return;
    }
    QMessageBox::information(this, tr("Mixer"), tr("All effect slots on %1 are in use.").arg(m->name));
}

void MainWindow::idlePlugins()
{
    for (const auto& c : m_project->channels)
        if (c->plugin)
            c->plugin->idle();
    for (const auto& m : m_project->inserts)
        for (const auto& s : m->fx)
            if (s.plugin)
                s.plugin->idle();
}

void MainWindow::drainRecording()
{
    RecordedNote rn;
    while (m_engine->takeRecordedNote(rn)) {
        const auto key = std::make_pair(rn.channel, rn.key);
        if (rn.endTick < 0) {
            m_pendingRec[key] = {rn.startTick, rn.velocity};
            continue;
        }
        auto it = m_pendingRec.find(key);
        if (it == m_pendingRec.end())
            continue;
        const double start = it->second.first;
        const float vel = it->second.second;
        m_pendingRec.erase(it);
        Pattern* pat = m_project->currentPattern();
        if (!pat || !m_project->channel(rn.channel))
            continue;
        double local = start;
        if (m_engine->songMode()) {
            bool found = false;
            for (const Clip& c : m_project->clips)
                if (c.kind == ClipKind::Pattern && c.ref == pat->id && start >= double(c.start) && start < double(c.end())) {
                    local = start - double(c.start) + double(c.offset);
                    found = true;
                    break;
                }
            if (!found)
                continue;
        }
        const Tick len = std::max<Tick>(kStepTicks / 2, Tick(rn.endTick - start));
        Tick t = Tick(std::llround(local / double(kStepTicks))) * kStepTicks;
        t %= std::max<Tick>(pat->length(), 1);
        pat->notes[rn.channel].push_back({rn.key, t, len, vel});
        m_project->notifyPatternEdited(pat->id);
    }
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(
        this, tr("Keyboard shortcuts"),
        tr("<table cellpadding=3>"
           "<tr><td><b>Space</b></td><td>Play / stop</td></tr>"
           "<tr><td><b>L</b></td><td>Toggle pattern / song mode</td></tr>"
           "<tr><td><b>R</b></td><td>Record (pads, MIDI input)</td></tr>"
           "<tr><td><b>F5 / F6 / F7 / F9</b></td><td>Playlist / Channel rack / Piano roll / Mixer</td></tr>"
           "<tr><td><b>Ctrl+F8 · Alt+F8</b></td><td>Sound pads · Browser</td></tr>"
           "<tr><td></td><td>A window key raises the window; press it again to hide it</td></tr>"
           "<tr><td><b>F12</b></td><td>Close all windows</td></tr>"
           "<tr><td><b>F4 · F2</b></td><td>New pattern · Rename pattern</td></tr>"
           "<tr><td><b>Ctrl+R</b></td><td>Export song</td></tr>"
           "<tr><td><b>Ctrl+M</b></td><td>Metronome</td></tr>"
           "<tr><td><b>Ctrl+.</b></td><td>Panic</td></tr>"
           "<tr><td colspan=2><br><b>Piano roll</b></td></tr>"
           "<tr><td>Click / drag</td><td>Draw / move notes, drag the right edge to resize</td></tr>"
           "<tr><td>Right-click</td><td>Delete notes</td></tr>"
           "<tr><td>Ctrl+drag</td><td>Rubber-band select</td></tr>"
           "<tr><td>↑ ↓ (Shift)</td><td>Transpose selection (octave)</td></tr>"
           "<tr><td>Ctrl+D</td><td>Duplicate selection</td></tr>"
           "<tr><td>Ctrl+wheel / Alt+wheel</td><td>Zoom horizontally / vertically</td></tr>"
           "<tr><td colspan=2><br><b>Playlist</b></td></tr>"
           "<tr><td>Click</td><td>Paint the selected pattern / clip</td></tr>"
           "<tr><td>Ctrl+drag</td><td>Duplicate clip</td></tr>"
           "<tr><td>Ruler drag</td><td>Set loop region (right-click clears)</td></tr>"
           "<tr><td>Automation clip</td><td>Double-click: add point · drag point · right-click point: delete</td></tr>"
           "<tr><td colspan=2><br><b>Pads</b></td></tr>"
           "<tr><td>1-4 Q-R A-F Z-V</td><td>Trigger pads (panel focused)</td></tr>"
           "</table>"));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Guiro"),
                       tr("<h2>Guiro 0.1</h2>"
                          "<p>A Linux-first, pattern-based digital audio workstation.</p>"
                          "<p>Audio: %1 at %2 Hz<br>Plugin formats: %3</p>"
                          "<p>Built with Qt %4.</p>")
                           .arg(m_engine->backendName())
                           .arg(int(m_engine->sampleRate()))
                           .arg(PluginManager::supportedFormats().join(", "), QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state2", saveState());
    m_workspace->saveLayout(s);
    PluginWindow::closeAll();
    e->accept();
}

} // namespace guiro
