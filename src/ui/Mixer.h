#pragma once

#include <QWidget>
#include <vector>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;
class QPushButton;

namespace guiro {

class Project;
class AudioEngine;
class Knob;
class Fader;
class Meter;

class MixerStrip : public QWidget {
    Q_OBJECT
public:
    MixerStrip(Project* project, int index, QWidget* parent);
    int index() const { return m_index; }
    void setSelected(bool s);
    void refresh();

signals:
    void selected(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    Project* m_project;
    int m_index;
    bool m_selected = false;
    QPushButton* m_filterType;
    Knob* m_cutoff;
    Knob* m_res;
    Knob* m_pan;
    Fader* m_fader;
    Meter* m_meter;
    QPushButton* m_mute;
    QPushButton* m_solo;
};

class FxRack : public QWidget {
    Q_OBJECT
public:
    FxRack(Project* project, QWidget* parent);
    void setInsert(int index);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void rebuild();
    void loadInto(int slot);

    Project* m_project;
    int m_index = 0;
    QLabel* m_title;
    QVBoxLayout* m_slots;
    std::vector<QWidget*> m_slotWidgets;
};

class Mixer : public QWidget {
    Q_OBJECT
public:
    Mixer(Project* project, AudioEngine* engine, QWidget* parent = nullptr);
    void selectInsert(int index);

private:
    void rebuild();

    Project* m_project;
    QHBoxLayout* m_strips;
    std::vector<MixerStrip*> m_stripWidgets;
    FxRack* m_fx;
    int m_selected = 0;
};

} // namespace guiro
