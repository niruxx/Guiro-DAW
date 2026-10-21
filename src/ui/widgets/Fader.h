#pragma once

#include "core/Param.h"

#include <QColor>
#include <QWidget>

namespace guiro {

class Fader : public QWidget {
    Q_OBJECT
public:
    explicit Fader(QWidget* parent = nullptr);
    void setParam(ParamPtr p);
    void setColor(const QColor& c) { m_color = c; update(); }

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

private:
    QRectF track() const;
    void setNorm(float n);
    void poll();

    ParamPtr m_param;
    QColor m_color;
    float m_last = -1.f;
    bool m_drag = false;
    qreal m_pressY = 0;
    float m_pressNorm = 0;
};

} // namespace guiro
