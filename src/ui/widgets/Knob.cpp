#include "ui/widgets/Knob.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <cmath>

namespace guiro {

Knob::Knob(QWidget* parent, int diameter) : QWidget(parent), m_color(theme::accent), m_diameter(diameter)
{
    setDiameter(diameter);
    setCursor(Qt::SizeVerCursor);
    connect(UiHub::instance(), &UiHub::frame, this, &Knob::poll);
}

void Knob::setDiameter(int d)
{
    m_diameter = d;
    const int labelW = m_label.isEmpty() ? 0 : QFontMetrics(theme::uiFont(7.5, QFont::Medium)).horizontalAdvance(m_label) + 6;
    setFixedSize(std::max(d + 4, labelW), d + 4 + (m_label.isEmpty() ? 0 : 15));
    update();
}

void Knob::setLabel(const QString& l)
{
    m_label = l;
    setDiameter(m_diameter);
}

void Knob::setParam(ParamPtr p)
{
    m_param = std::move(p);
    m_lastShown = -1.f;
    if (m_param)
        setToolTip(m_param->name());
    update();
}

void Knob::poll()
{
    if (!m_param || !isVisible())
        return;
    const float v = m_param->normalized();
    const bool a = m_param->automated.load();
    if (std::abs(v - m_lastShown) > 0.0005f || a != m_lastAutomated)
        update();
}

void Knob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal d = m_diameter;
    const QRectF r((width() - d) / 2.0, 2, d, d);
    const QPointF c = r.center();
    const float norm = m_param ? m_param->normalized() : 0.f;
    m_lastShown = norm;
    m_lastAutomated = m_param && m_param->automated.load();

    const bool bipolar = m_param && m_param->min() < 0.f && m_param->max() > 0.f;
    const QColor col = m_lastAutomated ? theme::automation : m_color;

    // Flat body
    const qreal arcW = std::max(2.5, d * 0.085);
    const QRectF arcR = r.adjusted(arcW / 2 + 1, arcW / 2 + 1, -arcW / 2 - 1, -arcW / 2 - 1);
    const qreal inset = d * 0.24;
    p.setPen(Qt::NoPen);
    p.setBrush(m_hover ? theme::bg4 : theme::bg3);
    p.drawEllipse(r.adjusted(inset, inset, -inset, -inset));

    // Track (270 degrees, from 225 to -45)
    p.setPen(QPen(theme::bg4, arcW, Qt::SolidLine, Qt::FlatCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcR, 225 * 16, -270 * 16);

    // Value
    const qreal start = bipolar ? 90.0 : 225.0;
    const qreal span = bipolar ? -(norm - 0.5) * 270.0 : -norm * 270.0;
    if (std::abs(span) > 0.5) {
        p.setPen(QPen(col, arcW, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arcR, int(start * 16), int(span * 16));
    }

    // Pointer
    const qreal ang = (225.0 - norm * 270.0) * M_PI / 180.0;
    const qreal r0 = d * 0.06, r1 = d * 0.25;
    p.setPen(QPen(theme::text, std::max(1.5, d * 0.055), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(c.x() + std::cos(ang) * r0, c.y() - std::sin(ang) * r0),
               QPointF(c.x() + std::cos(ang) * r1, c.y() - std::sin(ang) * r1));

    if (!m_label.isEmpty()) {
        p.setPen(theme::textDim);
        p.setFont(theme::uiFont(7.5, QFont::Medium));
        p.drawText(QRectF(0, d + 3, width(), 14), Qt::AlignCenter, m_label);
    }
}

void Knob::setNorm(float n)
{
    if (!m_param)
        return;
    m_param->setNormalized(std::clamp(n, 0.f, 1.f));
    update();
    emit edited();
    emit UiHub::instance()->paramEdited();
}

void Knob::showValue()
{
    if (m_param)
        QToolTip::showText(mapToGlobal(QPoint(width() / 2, -6)), m_param->name() + QStringLiteral(": ") + m_param->displayText(), this);
}

void Knob::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_param)
        return;
    m_dragging = true;
    m_pressPos = e->globalPosition();
    m_pressNorm = m_param->normalized();
    showValue();
}

void Knob::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    const qreal dy = m_pressPos.y() - e->globalPosition().y();
    const qreal dx = e->globalPosition().x() - m_pressPos.x();
    const float sens = (e->modifiers() & Qt::ShiftModifier) ? 1000.f : 180.f;
    setNorm(m_pressNorm + float(dy + dx * 0.3) / sens);
    if (e->modifiers() & Qt::ShiftModifier) {
        m_pressPos = e->globalPosition();
        m_pressNorm = m_param->normalized();
    }
    showValue();
}

void Knob::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

void Knob::mouseDoubleClickEvent(QMouseEvent*)
{
    if (!m_param)
        return;
    m_param->reset();
    update();
    emit edited();
    emit UiHub::instance()->paramEdited();
    showValue();
}

void Knob::wheelEvent(QWheelEvent* e)
{
    if (!m_param)
        return;
    const float step = (e->modifiers() & Qt::ShiftModifier) ? 0.005f : 0.03f;
    const float dir = e->angleDelta().y() > 0 ? 1.f : (e->angleDelta().y() < 0 ? -1.f : 0.f);
    if (m_param->unit() == ParamUnit::Choice) {
        m_param->setValue(m_param->value() + dir);
        update();
        emit edited();
        emit UiHub::instance()->paramEdited();
    } else {
        setNorm(m_param->normalized() + dir * step);
    }
    showValue();
    e->accept();
}

void Knob::contextMenuEvent(QContextMenuEvent* e)
{
    if (!m_param)
        return;
    QMenu menu(this);
    menu.addSection(m_param->name());
    menu.addAction(tr("Reset to default"), this, [this] {
        m_param->reset();
        update();
        emit edited();
    });
    menu.addAction(tr("Type in value…"), this, [this] {
        bool ok = false;
        const double v = QInputDialog::getDouble(this, m_param->name(), tr("Value (%1 – %2)").arg(m_param->min()).arg(m_param->max()),
                                                 m_param->value(), m_param->min(), m_param->max(), 3, &ok);
        if (ok) {
            m_param->setValue(float(v));
            update();
            emit edited();
            emit UiHub::instance()->paramEdited();
        }
    });
    if (!m_param->path().isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Create automation clip"), this,
                       [this] { emit UiHub::instance()->automationRequested(m_param->path()); });
    }
    menu.exec(e->globalPos());
}

void Knob::enterEvent(QEnterEvent*)
{
    m_hover = true;
    update();
}

void Knob::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

} // namespace guiro
