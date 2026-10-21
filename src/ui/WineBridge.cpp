#include "ui/WineBridge.h"
#include "plugins/PluginManager.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace guiro {

QString WineBridgeDialog::yabridgectl()
{
    QString p = QStandardPaths::findExecutable(QStringLiteral("yabridgectl"));
    if (p.isEmpty()) {
        // The official tarball installs into ~/.local/share/yabridge.
        const QString local = QDir::homePath() + QStringLiteral("/.local/share/yabridge/yabridgectl");
        if (QFileInfo(local).isExecutable())
            p = local;
    }
    return p;
}

QString WineBridgeDialog::wine()
{
    return QStandardPaths::findExecutable(QStringLiteral("wine"));
}

WineBridgeDialog::WineBridgeDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Windows plugins (Wine)"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 600);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(10);

    auto* title = new QLabel(tr("Windows plugins"), this);
    title->setFont(theme::uiFont(14, QFont::DemiBold));
    lay->addWidget(title);
    auto* intro = new QLabel(tr("Guiro runs Windows VST2 (.dll), VST3 and CLAP plugins through <b>Wine</b> with "
                                "<b>yabridge</b>, which turns them into Linux plugins with working editors. Add "
                                "the folders your Windows plugins were installed to, then sync."),
                             this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textDim.name()));
    lay->addWidget(intro);

    m_wineStatus = new QLabel(this);
    m_bridgeStatus = new QLabel(this);
    lay->addWidget(m_wineStatus);
    lay->addWidget(m_bridgeStatus);
    m_help = new QLabel(this);
    m_help->setWordWrap(true);
    m_help->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_help->setOpenExternalLinks(true);
    m_help->setTextFormat(Qt::RichText);
    lay->addWidget(m_help);

    auto* foldersLabel = new QLabel(tr("PLUGIN FOLDERS"), this);
    foldersLabel->setStyleSheet(QStringLiteral("color:%1;font-size:7.5pt;font-weight:700;").arg(theme::textMute.name()));
    lay->addWidget(foldersLabel);
    m_folders = new QListWidget(this);
    m_folders->setMinimumHeight(120);
    lay->addWidget(m_folders, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(icons::icon("plus"), tr("Add folder…"), this);
    auto* rm = new QPushButton(icons::icon("trash"), tr("Remove"), this);
    auto* syncBtn = new QPushButton(icons::icon("refresh"), tr("Sync && rescan"), this);
    syncBtn->setDefault(true);
    m_installer = new QPushButton(icons::icon("open"), tr("Run Windows installer…"), this);
    row->addWidget(add);
    row->addWidget(rm);
    row->addStretch(1);
    row->addWidget(m_installer);
    row->addWidget(syncBtn);
    lay->addLayout(row);
    m_needsBridge = {add, rm, syncBtn};

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFont(theme::monoFont(8.5, QFont::Normal));
    m_log->setMaximumHeight(170);
    m_log->setPlaceholderText(tr("yabridge output appears here"));
    lay->addWidget(m_log);

    connect(add, &QPushButton::clicked, this, &WineBridgeDialog::addFolder);
    connect(rm, &QPushButton::clicked, this, &WineBridgeDialog::removeFolder);
    connect(syncBtn, &QPushButton::clicked, this, [this] { sync(true); });
    connect(m_installer, &QPushButton::clicked, this, &WineBridgeDialog::runInstaller);

    refreshStatus();
    refreshFolders();
}

void WineBridgeDialog::refreshStatus()
{
    const QString w = wine();
    const QString y = yabridgectl();
    auto line = [](bool ok, const QString& name, const QString& detail) {
        return QStringLiteral("<span style='color:%1'>%2</span>&nbsp; <b>%3</b> &nbsp;<span style='color:%4'>%5</span>")
            .arg((ok ? theme::good : theme::danger).name(), ok ? QStringLiteral("●") : QStringLiteral("○"), name,
                 theme::textDim.name(), detail.toHtmlEscaped());
    };
    m_wineStatus->setText(line(!w.isEmpty(), QStringLiteral("Wine"), w.isEmpty() ? tr("not installed") : w));
    m_bridgeStatus->setText(line(!y.isEmpty(), QStringLiteral("yabridge"), y.isEmpty() ? tr("not installed") : y));
    if (!w.isEmpty() && !y.isEmpty()) {
        QProcess p;
        p.start(w, {QStringLiteral("--version")});
        if (p.waitForFinished(3000))
            m_wineStatus->setText(line(true, QStringLiteral("Wine"), QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed()));
        m_help->clear();
        m_help->hide();
    } else {
        m_help->show();
        m_help->setText(tr("<p>Install both, then reopen this window:</p>"
                           "<p><b>Arch / Manjaro / EndeavourOS</b><br>"
                           "<code>sudo pacman -S wine-staging</code><br>"
                           "<code>yay -S yabridge-bin</code> &nbsp;(AUR — also provides <code>yabridgectl</code>)</p>"
                           "<p><b>Other distros:</b> install Wine Staging from your package manager or "
                           "<a href='https://wiki.winehq.org/Download'>WineHQ</a>, and yabridge from "
                           "<a href='https://github.com/robbert-vdh/yabridge/releases'>its releases page</a> "
                           "(extract to <code>~/.local/share/yabridge</code>).</p>"));
    }
    for (QPushButton* b : m_needsBridge)
        b->setEnabled(!y.isEmpty());
    m_installer->setEnabled(!w.isEmpty());
}

void WineBridgeDialog::log(const QString& text)
{
    m_log->appendPlainText(text.trimmed());
}

void WineBridgeDialog::run(const QStringList& args, std::function<void(int, const QString&)> done)
{
    const QString exe = yabridgectl();
    if (exe.isEmpty() || m_busy)
        return;
    m_busy = true;
    for (QPushButton* b : m_needsBridge)
        b->setEnabled(false);
    log(QStringLiteral("$ yabridgectl %1").arg(args.join(' ')));
    auto* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p] { log(QString::fromLocal8Bit(p->readAll())); });
    connect(p, &QProcess::finished, this, [this, p, done](int code, QProcess::ExitStatus) {
        const QString rest = QString::fromLocal8Bit(p->readAll());
        if (!rest.trimmed().isEmpty())
            log(rest);
        p->deleteLater();
        m_busy = false;
        for (QPushButton* b : m_needsBridge)
            b->setEnabled(true);
        if (done)
            done(code, rest);
    });
    p->start(exe, args);
}

void WineBridgeDialog::refreshFolders()
{
    m_folders->clear();
    const QString exe = yabridgectl();
    if (exe.isEmpty())
        return;
    QProcess p;
    p.start(exe, {QStringLiteral("list")});
    if (!p.waitForFinished(5000))
        return;
    const QStringList lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n');
    for (QString l : lines) {
        l = l.trimmed();
        if (l.startsWith('/'))
            m_folders->addItem(new QListWidgetItem(icons::icon("folder", theme::accent), l));
    }
    if (m_folders->count() == 0) {
        auto* hint = new QListWidgetItem(tr("No folders yet — add e.g. drive_c/Program Files/Common Files/VST3"));
        hint->setFlags(Qt::NoItemFlags);
        m_folders->addItem(hint);
    }
}

void WineBridgeDialog::addFolder()
{
    QString start = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files");
    if (!QDir(start).exists())
        start = QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder with Windows plugins (.dll / .vst3 / .clap)"), start);
    if (dir.isEmpty())
        return;
    run({QStringLiteral("add"), dir}, [this](int code, const QString&) {
        refreshFolders();
        if (code == 0)
            sync(true);
    });
}

void WineBridgeDialog::removeFolder()
{
    QListWidgetItem* it = m_folders->currentItem();
    if (!it || !it->text().startsWith('/'))
        return;
    run({QStringLiteral("rm"), it->text()}, [this](int, const QString&) {
        refreshFolders();
        sync(true);
    });
}

void WineBridgeDialog::sync(bool rescanAfter)
{
    run({QStringLiteral("sync"), QStringLiteral("--prune")}, [this, rescanAfter](int code, const QString&) {
        if (code != 0) {
            log(tr("Sync failed (exit code %1).").arg(code));
            return;
        }
        if (rescanAfter) {
            log(tr("Rescanning plugins — Windows plugins can take a while to load the first time…"));
            PluginManager::instance().rescan();
        }
    });
}

void WineBridgeDialog::runInstaller()
{
    const QString w = wine();
    if (w.isEmpty())
        return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Windows plugin installer"), QDir::homePath() + "/Downloads",
                                                   tr("Installers (*.exe *.msi)"));
    if (f.isEmpty())
        return;
    QStringList args;
    if (f.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive))
        args << QStringLiteral("msiexec") << QStringLiteral("/i") << f;
    else
        args << f;
    QProcess::startDetached(w, args);
    log(tr("Started %1 under Wine. When it finishes, add the folder it installed to and sync.").arg(QFileInfo(f).fileName()));
}

} // namespace guiro
