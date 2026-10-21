#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSizeGrip>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace guiro {

// ================================================================ frame

namespace {

class TitleBar : public QWidget {
public:
    TitleBar(const QString& icon, QWidget* parent) : QWidget(parent), m_icon(icon) { setFixedHeight(30); }
    bool active = false;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), active ? theme::bg3 : theme::bg2);
        if (active)
            p.fillRect(QRectF(0, 0, width(), 2), theme::accent);
        icons::paint(p, m_icon, QRectF(10, (height() - 15) / 2.0, 15, 15), active ? theme::accent : theme::textMute);
    }

private:
    QString m_icon;
};

} // namespace

WorkspaceFrame::WorkspaceFrame(Workspace* ws, const QString& key, const QString& title, const QString& icon, QWidget* content)
    : QWidget(nullptr), m_ws(ws), m_key(key), m_icon(icon)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(1, 1, 1, 1);
    lay->setSpacing(0);

    auto* bar = new TitleBar(icon, this);
    m_bar = bar;
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(32, 0, 4, 0);
    bl->setSpacing(2);
    m_title = new QLabel(title, bar);
    m_title->setFont(theme::uiFont(8.8, QFont::DemiBold));
    bl->addWidget(m_title);
    bl->addStretch(1);
    auto mk = [bar, bl](const QString& ic, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setIcon(icons::icon(ic, theme::textDim, 14));
        b->setToolTip(tip);
        b->setFixedSize(26, 22);
        b->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;padding:0;border-radius:4px;min-height:0;}"
                                        "QToolButton:hover{background:%1;}").arg(theme::bg4.name()));
        bl->addWidget(b);
        return b;
    };
    auto* max = mk(QStringLiteral("export"), tr("Maximize / restore"));
    auto* close = mk(QStringLiteral("close"), tr("Close window"));
    connect(max, &QToolButton::clicked, this, [this] {
        if (QMdiSubWindow* s = sub())
            s->isMaximized() ? s->showNormal() : s->showMaximized();
    });
    connect(close, &QToolButton::clicked, this, [this] { m_ws->hidePane(m_key); });
    bar->installEventFilter(this);
    m_title->installEventFilter(this);

    lay->addWidget(bar);
    lay->addWidget(content, 1);

    m_grip = new QSizeGrip(this);
    m_grip->setFixedSize(14, 14);
    m_grip->setStyleSheet(QStringLiteral("background:transparent;"));
    m_grip->raise();

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::bg1);
    setPalette(pal);
}

QMdiSubWindow* WorkspaceFrame::sub() const
{
    return qobject_cast<QMdiSubWindow*>(parentWidget());
}

void WorkspaceFrame::setActive(bool a)
{
    m_active = a;
    static_cast<TitleBar*>(m_bar)->active = a;
    m_title->setStyleSheet(QStringLiteral("color:%1;").arg((a ? theme::text : theme::textDim).name()));
    m_bar->update();
    update();
}

void WorkspaceFrame::setTitle(const QString& t)
{
    m_title->setText(t);
}

void WorkspaceFrame::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_grip->move(width() - m_grip->width(), height() - m_grip->height());
    m_grip->raise();
}

bool WorkspaceFrame::eventFilter(QObject* o, QEvent* e)
{
    QMdiSubWindow* s = sub();
    if (!s)
        return false;
    switch (e->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton) {
            m_dragging = !s->isMaximized();
            m_pressGlobal = me->globalPosition().toPoint();
            m_pressPos = s->pos();
            if (QMdiArea* area = s->mdiArea())
                area->setActiveSubWindow(s);
            return true;
        }
        break;
    }
    case QEvent::MouseMove:
        if (m_dragging) {
            auto* me = static_cast<QMouseEvent*>(e);
            QPoint p = m_pressPos + (me->globalPosition().toPoint() - m_pressGlobal);
            if (QMdiArea* area = s->mdiArea()) {
                const QRect vr = area->viewport()->rect();
                p.setX(std::clamp(p.x(), 80 - s->width(), vr.width() - 80));
                p.setY(std::clamp(p.y(), 0, vr.height() - 30));
            }
            s->move(p);
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        m_dragging = false;
        break;
    case QEvent::MouseButtonDblClick:
        s->isMaximized() ? s->showNormal() : s->showMaximized();
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(o, e);
}

// ================================================================ workspace

Workspace::Workspace(QWidget* parent) : QMdiArea(parent)
{
    setBackground(theme::bg0);
    setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(this, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow* active) {
        for (auto& [k, pane] : m_panes)
            pane.frame->setActive(pane.sub == active);
    });
}

QAction* Workspace::addPane(const QString& key, const QString& title, const QString& icon, QWidget* content,
                            const QKeySequence& shortcut)
{
    Pane pane;
    pane.frame = new WorkspaceFrame(this, key, title, icon, content);
    pane.sub = addSubWindow(pane.frame, Qt::FramelessWindowHint);
    pane.sub->setAttribute(Qt::WA_DeleteOnClose, false);
    pane.sub->setMinimumSize(320, 180);
    pane.sub->installEventFilter(this);
    pane.action = new QAction(title, this);
    pane.action->setCheckable(true);
    pane.action->setShortcut(shortcut);
    pane.action->setToolTip(shortcut.isEmpty() ? title : QStringLiteral("%1 (%2)").arg(title, shortcut.toString(QKeySequence::NativeText)));
    // Triggered by shortcut or button: FL semantics (raise, or hide if already in front).
    connect(pane.action, &QAction::triggered, this, [this, key] { toggle(key); });
    pane.sub->hide();
    m_panes[key] = pane;
    return pane.action;
}

QAction* Workspace::action(const QString& key) const
{
    auto it = m_panes.find(key);
    return it == m_panes.end() ? nullptr : it->second.action;
}

void Workspace::toggle(const QString& key)
{
    auto it = m_panes.find(key);
    if (it == m_panes.end())
        return;
    QMdiSubWindow* s = it->second.sub;
    if (s->isVisible() && activeSubWindow() == s && key != QLatin1String("playlist"))
        hidePane(key);
    else
        showPane(key);
}

void Workspace::showPane(const QString& key)
{
    auto it = m_panes.find(key);
    if (it == m_panes.end())
        return;
    if (m_layoutPending && !m_restored)
        m_deferred.push_back(key);
    QMdiSubWindow* s = it->second.sub;
    s->show();
    s->raise();
    setActiveSubWindow(s);
    it->second.frame->setFocus();
    it->second.action->setChecked(true);
}

void Workspace::hidePane(const QString& key)
{
    auto it = m_panes.find(key);
    if (it == m_panes.end())
        return;
    it->second.sub->hide();
    it->second.action->setChecked(false);
}

void Workspace::closeAll()
{
    for (auto& [k, pane] : m_panes)
        if (k != QLatin1String("playlist"))
            hidePane(k);
}

void Workspace::setPaneTitle(const QString& key, const QString& title)
{
    if (auto it = m_panes.find(key); it != m_panes.end())
        it->second.frame->setTitle(title);
}

bool Workspace::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() == QEvent::Show || e->type() == QEvent::Hide) {
        for (auto& [k, pane] : m_panes)
            if (pane.sub == o)
                pane.action->setChecked(e->type() == QEvent::Show);
    }
    return QMdiArea::eventFilter(o, e);
}

void Workspace::resizeEvent(QResizeEvent* e)
{
    QMdiArea::resizeEvent(e);
    // Defaults depend on the real size, which is only known once the window settles.
    if (m_layoutPending && !m_layoutScheduled && viewport()->height() > 400 && !m_restored) {
        m_layoutScheduled = true;
        QTimer::singleShot(0, this, &Workspace::applyDefaultGeometry);
    }
}

void Workspace::applyDefaultGeometry()
{
    m_layoutPending = false;
    const QRect a = viewport()->rect();
    const int w = a.width(), h = a.height();
    auto place = [&](const QString& key, QRect r, bool visible) {
        auto it = m_panes.find(key);
        if (it == m_panes.end())
            return;
        it->second.sub->showNormal();
        it->second.sub->setGeometry(r);
        if (!visible)
            hidePane(key);
    };
    // Like FL: the playlist fills the workspace and the channel rack floats on top.
    place(QStringLiteral("playlist"), a, true);
    place(QStringLiteral("piano"), QRect(int(w * 0.06), int(h * 0.30), int(w * 0.88), int(h * 0.66)), false);
    place(QStringLiteral("mixer"), QRect(int(w * 0.04), int(h * 0.36), int(w * 0.92), int(h * 0.62)), false);
    place(QStringLiteral("pads"), QRect(w - 580, 30, 540, std::min(600, h - 60)), false);
    place(QStringLiteral("rack"), QRect(int(w * 0.10), int(h * 0.08), std::min(860, int(w * 0.62)), std::min(440, int(h * 0.5))), true);
    if (auto it = m_panes.find(QStringLiteral("playlist")); it != m_panes.end())
        it->second.sub->showMaximized();
    showPane(QStringLiteral("rack"));
    const auto deferred = std::move(m_deferred);
    m_deferred.clear();
    for (const QString& k : deferred)
        showPane(k);
}

void Workspace::resetLayout()
{
    m_restored = false;
    applyDefaultGeometry();
}

void Workspace::saveLayout(QSettings& s) const
{
    for (const auto& [k, pane] : m_panes) {
        s.setValue(QStringLiteral("workspace/%1/geometry").arg(k), pane.sub->isMaximized() ? pane.sub->normalGeometry() : pane.sub->geometry());
        s.setValue(QStringLiteral("workspace/%1/visible").arg(k), pane.sub->isVisible());
        s.setValue(QStringLiteral("workspace/%1/maximized").arg(k), pane.sub->isMaximized());
    }
}

void Workspace::restoreLayout(QSettings& s)
{
    if (!s.contains(QStringLiteral("workspace/playlist/geometry")))
        return;
    m_restored = true;
    m_layoutPending = false;
    for (auto& [k, pane] : m_panes) {
        const QRect g = s.value(QStringLiteral("workspace/%1/geometry").arg(k)).toRect();
        if (g.isValid())
            pane.sub->setGeometry(g);
        if (s.value(QStringLiteral("workspace/%1/visible").arg(k)).toBool()) {
            if (s.value(QStringLiteral("workspace/%1/maximized").arg(k)).toBool())
                pane.sub->showMaximized();
            else
                pane.sub->show();
            pane.action->setChecked(true);
        } else {
            hidePane(k);
        }
    }
}

} // namespace guiro
