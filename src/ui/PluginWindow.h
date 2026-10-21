#pragma once

#include "plugins/PluginInstance.h"

#include <QWidget>
#include <vector>

class QStackedWidget;
class QPushButton;
class QSlider;
class QLabel;

namespace guiro {

class Project;

// Hosts a plugin's native editor (X11 embedding) or a generic parameter list.
class PluginWindow : public QWidget {
    Q_OBJECT
public:
    static void open(Project* project, const PluginPtr& plugin, const QString& title, QWidget* parent);
    static void closeAll();
    ~PluginWindow() override;

protected:
    void closeEvent(QCloseEvent*) override;

private:
    PluginWindow(Project* project, const PluginPtr& plugin, const QString& title, QWidget* parent);
    void showNative();
    void showGeneric();
    void buildGeneric();
    void poll();
    QString targetFor(uint32_t paramId) const;

    Project* m_project;
    std::weak_ptr<PluginInstance> m_plugin;
    PluginInstance* m_raw;
    QStackedWidget* m_stack;
    QWidget* m_host = nullptr;
    QWidget* m_generic = nullptr;
    QPushButton* m_btnNative;
    QPushButton* m_btnGeneric;
    bool m_editorOpen = false;
    struct Row {
        uint32_t id;
        QSlider* slider;
        QLabel* value;
        double min, max;
    };
    std::vector<Row> m_rows;
    int m_tick = 0;
};

} // namespace guiro
