#include "ui/widgets/Fader.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <cmath>

namespace guiro {

Fader::Fader(QWidget* parent) : QWidget(parent), m_color(theme::accent)
{
    setMinimumSize(22, 90);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(22);
    setCursor(Qt::SizeVerCursor);
    connect(UiHub::instance(), &UiHub::frame, this, &Fader::poll);
}

void Fader::setParam(ParamPtr p)
{
    m_param = std::move(p);
    update();
}

void Fader::poll()
{
    if (m_param && isVisible() && std::abs(m_param->normalized() - m_last) > 0.0005f)
        update();
}

QRectF Fader::track() const
{
    return QRectF(width() / 2.0 - 2, 8, 4, height() - 16);
}

void Fader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = track();
    const float n = m_param ? m_param->normalized() : 0.f;
    m_last = n;

    // Unity-gain tick.
    if (m_param) {
        const float unity = m_param->toNormalized(1.f);
        const qreal uy = t.bottom() - unity * t.height();
        p.setPen(QPen(theme::textMute, 1));
        p.drawLine(QPointF(2, uy), QPointF(width() - 2, uy));
    }

    p.setPen(Qt::NoPen);
    p.setBrush(theme::bg0);
    p.drawRoundedRect(t, 2, 2);
    const qreal y = t.bottom() - n * t.height();
    const bool automated = m_param && m_param->automated.load();
    p.setBrush(automated ? theme::automation : m_color);
    p.drawRoundedRect(QRectF(t.left(), y, t.width(), t.bottom() - y), 2, 2);

    const QRectF thumb(2, y - 5, width() - 4, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::text);
    p.drawRoundedRect(thumb, 3, 3);
}

void Fader::setNorm(float n)
{
    if (!m_param)
        return;
    m_param->setNormalized(std::clamp(n, 0.f, 1.f));
    update();
    emit edited();
    emit UiHub::instance()->paramEdited();
    QToolTip::showText(mapToGlobal(QPoint(width() + 4, int(track().bottom() - m_param->normalized() * track().height()))),
                       m_param->displayText(), this);
}

void Fader::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_param)
        return;
    m_drag = true;
    m_pressY = e->position().y();
    m_pressNorm = m_param->normalized();
}

void Fader::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_drag)
        return;
    const qreal fine = (e->modifiers() & Qt::ShiftModifier) ? 0.2 : 1.0;
    setNorm(m_pressNorm + float((m_pressY - e->position().y()) / track().height() * fine));
}

void Fader::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = false;
}

void Fader::mouseDoubleClickEvent(QMouseEvent*)
{
    if (!m_param)
        return;
    m_param->reset();
    setNorm(m_param->normalized());
}

void Fader::wheelEvent(QWheelEvent* e)
{
    if (!m_param)
        return;
    setNorm(m_param->normalized() + (e->angleDelta().y() > 0 ? 0.02f : -0.02f));
    e->accept();
}

void Fader::contextMenuEvent(QContextMenuEvent* e)
{
    if (!m_param)
        return;
    QMenu menu(this);
    menu.addAction(tr("Reset"), this, [this] { m_param->reset(); setNorm(m_param->normalized()); });
    if (!m_param->path().isEmpty())
        menu.addAction(tr("Create automation clip"), this,
                       [this] { emit UiHub::instance()->automationRequested(m_param->path()); });
    menu.exec(e->globalPos());
}

} // namespace guiro
