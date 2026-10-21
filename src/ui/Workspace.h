#pragma once

#include <QKeySequence>
#include <QMdiArea>
#include <QWidget>
#include <map>

class QAction;
class QLabel;
class QMdiSubWindow;
class QSettings;
class QSizeGrip;

namespace guiro {

class Workspace;

// Frame around a workspace window: slim title bar (drag to move, double-click to
// maximize), the content, and a resize grip.
class WorkspaceFrame : public QWidget {
    Q_OBJECT
public:
    WorkspaceFrame(Workspace* ws, const QString& key, const QString& title, const QString& icon, QWidget* content);
    void setActive(bool a);
    void setTitle(const QString& t);
    QMdiSubWindow* sub() const;

protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    Workspace* m_ws;
    QString m_key;
    QString m_icon;
    QWidget* m_bar;
    QLabel* m_title;
    QSizeGrip* m_grip;
    bool m_active = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressPos;
};

// FL-style workspace: tool windows float over the playlist and are toggled
// with function keys (pressing the key of the front window hides it).
class Workspace : public QMdiArea {
    Q_OBJECT
public:
    explicit Workspace(QWidget* parent = nullptr);

    QAction* addPane(const QString& key, const QString& title, const QString& icon, QWidget* content,
                     const QKeySequence& shortcut);
    QAction* action(const QString& key) const;
    void toggle(const QString& key);
    void showPane(const QString& key);
    void hidePane(const QString& key);
    void closeAll();
    void setPaneTitle(const QString& key, const QString& title);

    void saveLayout(QSettings& s) const;
    void restoreLayout(QSettings& s);
    void resetLayout();

protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Pane {
        QMdiSubWindow* sub = nullptr;
        WorkspaceFrame* frame = nullptr;
        QAction* action = nullptr;
    };
    void applyDefaultGeometry();
    std::map<QString, Pane> m_panes;
    bool m_layoutPending = true;
    bool m_layoutScheduled = false;
    bool m_restored = false;
    std::vector<QString> m_deferred;   // panes requested before the default layout ran
};

} // namespace guiro
