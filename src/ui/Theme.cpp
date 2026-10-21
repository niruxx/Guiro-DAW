#include "ui/Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>

namespace guiro::theme {

namespace {

QString pickFamily(const QStringList& candidates, const QString& fallback)
{
    const QStringList families = QFontDatabase::families();
    for (const QString& c : candidates)
        if (families.contains(c, Qt::CaseInsensitive))
            return c;
    return fallback;
}

QString uiFamily()
{
    static const QString f = pickFamily({"Inter", "Inter Variable", "Rubik", "Manrope", "Noto Sans", "Cantarell", "DejaVu Sans"},
                                        QApplication::font().family());
    return f;
}

QString monoFamily()
{
    static const QString f = pickFamily({"JetBrains Mono", "Iosevka", "Fira Code", "Hack", "Noto Sans Mono", "DejaVu Sans Mono"},
                                        QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    return f;
}

} // namespace

QFont uiFont(qreal pt, int weight)
{
    QFont f(uiFamily());
    f.setPointSizeF(pt);
    f.setWeight(QFont::Weight(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont monoFont(qreal pt, int weight)
{
    QFont f(monoFamily());
    f.setPointSizeF(pt);
    f.setWeight(QFont::Weight(weight));
    return f;
}

QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t), float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QLinearGradient accentGradient(const QRectF& r, bool horizontal)
{
    QLinearGradient g(r.topLeft(), horizontal ? r.topRight() : r.bottomLeft());
    g.setColorAt(0, accent);
    g.setColorAt(1, accent);
    return g;
}

void drawPanel(QPainter& p, const QRectF& r, const QColor& fill, qreal radius, bool border)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(border ? QPen(line, 1.0) : Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.restore();
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette pal;
    pal.setColor(QPalette::Window, bg1);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, bg0);
    pal.setColor(QPalette::AlternateBase, bg2);
    pal.setColor(QPalette::ToolTipBase, bg3);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::PlaceholderText, textMute);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, bg3);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, accent);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, bg0);
    pal.setColor(QPalette::Link, accent2);
    pal.setColor(QPalette::Mid, line);
    pal.setColor(QPalette::Dark, bg0);
    pal.setColor(QPalette::Light, bg4);
    pal.setColor(QPalette::Disabled, QPalette::Text, textMute);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, textMute);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, textMute);
    app.setPalette(pal);
    app.setFont(uiFont());

    QString qss = QStringLiteral(R"(
        QMainWindow { background: @bg0@; }
        QMainWindow::separator { background: @bg0@; width: 4px; height: 4px; }
        QMainWindow::separator:hover { background: @accent@; }
        QToolTip { background: @bg3@; color: @text@; border: none; border-radius: 4px; padding: 5px 8px; }
        QMenuBar { background: transparent; color: @text@; }
        QMenu { background: @bg2@; color: @text@; border: 1px solid @line@; border-radius: 6px; padding: 4px; }
        QMenu::item { padding: 6px 24px 6px 10px; border-radius: 4px; }
        QMenu::item:selected { background: @bg4@; }
        QMenu::item:disabled { color: @dim@; }
        QMenu::separator { height: 1px; background: @line@; margin: 4px 6px; }
        QMenu::section { color: @mute@; font-size: 7.5pt; font-weight: 700; padding: 6px 10px 2px 10px; }
        QMenu::icon { padding-left: 6px; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
        QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
        QScrollBar::handle { background: @bg4@; border-radius: 2px; min-height: 28px; min-width: 28px; }
        QScrollBar::handle:hover { background: @mute@; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QPushButton, QToolButton { background: @bg3@; color: @text@; border: none; border-radius: 6px;
            padding: 0 12px; min-height: 28px; }
        QToolButton { padding: 0 8px; }
        QPushButton:hover, QToolButton:hover { background: @bg4@; }
        QPushButton:pressed, QToolButton:pressed { background: @bg2@; }
        QPushButton:checked, QToolButton:checked { background: @sel@; color: @accent@; }
        QPushButton:disabled, QToolButton:disabled { color: @mute@; background: @bg2@; }
        QPushButton:default { background: @accent@; color: white; }
        QPushButton:default:hover { background: @accentHover@; }
        QToolButton::menu-indicator { image: none; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
            background: @bg3@; color: @text@; border: 1px solid transparent; border-radius: 6px; padding: 0 8px;
            min-height: 28px; selection-background-color: @accent@; selection-color: white; }
        QPlainTextEdit { padding: 6px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: @accent@; }
        QComboBox:hover { background: @bg4@; }
        QComboBox::drop-down { border: none; width: 16px; }
        QComboBox QAbstractItemView { background: @bg2@; border: 1px solid @line@; border-radius: 6px; selection-background-color: @bg4@; selection-color: @text@; outline: none; padding: 4px; }
        QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
        QTreeView, QListView, QTreeWidget, QListWidget { background: transparent; color: @text@; border: none; outline: none; }
        QTreeView::item, QListView::item { padding: 4px 2px; border-radius: 4px; }
        QTreeView::item:hover, QListView::item:hover { background: @bg3@; }
        QTreeView::item:selected, QListView::item:selected { background: @sel@; color: @text@; }
        QTreeView::branch { background: transparent; }
        QHeaderView::section { background: transparent; color: @dim@; border: none; padding: 4px; }
        QTabWidget::pane { border: none; }
        QTabBar { background: @bg1@; }
        QTabBar::tab { background: transparent; color: @dim@; padding: 8px 14px; border: none; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: @text@; border-bottom: 2px solid @accent@; }
        QTabBar::tab:hover:!selected { color: @text@; }
        QDockWidget { color: @text@; }
        QSplitter::handle { background: @bg0@; }
        QProgressBar { background: @bg3@; border: none; border-radius: 4px; text-align: center; color: @text@; height: 10px; }
        QProgressBar::chunk { background: @accent@; border-radius: 4px; }
        QCheckBox { color: @text@; spacing: 6px; }
        QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; border: none; background: @bg4@; }
        QCheckBox::indicator:checked { background: @accent@; }
        QSlider::groove:horizontal { height: 4px; background: @bg4@; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
        QSlider::handle:horizontal { background: @text@; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
        QLabel { color: @text@; background: transparent; }
        QStatusBar { background: @bg0@; color: @dim@; }
        QStatusBar::item { border: none; }
        QMessageBox QLabel { color: @text@; }
    )");
    const std::pair<const char*, QColor> tokens[] = {
        {"@bg0@", bg0}, {"@bg1@", bg1}, {"@bg2@", bg2}, {"@bg3@", bg3}, {"@bg4@", bg4},
        {"@text@", text}, {"@dim@", textDim}, {"@line@", line}, {"@accent@", accent},
        {"@mute@", textMute}, {"@accentHover@", accent.lighter(112)}, {"@sel@", mix(bg3, accent, 0.22)},
    };
    for (const auto& [token, color] : tokens)
        qss.replace(QLatin1String(token), color.name());
    app.setStyleSheet(qss);
}

} // namespace guiro::theme
