#include "ui/widgets/Scope.h"
#include "engine/AudioEngine.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace guiro {

Scope::Scope(QWidget* parent) : QWidget(parent)
{
    setFixedSize(96, 34);
    connect(UiHub::instance(), &UiHub::frame, this, [this] {
        if (isVisible())
            update();
    });
}

void Scope::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    theme::drawPanel(p, rect(), theme::bg2, 6, false);
    if (auto* e = UiHub::instance()->engine())
        e->readScope(m_buf, 512);

    // Trigger on a rising zero crossing for a stable picture.
    int start = 0;
    for (int i = 1; i < 256; ++i)
        if (m_buf[i - 1] <= 0.f && m_buf[i] > 0.f) {
            start = i;
            break;
        }
    const int n = 256;
    const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        const float v = std::clamp(m_buf[start + i], -1.f, 1.f);
        const QPointF pt(r.left() + r.width() * i / (n - 1), r.center().y() - v * r.height() * 0.5);
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    p.setPen(QPen(theme::accent, 1.4));
    p.drawPath(path);
}

} // namespace guiro
