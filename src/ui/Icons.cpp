#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace guiro::icons {

void paint(QPainter& p, const QString& name, const QRectF& r, const QColor& color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(r.topLeft());
    const qreal s = std::min(r.width(), r.height()) / 24.0;
    p.scale(s, s);
    QPen pen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    auto filled = [&](const QPainterPath& path) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(path);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    };

    if (name == "play") {
        QPainterPath path;
        path.moveTo(7, 4.5);
        path.lineTo(19.5, 12);
        path.lineTo(7, 19.5);
        path.closeSubpath();
        p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(color);
        p.drawPath(path);
    } else if (name == "stop") {
        QPainterPath path;
        path.addRoundedRect(QRectF(6, 6, 12, 12), 2.2, 2.2);
        filled(path);
    } else if (name == "record") {
        QPainterPath path;
        path.addEllipse(QPointF(12, 12), 6.5, 6.5);
        filled(path);
    } else if (name == "metronome") {
        QPainterPath path;
        path.moveTo(8, 20);
        path.lineTo(10.5, 4);
        path.lineTo(13.5, 4);
        path.lineTo(16, 20);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(12, 15), QPointF(18, 7));
        p.drawLine(QPointF(7, 20), QPointF(17, 20));
    } else if (name == "pattern") {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 2; ++j) {
                QPainterPath path;
                path.addRoundedRect(QRectF(3.5 + i * 4.5, 7 + j * 6, 3.4, 4.2), 1, 1);
                if ((i + j) % 2 == 0)
                    filled(path);
                else
                    p.drawPath(path);
            }
    } else if (name == "song" || name == "playlist") {
        p.drawLine(QPointF(4, 6), QPointF(20, 6));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawLine(QPointF(4, 18), QPointF(20, 18));
        QPainterPath a, b, c;
        a.addRoundedRect(QRectF(5, 4, 7, 4), 1.2, 1.2);
        b.addRoundedRect(QRectF(10, 10, 8, 4), 1.2, 1.2);
        c.addRoundedRect(QRectF(6, 16, 6, 4), 1.2, 1.2);
        filled(a);
        filled(b);
        filled(c);
    } else if (name == "browser" || name == "folder") {
        QPainterPath path;
        path.moveTo(3.5, 7);
        path.lineTo(3.5, 18.5);
        path.lineTo(20.5, 18.5);
        path.lineTo(20.5, 8.5);
        path.lineTo(11.5, 8.5);
        path.lineTo(9.5, 5.5);
        path.lineTo(4.5, 5.5);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == "rack") {
        for (int r = 0; r < 4; ++r) {
            p.drawEllipse(QPointF(5, 5.5 + r * 4.4), 1.2, 1.2);
            for (int c = 0; c < 3; ++c) {
                QPainterPath path;
                path.addRoundedRect(QRectF(9 + c * 4, 4 + r * 4.4, 3, 3), 0.8, 0.8);
                if ((r + c) % 3 == 0)
                    filled(path);
                else
                    p.drawPath(path);
            }
        }
    } else if (name == "piano" || name == "keys") {
        p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2);
        for (int i = 1; i < 4; ++i)
            p.drawLine(QPointF(3.5 + i * 4.25, 12.5), QPointF(3.5 + i * 4.25, 19));
        for (qreal x : {6.6, 10.85, 15.1}) {
            QPainterPath k;
            k.addRect(QRectF(x, 5, 2.4, 7.5));
            filled(k);
        }
    } else if (name == "mixer") {
        for (int i = 0; i < 3; ++i) {
            const qreal x = 6 + i * 6;
            p.drawLine(QPointF(x, 4), QPointF(x, 20));
            const qreal y = (i == 0 ? 14 : i == 1 ? 8 : 16);
            QPainterPath kn;
            kn.addRoundedRect(QRectF(x - 2.5, y - 1.8, 5, 3.6), 1, 1);
            filled(kn);
        }
    } else if (name == "pads") {
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) {
                QPainterPath path;
                path.addRoundedRect(QRectF(4 + c * 8.5, 4 + r * 8.5, 7, 7), 1.8, 1.8);
                if (r == 0 && c == 1)
                    filled(path);
                else
                    p.drawPath(path);
            }
    } else if (name == "plus") {
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (name == "close") {
        p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
        p.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
    } else if (name == "save") {
        QPainterPath path;
        path.moveTo(5, 4.5);
        path.lineTo(16, 4.5);
        path.lineTo(19.5, 8);
        path.lineTo(19.5, 19.5);
        path.lineTo(4.5, 19.5);
        path.lineTo(4.5, 5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawRect(QRectF(8, 4.5, 7, 4.5));
        p.drawRect(QRectF(7.5, 13, 9, 6.5));
    } else if (name == "open") {
        QPainterPath path;
        path.moveTo(3.5, 18.5);
        path.lineTo(3.5, 5.5);
        path.lineTo(9, 5.5);
        path.lineTo(11, 8);
        path.lineTo(18, 8);
        path.lineTo(18, 11);
        p.drawPath(path);
        QPainterPath lid;
        lid.moveTo(3.5, 18.5);
        lid.lineTo(7, 11);
        lid.lineTo(21, 11);
        lid.lineTo(17.5, 18.5);
        lid.closeSubpath();
        p.drawPath(lid);
    } else if (name == "new") {
        QPainterPath path;
        path.moveTo(6, 3.5);
        path.lineTo(14, 3.5);
        path.lineTo(18.5, 8);
        path.lineTo(18.5, 20.5);
        path.lineTo(6, 20.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(12.2, 10), QPointF(12.2, 17));
        p.drawLine(QPointF(8.7, 13.5), QPointF(15.7, 13.5));
    } else if (name == "export") {
        p.drawLine(QPointF(12, 4), QPointF(12, 15));
        p.drawLine(QPointF(7.5, 8.5), QPointF(12, 4));
        p.drawLine(QPointF(16.5, 8.5), QPointF(12, 4));
        QPainterPath tray;
        tray.moveTo(4.5, 14);
        tray.lineTo(4.5, 19.5);
        tray.lineTo(19.5, 19.5);
        tray.lineTo(19.5, 14);
        p.drawPath(tray);
    } else if (name == "loop") {
        QPainterPath path;
        path.moveTo(17, 7);
        path.lineTo(7.5, 7);
        path.cubicTo(4.5, 7, 4, 10, 4, 12);
        p.drawPath(path);
        QPainterPath b;
        b.moveTo(7, 17);
        b.lineTo(16.5, 17);
        b.cubicTo(19.5, 17, 20, 14, 20, 12);
        p.drawPath(b);
        p.drawLine(QPointF(14.5, 4.5), QPointF(17, 7));
        p.drawLine(QPointF(14.5, 9.5), QPointF(17, 7));
        p.drawLine(QPointF(9.5, 14.5), QPointF(7, 17));
        p.drawLine(QPointF(9.5, 19.5), QPointF(7, 17));
    } else if (name == "plugin" || name == "instrument" || name == "effect") {
        if (name == "instrument") {
            p.drawRoundedRect(QRectF(4, 6, 16, 12), 2.5, 2.5);
            p.drawEllipse(QPointF(9, 12), 2.2, 2.2);
            p.drawLine(QPointF(14, 10), QPointF(17, 10));
            p.drawLine(QPointF(14, 14), QPointF(17, 14));
        } else if (name == "effect") {
            QPainterPath path;
            path.moveTo(3, 12);
            path.cubicTo(6, 4, 8, 4, 10, 12);
            path.cubicTo(12, 20, 14, 20, 16, 12);
            path.cubicTo(17, 8, 19, 8, 21, 12);
            p.drawPath(path);
        } else {
            p.drawRoundedRect(QRectF(5, 8, 14, 11), 2, 2);
            p.drawLine(QPointF(9, 4), QPointF(9, 8));
            p.drawLine(QPointF(15, 4), QPointF(15, 8));
        }
    } else if (name == "wave") {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i) {
            const qreal x = 3 + i * 18.0 / 40.0;
            const qreal y = 12 + std::sin(i * 0.55) * (7.0 * std::exp(-std::abs(i - 20) / 14.0));
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        p.drawPath(path);
    } else if (name == "automation") {
        QPainterPath path;
        path.moveTo(3, 18);
        path.lineTo(9, 9);
        path.lineTo(14, 14);
        path.lineTo(21, 5);
        p.drawPath(path);
        p.setBrush(color);
        for (QPointF pt : {QPointF(9, 9), QPointF(14, 14)})
            p.drawEllipse(pt, 1.8, 1.8);
    } else if (name == "gear") {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4.0;
            p.drawLine(QPointF(12 + std::cos(a) * 6, 12 + std::sin(a) * 6), QPointF(12 + std::cos(a) * 8.5, 12 + std::sin(a) * 8.5));
        }
        p.drawEllipse(QPointF(12, 12), 6, 6);
    } else if (name == "trash") {
        p.drawLine(QPointF(4.5, 7), QPointF(19.5, 7));
        p.drawLine(QPointF(9.5, 7), QPointF(10.5, 4.5));
        p.drawLine(QPointF(14.5, 7), QPointF(13.5, 4.5));
        p.drawLine(QPointF(10.5, 4.5), QPointF(13.5, 4.5));
        QPainterPath b;
        b.moveTo(6.5, 7);
        b.lineTo(7.5, 20);
        b.lineTo(16.5, 20);
        b.lineTo(17.5, 7);
        p.drawPath(b);
    } else if (name == "magnet") {
        QPainterPath path;
        path.moveTo(6, 4);
        path.lineTo(6, 12);
        path.cubicTo(6, 20, 18, 20, 18, 12);
        path.lineTo(18, 4);
        p.drawPath(path);
        p.drawLine(QPointF(6, 8), QPointF(9.5, 8));
        p.drawLine(QPointF(14.5, 8), QPointF(18, 8));
        p.drawLine(QPointF(9.5, 4), QPointF(9.5, 12));
        p.drawLine(QPointF(14.5, 4), QPointF(14.5, 12));
    } else if (name == "edit") {
        QPainterPath path;
        path.moveTo(5, 19);
        path.lineTo(5.5, 15);
        path.lineTo(15.5, 5);
        path.lineTo(19, 8.5);
        path.lineTo(9, 18.5);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == "search") {
        p.drawEllipse(QPointF(10.5, 10.5), 5.5, 5.5);
        p.drawLine(QPointF(14.5, 14.5), QPointF(19.5, 19.5));
    } else if (name == "refresh") {
        QPainterPath path;
        path.arcMoveTo(QRectF(5, 5, 14, 14), 60);
        path.arcTo(QRectF(5, 5, 14, 14), 60, 290);
        p.drawPath(path);
        p.drawLine(QPointF(15.5, 6.2), QPointF(18.8, 6.5));
        p.drawLine(QPointF(15.5, 6.2), QPointF(16.6, 3.3));
    } else if (name == "power") {
        QPainterPath path;
        path.arcMoveTo(QRectF(5, 5, 14, 14), 120);
        path.arcTo(QRectF(5, 5, 14, 14), 120, 300);
        p.drawPath(path);
        p.drawLine(QPointF(12, 3.5), QPointF(12, 11));
    } else if (name == "chevron-left") {
        p.drawLine(QPointF(14.5, 6), QPointF(8.5, 12));
        p.drawLine(QPointF(8.5, 12), QPointF(14.5, 18));
    } else if (name == "chevron-right") {
        p.drawLine(QPointF(9.5, 6), QPointF(15.5, 12));
        p.drawLine(QPointF(15.5, 12), QPointF(9.5, 18));
    } else if (name == "menu") {
        p.drawLine(QPointF(5, 7), QPointF(19, 7));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        p.drawLine(QPointF(5, 17), QPointF(15, 17));
    } else if (name == "undo") {
        QPainterPath path;
        path.moveTo(8, 9);
        path.lineTo(15, 9);
        path.cubicTo(22, 9, 22, 19, 15, 19);
        path.lineTo(9, 19);
        p.drawPath(path);
        p.drawLine(QPointF(8, 9), QPointF(11.5, 5.5));
        p.drawLine(QPointF(8, 9), QPointF(11.5, 12.5));
    }
    p.restore();
}

QIcon icon(const QString& name, const QColor& colorIn, int size)
{
    const QColor color = colorIn.isValid() ? colorIn : theme::textDim;
    const QString key = name + color.name(QColor::HexArgb) + QString::number(size);
    static QHash<QString, QIcon> cache;
    if (auto it = cache.find(key); it != cache.end())
        return it.value();

    QIcon ic;
    for (int scale : {1, 2}) {
        for (QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Disabled, QIcon::Selected}) {
            QPixmap pm(size * scale, size * scale);
            pm.setDevicePixelRatio(scale);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            QColor c = color;
            if (mode == QIcon::Active)
                c = color.lighter(130);
            else if (mode == QIcon::Disabled)
                c = theme::textMute;
            else if (mode == QIcon::Selected)
                c = theme::accent;
            paint(p, name, QRectF(0, 0, size, size), c);
            p.end();
            ic.addPixmap(pm, mode, QIcon::Off);
            if (mode == QIcon::Normal) {
                QPixmap on(size * scale, size * scale);
                on.setDevicePixelRatio(scale);
                on.fill(Qt::transparent);
                QPainter po(&on);
                paint(po, name, QRectF(0, 0, size, size), theme::accent);
                po.end();
                ic.addPixmap(on, QIcon::Normal, QIcon::On);
            }
        }
    }
    cache.insert(key, ic);
    return ic;
}

} // namespace guiro::icons
