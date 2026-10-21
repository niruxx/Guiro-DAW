#include "ui/DockTitle.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

namespace guiro {

DockTitle::DockTitle(QDockWidget* dock, const QString& icon) : QWidget(dock), m_dock(dock), m_icon(icon)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 8, 0);
    lay->setSpacing(2);
    lay->addStretch(1);
    auto mk = [this, lay](const QString& ic, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(icons::icon(ic, theme::textMute, 14));
        b->setToolTip(tip);
        b->setFixedSize(24, 24);
        b->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;padding:0;border-radius:4px;min-height:0;}"
                                        "QToolButton:hover{background:%1;}").arg(theme::bg3.name()));
        lay->addWidget(b);
        return b;
    };
    auto* flt = mk(QStringLiteral("export"), tr("Detach / attach"));
    auto* close = mk(QStringLiteral("close"), tr("Hide panel"));
    connect(flt, &QToolButton::clicked, this, [this] { m_dock->setFloating(!m_dock->isFloating()); });
    connect(close, &QToolButton::clicked, m_dock, &QDockWidget::close);
}

void DockTitle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::bg1);
    p.setPen(theme::lineSoft);
    p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
    const QRectF ic(12, (height() - 16) / 2.0, 16, 16);
    icons::paint(p, m_icon, ic, theme::textDim);
    p.setPen(theme::text);
    p.setFont(theme::uiFont(9, QFont::DemiBold));
    p.drawText(QRectF(ic.right() + 9, 0, width() - 120, height()), Qt::AlignVCenter | Qt::AlignLeft, m_dock->windowTitle());
}

void DockTitle::mouseDoubleClickEvent(QMouseEvent*)
{
    m_dock->setFloating(!m_dock->isFloating());
}

} // namespace guiro
