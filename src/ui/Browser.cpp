#include "ui/Browser.h"
#include "core/DefaultKit.h"
#include "core/Project.h"
#include "engine/AudioEngine.h"
#include "plugins/PluginManager.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/WineBridge.h"

#include <QComboBox>
#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace guiro {

namespace {

class PluginTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.isEmpty() || items.first()->data(0, Qt::UserRole).toString().isEmpty())
            return nullptr;
        auto* m = new QMimeData;
        m->setData(kPluginMime, items.first()->data(0, Qt::UserRole).toString().toUtf8());
        return m;
    }
    QStringList mimeTypes() const override { return {kPluginMime}; }
};

class FileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(o, i);
        s.setHeight(std::max(s.height(), 24));
        return s;
    }
};

} // namespace

const PluginDescriptor* pluginFromMime(const QMimeData* m)
{
    if (!m || !m->hasFormat(kPluginMime))
        return nullptr;
    const QStringList parts = QString::fromUtf8(m->data(kPluginMime)).split(QLatin1Char('\n'));
    PluginFormat f;
    if (parts.size() != 2 || !formatFromName(parts[0], &f))
        return nullptr;
    return PluginManager::instance().find(f, parts[1]);
}

Browser::Browser(Project* project, AudioEngine* engine, QWidget* parent)
    : QWidget(parent), m_project(project), m_engine(engine)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);

    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    m_tabFiles = new QPushButton(icons::icon("folder"), tr("Files"), this);
    m_tabPlugins = new QPushButton(icons::icon("plugin"), tr("Plugins"), this);
    for (auto* b : {m_tabFiles, m_tabPlugins}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedHeight(28);
        tabs->addWidget(b);
    }
    lay->addLayout(tabs);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search…"));
    m_search->addAction(icons::icon("search", theme::textMute), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    lay->addWidget(m_search);

    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack, 1);

    // ---- Files page
    auto* filesPage = new QWidget(this);
    auto* fl = new QVBoxLayout(filesPage);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);
    m_places = new QComboBox(filesPage);
    const QString home = QDir::homePath();
    struct Place {
        QString name, path;
    };
    QList<Place> places = {{tr("Guiro Packs"), DefaultKit::packsDir()}};
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (!music.isEmpty() && QDir(music).exists())
        places.push_back({tr("Music"), music});
    places.push_back({tr("Home"), home});
    for (const auto& [n, sub] : {std::pair{tr("Desktop"), QStandardPaths::DesktopLocation},
                                 std::pair{tr("Downloads"), QStandardPaths::DownloadLocation}}) {
        const QString pth = QStandardPaths::writableLocation(sub);
        if (!pth.isEmpty() && QDir(pth).exists() && pth != home)
            places.push_back({n, pth});
    }
    places.push_back({tr("File system"), QStringLiteral("/")});
    for (const auto& pl : places)
        m_places->addItem(icons::icon("folder", theme::accent), pl.name, pl.path);
    fl->addWidget(m_places);

    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setRootPath(QStringLiteral("/"));
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilterDisables(false);
    m_fsModel->setNameFilters(SampleCache::nameFilters());
    m_files = new QTreeView(filesPage);
    m_files->setModel(m_fsModel);
    m_files->setItemDelegate(new FileDelegate(m_files));
    for (int c = 1; c < m_fsModel->columnCount(); ++c)
        m_files->hideColumn(c);
    m_files->header()->hide();
    m_files->setDragEnabled(true);
    m_files->setDragDropMode(QAbstractItemView::DragOnly);
    m_files->setAnimated(true);
    m_files->setIndentation(14);
    m_files->setUniformRowHeights(true);
    fl->addWidget(m_files, 1);
    auto* hint = new QLabel(tr("Click to preview · drag onto the rack, playlist or pads"), filesPage);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 8pt;").arg(theme::textMute.name()));
    hint->setWordWrap(true);
    fl->addWidget(hint);
    m_stack->addWidget(filesPage);

    connect(m_places, &QComboBox::currentIndexChanged, this, &Browser::setPlace);
    connect(m_files, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        const QString path = m_fsModel->filePath(idx);
        if (m_fsModel->isDir(idx) || !SampleCache::isAudioFile(path))
            return;
        if (SamplePtr s = SampleCache::instance().load(path))
            m_engine->previewSample(s);
    });
    connect(m_files, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        const QString path = m_fsModel->filePath(idx);
        if (!m_fsModel->isDir(idx) && SampleCache::isAudioFile(path))
            emit sampleActivated(path);
    });

    // ---- Plugins page
    auto* plugPage = new QWidget(this);
    auto* pl = new QVBoxLayout(plugPage);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(6);
    m_plugins = new PluginTree(plugPage);
    m_plugins->setHeaderHidden(true);
    m_plugins->setColumnCount(2);
    m_plugins->header()->setStretchLastSection(false);
    m_plugins->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_plugins->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_plugins->setDragEnabled(true);
    m_plugins->setDragDropMode(QAbstractItemView::DragOnly);
    m_plugins->setIndentation(12);
    m_plugins->setRootIsDecorated(true);
    pl->addWidget(m_plugins, 1);
    auto* row = new QHBoxLayout;
    m_scanLabel = new QLabel(plugPage);
    m_scanLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 8pt;").arg(theme::textMute.name()));
    m_scanLabel->setWordWrap(true);
    auto* rescan = new QToolButton(plugPage);
    rescan->setIcon(icons::icon("refresh"));
    rescan->setToolTip(tr("Rescan plugins (%1)").arg(PluginManager::supportedFormats().join(", ")));
    connect(rescan, &QToolButton::clicked, this, [] { PluginManager::instance().rescan(); });
    auto* windows = new QToolButton(plugPage);
    windows->setText(tr("Windows…"));
    windows->setToolTip(tr("Load Windows VST plugins (.dll) through Wine"));
    connect(windows, &QToolButton::clicked, this, [this] { (new WineBridgeDialog(window()))->show(); });
    row->addWidget(m_scanLabel, 1);
    row->addWidget(windows);
    row->addWidget(rescan);
    pl->addLayout(row);
    m_stack->addWidget(plugPage);

    connect(m_plugins, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QStringList parts = item->data(0, Qt::UserRole).toString().split(QLatin1Char('\n'));
        PluginFormat f;
        if (parts.size() == 2 && formatFromName(parts[0], &f))
            if (const PluginDescriptor* d = PluginManager::instance().find(f, parts[1]))
                emit pluginActivated(*d);
    });

    auto& pm = PluginManager::instance();
    connect(&pm, &PluginManager::pluginsChanged, this, &Browser::rebuildPlugins);
    connect(&pm, &PluginManager::scanProgress, this, [this](int done, int total, const QString& cur) {
        m_scanLabel->setText(tr("Scanning %1/%2  %3").arg(done).arg(total).arg(cur));
    });
    connect(&pm, &PluginManager::scanFinished, this, &Browser::rebuildPlugins);

    connect(m_tabFiles, &QPushButton::clicked, this, [this] { setPage(0); });
    connect(m_tabPlugins, &QPushButton::clicked, this, [this] { setPage(1); });
    connect(m_search, &QLineEdit::textChanged, this, &Browser::applyFilter);

    setPage(0);
    setPlace(0);
    rebuildPlugins();
}

void Browser::setPlace(int idx)
{
    const QString path = m_places->itemData(idx).toString();
    QDir().mkpath(path);
    m_files->setRootIndex(m_fsModel->index(path));
}

void Browser::setPage(int i)
{
    m_stack->setCurrentIndex(i);
    m_tabFiles->setChecked(i == 0);
    m_tabPlugins->setChecked(i == 1);
    applyFilter();
}

void Browser::applyFilter()
{
    const QString text = m_search->text().trimmed();
    if (m_stack->currentIndex() == 0) {
        if (text.isEmpty()) {
            m_fsModel->setNameFilters(SampleCache::nameFilters());
        } else {
            QStringList f;
            for (const QString& ext : SampleCache::nameFilters())
                f << QStringLiteral("*%1%2").arg(text, ext.mid(1));
            m_fsModel->setNameFilters(f);
        }
        return;
    }
    for (int i = 0; i < m_plugins->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_plugins->topLevelItem(i);
        for (int k = 0; k < group->childCount(); ++k) {
            QTreeWidgetItem* it = group->child(k);
            it->setHidden(!text.isEmpty() && !it->text(0).contains(text, Qt::CaseInsensitive) &&
                          !it->toolTip(0).contains(text, Qt::CaseInsensitive));
        }
    }
}

void Browser::rebuildPlugins()
{
    m_plugins->clear();
    auto* inst = new QTreeWidgetItem(m_plugins, {tr("Instruments")});
    auto* fx = new QTreeWidgetItem(m_plugins, {tr("Effects")});
    for (auto* g : {inst, fx}) {
        g->setFlags(Qt::ItemIsEnabled);
        g->setForeground(0, theme::textDim);
        QFont f = theme::uiFont(8.5, QFont::Bold);
        g->setFont(0, f);
    }
    inst->setIcon(0, icons::icon("instrument", theme::accent));
    fx->setIcon(0, icons::icon("effect", theme::accent2));
    const auto& list = PluginManager::instance().plugins();
    for (const auto& d : list) {
        auto* it = new QTreeWidgetItem(d.instrument ? inst : fx);
        const QString tag = formatName(d.format) + (d.bridged ? QStringLiteral(" · Wine") : QString());
        it->setText(0, QStringLiteral("%1   ").arg(d.name));
        it->setText(1, tag);
        it->setForeground(1, d.bridged ? theme::accent2 : theme::textMute);
        it->setFont(1, theme::uiFont(7.5, QFont::DemiBold));
        it->setToolTip(0, QStringLiteral("%1\n%2 · %3\n%4").arg(d.name, d.vendor, tag, d.path.isEmpty() ? d.uid : d.path));
        it->setData(0, Qt::UserRole, formatName(d.format) + QLatin1Char('\n') + d.uid);
        it->setIcon(0, icons::icon(d.instrument ? "instrument" : "effect", d.instrument ? theme::accent : theme::accent2, 16));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }
    inst->setText(0, tr("Instruments  (%1)").arg(inst->childCount()));
    fx->setText(0, tr("Effects  (%1)").arg(fx->childCount()));
    inst->setExpanded(true);
    fx->setExpanded(true);
    if (!PluginManager::instance().isScanning()) {
        const int failed = int(PluginManager::instance().blacklist().size());
        m_scanLabel->setText(list.isEmpty() ? tr("No plugins found. Install VST3/CLAP/LV2 plugins and rescan.")
                                            : tr("%1 plugins").arg(list.size()) +
                                                  (failed ? tr(" · %1 failed to load").arg(failed) : QString()));
    }
    applyFilter();
}

} // namespace guiro
