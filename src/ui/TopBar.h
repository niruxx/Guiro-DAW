#pragma once

#include <QWidget>

class QMenuBar;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QAction;

namespace guiro {

class Project;
class AudioEngine;
class TransportButton;

class TopBar : public QWidget {
    Q_OBJECT
public:
    TopBar(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

    QMenuBar* menuBar() const { return m_menuBar; }
    void addPanelToggle(QAction* action, const QString& icon);
    // FL-style hint bar under the menus.
    void setHint(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void refreshPattern();
    void bindMaster();
    void showPatternMenu();
    void stepPattern(int delta);

    Project* m_project;
    AudioEngine* m_engine;
    QMenuBar* m_menuBar;
    QLabel* m_hint;
    class Knob* m_master;
    QHBoxLayout* m_toggles;
    QPushButton* m_patternButton;
    TransportButton* m_play;
    TransportButton* m_rec;
    TransportButton* m_metro;
};

} // namespace guiro
