#pragma once

#include <QWidget>

class QDockWidget;

namespace guiro {

// Slim, modern header for dock panels: icon, title, float and close.
class DockTitle : public QWidget {
    Q_OBJECT
public:
    DockTitle(QDockWidget* dock, const QString& icon);
    QSize sizeHint() const override { return {200, 36}; }
    QSize minimumSizeHint() const override { return {80, 36}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    QDockWidget* m_dock;
    QString m_icon;
};

} // namespace guiro
