#include "ui/widgets/Meter.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QPainter>
#include <cmath>

namespace guiro {

Meter::Meter(QWidget* parent, Qt::Orientation o) : QWidget(parent), m_orient(o)
{
    if (o == Qt::Vertical)
        setFixedWidth(12);
    else
        setFixedHeight(8);
    connect(UiHub::instance(), &UiHub::frame, this, &Meter::tick);
}

void Meter::setSources(std::atomic<float>* l, std::atomic<float>* r)
{
    m_l = l;
    m_r = r;
}

float Meter::toPos(float level)
{
    // -60 dB .. +6 dB mapped to 0..1
    if (level <= 0.001f)
        return 0.f;
    const float db = 20.f * std::log10(level);
    return std::clamp((db + 60.f) / 66.f, 0.f, 1.f);
}

void Meter::tick()
{
    if (!isVisible())
        return;
    std::atomic<float>* src[2] = {m_l, m_r ? m_r : m_l};
    bool changed = false;
    for (int c = 0; c < 2; ++c) {
        const float v = src[c] ? src[c]->exchange(0.f, std::memory_order_relaxed) : 0.f;
        if (v > 1.f)
            m_clip = true;
        const float nl = std::max(v, m_level[c] * 0.86f);
        if (std::abs(nl - m_level[c]) > 1e-5f)
            changed = true;
        m_level[c] = nl < 1e-4f ? 0.f : nl;
        if (v >= m_hold[c]) {
            m_hold[c] = v;
            m_holdTimer[c] = 50;
        } else if (--m_holdTimer[c] <= 0) {
            m_hold[c] *= 0.92f;
            changed = true;
        }
    }
    if (changed)
        update();
}

void Meter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF all = rect();
    p.fillRect(all, theme::bg0);
    const int channels = 2;
    const bool vert = m_orient == Qt::Vertical;
    const qreal gap = 1;
    for (int c = 0; c < channels; ++c) {
        QRectF lane;
        if (vert) {
            const qreal w = (all.width() - gap) / channels;
            lane = QRectF(all.left() + c * (w + gap), all.top(), w, all.height());
        } else {
            const qreal h = (all.height() - gap) / channels;
            lane = QRectF(all.left(), all.top() + c * (h + gap), all.width(), h);
        }
        const float pos = toPos(m_level[c]);
        // Flat zones: normal, hot (above -6 dB), clipping (above 0 dB).
        const float hot = toPos(0.5f), clip = toPos(1.f);
        auto zone = [&](float a, float b, const QColor& col) {
            if (pos <= a)
                return;
            const float e = std::min(pos, b);
            const QRectF z = vert ? QRectF(lane.left(), lane.bottom() - lane.height() * e, lane.width(), lane.height() * (e - a))
                                  : QRectF(lane.left() + lane.width() * a, lane.top(), lane.width() * (e - a), lane.height());
            p.fillRect(z, col);
        };
        zone(0.f, hot, theme::accent2);
        zone(hot, clip, theme::automation);
        zone(clip, 1.f, theme::danger);
        const float hp = toPos(m_hold[c]);
        if (hp > 0.f) {
            p.setPen(QPen(hp > 0.909f ? theme::danger : theme::text, 1));
            if (vert) {
                const qreal y = lane.bottom() - lane.height() * hp;
                p.drawLine(QPointF(lane.left(), y), QPointF(lane.right(), y));
            } else {
                const qreal x = lane.left() + lane.width() * hp;
                p.drawLine(QPointF(x, lane.top()), QPointF(x, lane.bottom()));
            }
        }
    }
    // 0 dB marker
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    if (vert) {
        const qreal y = all.bottom() - all.height() * (60.f / 66.f);
        p.drawLine(QPointF(all.left(), y), QPointF(all.right(), y));
    }
}

} // namespace guiro
