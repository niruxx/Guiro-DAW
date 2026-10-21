#pragma once

#include <QDialog>
#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QProcess;

namespace guiro {

// Manages Windows plugins through yabridge (https://github.com/robbert-vdh/yabridge):
// folders of Windows VST2 (.dll), VST3 and CLAP plugins are synced into native
// Linux plugins that Guiro then scans and hosts like any other plugin.
class WineBridgeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WineBridgeDialog(QWidget* parent = nullptr);

    static QString yabridgectl();
    static QString wine();

private:
    void refreshStatus();
    void refreshFolders();
    void addFolder();
    void removeFolder();
    void sync(bool rescanAfter);
    void runInstaller();
    void run(const QStringList& args, std::function<void(int, const QString&)> done);
    void log(const QString& text);

    QLabel* m_wineStatus;
    QLabel* m_bridgeStatus;
    QLabel* m_help;
    QListWidget* m_folders;
    QPlainTextEdit* m_log;
    QList<QPushButton*> m_needsBridge;
    QPushButton* m_installer;
    bool m_busy = false;
};

} // namespace guiro
