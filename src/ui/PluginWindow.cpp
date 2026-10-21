#include "ui/PluginWindow.h"
#include "core/Project.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

namespace guiro {

namespace {
QHash<PluginInstance*, PluginWindow*>& registry()
{
    static QHash<PluginInstance*, PluginWindow*> r;
    return r;
}

bool nativeEditorsAvailable()
{
    // Linux plugin GUIs embed via X11; under a pure Wayland session we fall back to generic editors.
    return QGuiApplication::platformName() == QLatin1String("xcb");
}
} // namespace

void PluginWindow::open(Project* project, const PluginPtr& plugin, const QString& title, QWidget* parent)
{
    if (!plugin)
        return;
    if (PluginWindow* w = registry().value(plugin.get())) {
        w->show();
        w->raise();
        w->activateWindow();
        return;
    }
    auto* w = new PluginWindow(project, plugin, title, parent);
    registry().insert(plugin.get(), w);
    w->show();
}

void PluginWindow::closeAll()
{
    const auto windows = registry().values();
    for (PluginWindow* w : windows)
        w->close();
}

PluginWindow::PluginWindow(Project* project, const PluginPtr& plugin, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window), m_project(project), m_plugin(plugin), m_raw(plugin.get())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("%1 — %2").arg(title, plugin->name()));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setFixedHeight(38);
    bar->setAutoFillBackground(true);
    QPalette pal = bar->palette();
    pal.setColor(QPalette::Window, theme::bg2);
    bar->setPalette(pal);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 4, 10, 4);
    auto* name = new QLabel(QStringLiteral("<b>%1</b>  <span style='color:%2'>%3 · %4</span>")
                                .arg(plugin->name().toHtmlEscaped(), theme::textMute.name(),
                                     plugin->descriptor().vendor.toHtmlEscaped(), formatName(plugin->descriptor().format)),
                            bar);
    bl->addWidget(name, 1);
    m_btnNative = new QPushButton(tr("Editor"), bar);
    m_btnGeneric = new QPushButton(tr("Parameters"), bar);
    for (auto* b : {m_btnNative, m_btnGeneric}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        bl->addWidget(b);
    }
    lay->addWidget(bar);

    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack, 1);

    connect(m_btnNative, &QPushButton::clicked, this, &PluginWindow::showNative);
    connect(m_btnGeneric, &QPushButton::clicked, this, &PluginWindow::showGeneric);
    connect(UiHub::instance(), &UiHub::frame, this, &PluginWindow::poll);

    const bool native = plugin->hasEditor() && nativeEditorsAvailable();
    m_btnNative->setEnabled(native);
    if (!native)
        m_btnNative->setToolTip(plugin->hasEditor() ? tr("Native editors need the X11 (xcb) platform — start Guiro without GUIRO_WAYLAND=1")
                                                    : tr("This plugin has no editor"));
    if (native)
        showNative();
    else
        showGeneric();
}

PluginWindow::~PluginWindow()
{
    registry().remove(m_raw);
}

void PluginWindow::closeEvent(QCloseEvent* e)
{
    if (auto p = m_plugin.lock(); p && m_editorOpen) {
        p->closeEditor();
        p->onEditorResizeRequest = nullptr;
    }
    m_editorOpen = false;
    e->accept();
}

void PluginWindow::showNative()
{
    auto p = m_plugin.lock();
    if (!p)
        return;
    m_btnNative->setChecked(true);
    if (!m_host) {
        m_host = new QWidget(m_stack);
        m_host->setAttribute(Qt::WA_NativeWindow);
        m_host->setAttribute(Qt::WA_DontCreateNativeAncestors);
        m_host->setStyleSheet(QStringLiteral("background:black;"));
        m_stack->addWidget(m_host);
        m_stack->setCurrentWidget(m_host);
        m_host->winId();
        QSize size;
        if (p->openEditor(m_host->windowHandle(), &size)) {
            m_editorOpen = true;
            m_host->setFixedSize(size);
            QPointer<PluginWindow> self(this);
            p->onEditorResizeRequest = [self](QSize s) {
                if (!self || !self->m_host)
                    return;
                self->m_host->setFixedSize(s);
                self->adjustSize();
                if (auto pl = self->m_plugin.lock())
                    pl->editorResized(s);
            };
        } else {
            m_btnNative->setEnabled(false);
            showGeneric();
            return;
        }
    }
    m_stack->setCurrentWidget(m_host);
    adjustSize();
    setFixedSize(sizeHint());
}

void PluginWindow::showGeneric()
{
    m_btnGeneric->setChecked(true);
    if (!m_generic)
        buildGeneric();
    m_stack->setCurrentWidget(m_generic);
    setMinimumSize(420, 300);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    resize(std::max(width(), 460), std::max(height(), 520));
}

QString PluginWindow::targetFor(uint32_t paramId) const
{
    for (const auto& c : m_project->channels)
        if (c->plugin.get() == m_raw)
            return QStringLiteral("plugin/ch%1/%2").arg(c->id).arg(paramId);
    for (const auto& m : m_project->inserts)
        for (int s = 0; s < kNumFxSlots; ++s)
            if (m->fx[size_t(s)].plugin.get() == m_raw)
                return QStringLiteral("plugin/fx%1.%2/%3").arg(m->index).arg(s).arg(paramId);
    return {};
}

void PluginWindow::buildGeneric()
{
    auto p = m_plugin.lock();
    m_generic = new QScrollArea(m_stack);
    auto* scroll = static_cast<QScrollArea*>(m_generic);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(4);
    int shown = 0;
    if (p) {
        for (int i = 0; i < p->paramCount() && shown < 768; ++i) {
            const PluginParamInfo info = p->paramInfo(i);
            if (info.hidden || info.max <= info.min)
                continue;
            ++shown;
            auto* row = new QWidget(body);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            auto* label = new QLabel(info.name, row);
            label->setFixedWidth(150);
            label->setToolTip(info.name);
            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(0, 1000);
            auto* value = new QLabel(row);
            value->setFixedWidth(90);
            value->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textDim.name()));
            rl->addWidget(label);
            rl->addWidget(slider, 1);
            rl->addWidget(value);
            lay->addWidget(row);
            const uint32_t id = info.id;
            const double mn = info.min, mx = info.max;
            m_rows.push_back({id, slider, value, mn, mx});
            connect(slider, &QSlider::sliderMoved, this, [this, id, mn, mx, value](int v) {
                if (auto pl = m_plugin.lock()) {
                    const double plain = mn + (mx - mn) * v / 1000.0;
                    pl->setParamValue(id, plain);
                    value->setText(pl->paramText(id, plain));
                    emit UiHub::instance()->paramEdited();
                }
            });
            slider->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(slider, &QSlider::customContextMenuRequested, this, [this, id, slider](const QPoint& pt) {
                QMenu menu(this);
                const QString target = targetFor(id);
                QAction* a = menu.addAction(icons::icon("automation", theme::automation), tr("Create automation clip"), this,
                                            [target] { emit UiHub::instance()->automationRequested(target); });
                a->setEnabled(!target.isEmpty());
                menu.exec(slider->mapToGlobal(pt));
            });
        }
    }
    if (!shown) {
        auto* l = new QLabel(tr("This plugin exposes no automatable parameters."), body);
        l->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMute.name()));
        lay->addWidget(l);
    }
    lay->addStretch(1);
    scroll->setWidget(body);
    m_stack->addWidget(m_generic);
    m_tick = 9;
    poll();
}

void PluginWindow::poll()
{
    auto p = m_plugin.lock();
    if (!p) {
        close();
        return;
    }
    if (!m_generic || m_stack->currentWidget() != m_generic || ++m_tick % 10 != 0)
        return;
    for (auto& r : m_rows) {
        if (r.slider->isSliderDown())
            continue;
        const double v = p->paramValue(r.id);
        const int pos = int((v - r.min) / (r.max - r.min) * 1000.0);
        if (r.slider->value() != pos) {
            QSignalBlocker b(r.slider);
            r.slider->setValue(pos);
        }
        r.value->setText(p->paramText(r.id, v));
    }
}

} // namespace guiro
