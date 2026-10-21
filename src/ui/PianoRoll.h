#pragma once

#include "core/Project.h"

#include <QAbstractScrollArea>
#include <QWidget>

class QComboBox;
class QLabel;

namespace guiro {

class AudioEngine;

class PianoRollView : public QAbstractScrollArea {
    Q_OBJECT
public:
    PianoRollView(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

    void setChannel(ObjId id);
    ObjId channelId() const { return m_channel; }
    void setSnap(Tick snap) { m_snap = snap; }
    void zoomX(double factor);
    void scrollToNotes();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void scrollContentsBy(int, int) override { viewport()->update(); }

private:
    enum class Drag { None, Move, Resize, Select, Erase, Velocity, Keyboard };

    std::vector<Note>* notes() const;
    Pattern* pattern() const { return m_project->currentPattern(); }
    void updateScrollbars();
    QRectF gridRect() const;
    QRectF velRect() const;
    qreal tickToX(double t) const;
    double xToTick(qreal x) const;
    qreal keyToY(int key) const;
    int yToKey(qreal y) const;
    QRectF noteRect(const Note& n) const;
    int noteAt(QPointF p, bool* nearEnd = nullptr) const;
    Tick snapTick(double t) const;
    void commit();
    void syncSelection();
    void preview(int key, float vel);
    void stopPreview();
    void setVelocityAt(QPointF p);

    Project* m_project;
    AudioEngine* m_engine;
    ObjId m_channel = 0;
    Tick m_snap = kStepTicks;
    double m_pxPerTick = 0.9;
    int m_keyH = 15;

    std::vector<char> m_sel;
    Drag m_drag = Drag::None;
    QPointF m_pressPos;
    QPointF m_curPos;
    std::vector<Note> m_dragOrigin;
    int m_pressNote = -1;
    int m_previewKey = -1;
    int m_hoverKey = -1;
    bool m_initialScroll = false;

    static constexpr int kKeyW = 62;
    static constexpr int kRulerH = 24;
    static constexpr int kVelH = 78;
};

class PianoRoll : public QWidget {
    Q_OBJECT
public:
    PianoRoll(Project* project, AudioEngine* engine, QWidget* parent = nullptr);
    void showChannel(ObjId id);

private:
    void refreshHeader();

    Project* m_project;
    PianoRollView* m_view;
    QLabel* m_title;
    QComboBox* m_snap;
};

QString noteName(int key);

} // namespace guiro
