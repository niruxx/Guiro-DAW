#pragma once

#include "core/Param.h"

#include <QColor>
#include <QWidget>

namespace guiro {

// Rotary control bound to a Param. Vertical drag (Shift = fine), wheel,
// double-click resets, right-click offers automation.
class Knob : public QWidget {
    Q_OBJECT
public:
    explicit Knob(QWidget* parent = nullptr, int diameter = 34);

    void setParam(ParamPtr p);
    ParamPtr param() const { return m_param; }
    void setColor(const QColor& c) { m_color = c; update(); }
    void setLabel(const QString& l);
    void setDiameter(int d);

signals:
    void edited();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void setNorm(float n);
    void showValue();
    void poll();

    ParamPtr m_param;
    QColor m_color;
    QString m_label;
    int m_diameter;
    float m_lastShown = -1.f;
    bool m_lastAutomated = false;
    bool m_dragging = false;
    bool m_hover = false;
    QPointF m_pressPos;
    float m_pressNorm = 0.f;
};

} // namespace guiro
