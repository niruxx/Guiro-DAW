#pragma once

#include "core/Project.h"

#include <QAbstractScrollArea>
#include <QWidget>

class QComboBox;

namespace guiro {

class AudioEngine;

inline constexpr const char* kClipMime = "application/x-guiro-clip";

struct Brush {
    ClipKind kind = ClipKind::Pattern;
    ObjId ref = 0;
};

// Left-hand list of everything that can be placed in the playlist.
class ClipPicker : public QWidget {
    Q_OBJECT
public:
    ClipPicker(Project* project, QWidget* parent = nullptr);
    Brush brush() const { return m_brush; }
    void setBrush(Brush b);

signals:
    void brushChanged(Brush b);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    struct Item {
        ClipKind kind;
        ObjId ref;
        QString name;
        QColor color;
        bool header = false;
    };
    void rebuild();
    int itemAt(QPointF p) const;

    Project* m_project;
    std::vector<Item> m_items;
    Brush m_brush;
    QPointF m_pressPos;
    int m_pressItem = -1;
    static constexpr int kRowH = 26;
};

class PlaylistView : public QAbstractScrollArea {
    Q_OBJECT
public:
    PlaylistView(Project* project, AudioEngine* engine, ClipPicker* picker, QWidget* parent = nullptr);
    void setSnap(Tick s) { m_snap = s; }
    void zoomX(double f);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void scrollContentsBy(int, int) override { viewport()->update(); }

private:
    enum class Drag { None, Move, ResizeRight, ResizeLeft, Erase, Ruler, Point, TrackMute };

    void updateScrollbars();
    QRectF gridRect() const;
    qreal tickToX(double t) const;
    double xToTick(qreal x) const;
    qreal trackToY(int track) const;
    int yToTrack(qreal y) const;
    QRectF clipRect(const Clip& c) const;
    int clipAt(QPointF p) const;
    Tick snapTick(double t) const;
    QString clipName(const Clip& c) const;
    QColor clipColor(const Clip& c) const;
    void drawClip(QPainter& p, const Clip& c, const QRectF& r, bool selected);
    int automationPointAt(const Clip& c, QPointF pos) const;
    void followPlayhead();

    Project* m_project;
    AudioEngine* m_engine;
    ClipPicker* m_picker;
    Tick m_snap = kBeatTicks;
    double m_pxPerTick = 0.26;
    int m_trackH = 54;

    Drag m_drag = Drag::None;
    QPointF m_pressPos;
    ObjId m_dragClip = 0;
    Clip m_origin;
    std::vector<ObjId> m_selected;
    int m_dragPoint = -1;
    double m_rulerStart = 0;
    bool m_rulerMoved = false;

    static constexpr int kHeaderW = 150;
    static constexpr int kRulerH = 28;
};

class Playlist : public QWidget {
    Q_OBJECT
public:
    Playlist(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

private:
    PlaylistView* m_view;
    ClipPicker* m_picker;
    QComboBox* m_snap;
};

} // namespace guiro
