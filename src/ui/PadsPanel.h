#pragma once

#include "core/Types.h"

#include <QWidget>
#include <array>

class QComboBox;
class QLabel;

namespace guiro {

class Project;
class AudioEngine;
class Knob;
class Waveform;
struct Channel;

class PadWidget : public QWidget {
    Q_OBJECT
public:
    PadWidget(int index, QWidget* parent);
    void setInfo(const QString& name, const QColor& color, bool loaded, const QString& key);
    void setSelected(bool s);
    void flash(float velocity);

signals:
    void pressed(int index, float velocity);
    void released(int index);
    void filesDropped(int index, const QStringList& paths);
    void menuRequested(int index, const QPoint& global);
    void dragOut(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    int m_index;
    QString m_name;
    QString m_key;
    QColor m_color;
    bool m_loaded = false;
    bool m_selected = false;
    bool m_dropHover = false;
    float m_glow = 0.f;
    bool m_down = false;
    QPointF m_pressPos;
};

class PadsPanel : public QWidget {
    Q_OBJECT
public:
    PadsPanel(Project* project, AudioEngine* engine, QWidget* parent = nullptr);
    void showChannel(ObjId id);

protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

private:
    Channel* channel() const;
    void rebuildBanks();
    void refresh();
    void hit(int pad, float velocity);
    void release(int pad);
    void selectPad(int pad);
    void loadFiles(int pad, const QStringList& files);
    void padMenu(int pad, const QPoint& global);
    void recordHit(int pad, float velocity);

    Project* m_project;
    AudioEngine* m_engine;
    ObjId m_channel = 0;
    int m_selectedPad = 0;
    QComboBox* m_banks;
    std::array<PadWidget*, kNumPads> m_pads{};
    QLabel* m_padTitle;
    Waveform* m_wave;
    Knob* m_gain;
    Knob* m_pitch;
};

} // namespace guiro
