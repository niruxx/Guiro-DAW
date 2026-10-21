#pragma once

#include "plugins/PluginInstance.h"

#include <QList>
#include <QObject>
#include <QStringList>

class QProcess;

namespace guiro {

// Discovers VST3 / CLAP / LV2 plugins. Binary formats are probed in a child
// process (`guiro --scan-plugin <format> <path>`) so a crashing plugin can
// never take the DAW down with it. Results are cached in ~/.config/Guiro.
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager& instance();

    const QList<PluginDescriptor>& plugins() const { return m_plugins; }
    const PluginDescriptor* find(PluginFormat f, const QString& uid) const;
    QStringList blacklist() const { return m_failed; }

    void loadCache();
    void rescan();
    bool isScanning() const { return m_scanning; }

    PluginPtr create(const PluginDescriptor& d, QString* error);

    static QStringList searchPaths(PluginFormat f);
    static QStringList supportedFormats();
    // True for Windows plugins exposed to Linux by yabridge.
    static bool isWineBridged(const QString& path);

    // Entry point for the scanner child process; prints one JSON object per line.
    static int runScanner(const QString& format, const QString& path);

signals:
    void pluginsChanged();
    void scanProgress(int done, int total, const QString& current);
    void scanFinished();

private:
    PluginManager() = default;
    void saveCache();
    void startNext();
    QString cachePath() const;

    QList<PluginDescriptor> m_plugins;
    QStringList m_failed;
    bool m_scanning = false;

    struct Job {
        PluginFormat format;
        QString path;
    };
    QList<Job> m_queue;
    QList<PluginDescriptor> m_found;
    int m_total = 0;
    int m_done = 0;
    int m_running = 0;
};

} // namespace guiro
