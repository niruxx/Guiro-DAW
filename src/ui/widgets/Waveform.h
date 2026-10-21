#pragma once

#include "core/Sample.h"

#include <QColor>
#include <QWidget>

class QPainter;

namespace guiro {

void drawWaveform(QPainter& p, const Sample& s, const QRectF& r, const QColor& color, double fromFrame = 0.0,
                  double toFrame = -1.0);

class Waveform : public QWidget {
    Q_OBJECT
public:
    explicit Waveform(QWidget* parent = nullptr);
    void setSample(SamplePtr s);
    void setColor(const QColor& c) { m_color = c; update(); }

signals:
    void fileDropped(const QString& path);

protected:
    void paintEvent(QPaintEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    SamplePtr m_sample;
    QColor m_color;
};

} // namespace guiro
