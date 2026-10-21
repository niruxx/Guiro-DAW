#include "ui/widgets/Waveform.h"
#include "ui/Theme.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace guiro {

void drawWaveform(QPainter& p, const Sample& s, const QRectF& r, const QColor& color, double fromFrame, double toFrame)
{
    if (s.overviewMin.empty() || r.width() < 2)
        return;
    if (toFrame < 0)
        toFrame = double(s.frames);
    const double span = std::max(1.0, toFrame - fromFrame);
    const int w = int(r.width());
    const qreal mid = r.center().y();
    const qreal half = r.height() * 0.46;

    QPainterPath top, bottom;
    for (int x = 0; x <= w; ++x) {
        const double f0 = fromFrame + span * double(x) / double(w);
        const double f1 = fromFrame + span * double(x + 1) / double(w);
        int64_t b0 = int64_t(f0) / Sample::kOverviewStep;
        int64_t b1 = std::max<int64_t>(b0 + 1, int64_t(f1) / Sample::kOverviewStep);
        float lo = 0.f, hi = 0.f;
        for (int64_t b = b0; b < b1 && b < int64_t(s.overviewMin.size()); ++b) {
            if (b < 0)
                continue;
            lo = std::min(lo, s.overviewMin[size_t(b)]);
            hi = std::max(hi, s.overviewMax[size_t(b)]);
        }
        const qreal px = r.left() + x;
        if (x == 0) {
            top.moveTo(px, mid - hi * half);
            bottom.moveTo(px, mid - lo * half);
        } else {
            top.lineTo(px, mid - hi * half);
            bottom.lineTo(px, mid - lo * half);
        }
    }
    QPainterPath shape = top;
    shape.connectPath(bottom.toReversed());
    shape.closeSubpath();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = color;
    fill.setAlphaF(0.55f);
    p.setPen(QPen(color, 1));
    p.setBrush(fill);
    p.drawPath(shape);
    p.restore();
}

Waveform::Waveform(QWidget* parent) : QWidget(parent), m_color(theme::accent2)
{
    setMinimumHeight(60);
    setAcceptDrops(true);
}

void Waveform::setSample(SamplePtr s)
{
    m_sample = std::move(s);
    update();
}

void Waveform::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    theme::drawPanel(p, rect(), theme::bg0, 6);
    p.setPen(QPen(theme::lineSoft, 1));
    p.drawLine(QPointF(4, height() / 2.0), QPointF(width() - 4, height() / 2.0));
    if (m_sample) {
        drawWaveform(p, *m_sample, QRectF(rect()).adjusted(6, 6, -6, -6), m_color);
        p.setPen(theme::textDim);
        p.setFont(theme::uiFont(8));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignTop | Qt::AlignRight,
                   QStringLiteral("%1 s · %2 Hz").arg(m_sample->seconds(), 0, 'f', 2).arg(int(m_sample->sourceRate)));
    } else {
        p.setPen(theme::textMute);
        p.drawText(rect(), Qt::AlignCenter, tr("Drop an audio file here"));
    }
}

void Waveform::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void Waveform::dropEvent(QDropEvent* e)
{
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && SampleCache::isAudioFile(urls.first().toLocalFile()))
        emit fileDropped(urls.first().toLocalFile());
}

} // namespace guiro
