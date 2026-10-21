#pragma once

#include "core/Types.h"

#include <QWidget>
#include <vector>

class QComboBox;
class QScrollArea;
class QVBoxLayout;

namespace guiro {

class Project;
class AudioEngine;
class Knob;
struct Channel;

class ChannelRow : public QWidget {
    Q_OBJECT
public:
    ChannelRow(Project* project, AudioEngine* engine, ObjId channel, QWidget* parent);
    ObjId channelId() const { return m_id; }
    void refresh();

    static constexpr int kHeight = 36;
    static constexpr int kStepsX = 262;
    static constexpr int kStepW = 21;
    static constexpr int kStepGap = 3;
    static int stepX(int i) { return kStepsX + i * (kStepW + kStepGap) + (i / 4) * 6; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    Channel* channel() const;
    int stepAt(QPointF p) const;
    QRectF nameRect() const { return QRectF(118, 5, 136, kHeight - 10); }
    QRectF ledRect() const { return QRectF(8, kHeight / 2.0 - 6, 12, 12); }
    QRectF insertRect() const { return QRectF(84, 8, 28, kHeight - 16); }
    bool showsPreview() const;
    void showMenu(const QPoint& global);
    void setStep(int i, bool on);
    void tickFrame();

    Project* m_project;
    AudioEngine* m_engine;
    ObjId m_id;
    Knob* m_pan;
    Knob* m_vol;
    int m_paintMode = -1;   // 1 = painting on, 0 = erasing
    int m_hoverStep = -1;
    int m_playStep = -1;
    float m_activity = 0.f;
    bool m_insertDrag = false;
    qreal m_insertPressY = 0;
    int m_insertPressVal = 0;
};

class ChannelRack : public QWidget {
    Q_OBJECT
public:
    ChannelRack(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void rebuild();
    void refreshRows();
    void showAddMenu();
    void syncSteps();

    Project* m_project;
    AudioEngine* m_engine;
    QScrollArea* m_scroll;
    QWidget* m_body;
    QWidget* m_empty = nullptr;
    QVBoxLayout* m_rows;
    QComboBox* m_steps;
    Knob* m_swing;
    std::vector<ChannelRow*> m_rowWidgets;
};

void addPluginChannel(Project* project, const struct PluginDescriptor& d, QWidget* parent);

} // namespace guiro
