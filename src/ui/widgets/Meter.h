#pragma once

#include <QWidget>
#include <atomic>

namespace guiro {

// Stereo (or mono) peak meter fed by atomics written on the audio thread.
class Meter : public QWidget {
    Q_OBJECT
public:
    explicit Meter(QWidget* parent = nullptr, Qt::Orientation o = Qt::Vertical);
    void setSources(std::atomic<float>* l, std::atomic<float>* r);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void tick();
    static float toPos(float level);

    Qt::Orientation m_orient;
    std::atomic<float>* m_l = nullptr;
    std::atomic<float>* m_r = nullptr;
    float m_level[2] = {0, 0};
    float m_hold[2] = {0, 0};
    int m_holdTimer[2] = {0, 0};
    bool m_clip = false;
};

} // namespace guiro
