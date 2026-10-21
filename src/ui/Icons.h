#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QPainter;
class QRectF;

namespace guiro::icons {

// Crisp vector icons drawn with QPainter (no image assets needed).
// Names: play, stop, record, metronome, pattern, song, browser, rack, piano,
// playlist, mixer, pads, plus, save, open, new, export, loop, plugin, wave,
// automation, close, gear, trash, magnet, edit, keys, undo, folder, search,
// refresh, power, instrument, effect, chevron-left, chevron-right.
QIcon icon(const QString& name, const QColor& color = QColor(), int size = 18);
void paint(QPainter& p, const QString& name, const QRectF& r, const QColor& color);

} // namespace guiro::icons
