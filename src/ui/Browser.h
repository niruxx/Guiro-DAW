#pragma once

#include "plugins/PluginInstance.h"

#include <QWidget>

class QComboBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeView;
class QTreeWidget;
class QPushButton;
class QMimeData;

namespace guiro {

class Project;
class AudioEngine;

inline constexpr const char* kPluginMime = "application/x-guiro-plugin";

// Decodes a plugin dragged out of the browser.
const PluginDescriptor* pluginFromMime(const QMimeData* m);

class Browser : public QWidget {
    Q_OBJECT
public:
    Browser(Project* project, AudioEngine* engine, QWidget* parent = nullptr);

signals:
    void sampleActivated(const QString& path);
    void pluginActivated(const PluginDescriptor& d);

private:
    void setPage(int i);
    void applyFilter();
    void rebuildPlugins();
    void setPlace(int idx);

    Project* m_project;
    AudioEngine* m_engine;
    QPushButton* m_tabFiles;
    QPushButton* m_tabPlugins;
    QLineEdit* m_search;
    QStackedWidget* m_stack;
    QComboBox* m_places;
    QFileSystemModel* m_fsModel;
    QTreeView* m_files;
    QTreeWidget* m_plugins;
    QLabel* m_scanLabel;
};

} // namespace guiro
