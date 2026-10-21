#pragma once

#include <QColor>
#include <QFont>
#include <QLinearGradient>

class QApplication;
class QPainter;
class QRectF;

namespace guiro::theme {

// Flat neutral palette, darkest to lightest, with a single accent.
inline const QColor bg0{0x0e, 0x0f, 0x11};   // app gutter, wells, displays
inline const QColor bg1{0x15, 0x16, 0x19};   // panel background
inline const QColor bg2{0x1b, 0x1c, 0x20};   // headers, cards
inline const QColor bg3{0x24, 0x25, 0x2a};   // controls
inline const QColor bg4{0x2e, 0x30, 0x36};   // hover
inline const QColor line{0x2a, 0x2b, 0x31};
inline const QColor lineSoft{0x1e, 0x1f, 0x23};

inline const QColor text{0xe9, 0xea, 0xed};
inline const QColor textDim{0x9b, 0x9d, 0xa6};
inline const QColor textMute{0x60, 0x62, 0x6b};

inline const QColor accent{0x4c, 0x8d, 0xff};    // blue
inline const QColor accentHot = accent;          // kept for API compatibility; the design is single-accent
inline const QColor accent2{0x2b, 0xc4, 0xa0};   // teal, for pan / secondary values
inline const QColor danger{0xf2, 0x54, 0x5b};
inline const QColor good{0x3c, 0xcb, 0x7f};
inline const QColor automation{0xf5, 0xa5, 0x24};

// Solid accent brush (flat design: no actual gradient).
QLinearGradient accentGradient(const QRectF& r, bool horizontal = true);

void apply(QApplication& app);

QFont uiFont(qreal pointSize = 9.5, int weight = QFont::Normal);
QFont monoFont(qreal pointSize = 10, int weight = QFont::Medium);

// Shared painting helpers.
void drawPanel(QPainter& p, const QRectF& r, const QColor& fill, qreal radius = 6.0, bool border = true);
QColor mix(const QColor& a, const QColor& b, qreal t);

} // namespace guiro::theme
