#include "core/Project.h"
#include "core/ProjectIO.h"
#include "engine/Exporter.h"
#include "engine/AudioEngine.h"
#include "plugins/PluginManager.h"
#include "ui/MainWindow.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/UiHub.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <cstdio>
#include <cstring>

using namespace guiro;

int main(int argc, char** argv)
{
    // Child-process plugin scanner: crashes here never reach the DAW.
    if (argc >= 4 && std::strcmp(argv[1], "--scan-plugin") == 0) {
        QCoreApplication app(argc, argv);
        return PluginManager::runScanner(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }

    // Headless render: guiro --render <project.guiro|demo> <out.wav|flac>
    if (argc >= 4 && std::strcmp(argv[1], "--render") == 0) {
        qputenv("GUIRO_NO_JACK", "1");
        QCoreApplication app(argc, argv);
        Project project;
        AudioEngine engine(&project);
        engine.start();
        PluginManager::instance().loadCache();
        const QString in = QString::fromLocal8Bit(argv[2]);
        QString err;
        QStringList warnings;
        if (in == QLatin1String("demo"))
            project.createDefault();
        else if (!ProjectIO::load(project, in, &err, &warnings)) {
            std::fprintf(stderr, "Could not open %s: %s\n", argv[2], qPrintable(err));
            return 1;
        }
        for (const QString& w : warnings)
            std::fprintf(stderr, "warning: %s\n", qPrintable(w));
        engine.rebuildSnapshot();
        const bool ok = renderSong(&engine, QString::fromLocal8Bit(argv[3]), [last = -1](double p) mutable {
            if (int(p * 100) != last) {
                last = int(p * 100);
                std::fprintf(stderr, "\rRendering %3d%%", last);
            }
            return true;
        }, &err);
        std::fprintf(stderr, "\n%s\n", ok ? "Done." : qPrintable(err));
        engine.shutdown();
        return ok ? 0 : 1;
    }

    // Linux plugin editors (VST3/CLAP) embed through X11. Prefer XWayland unless the
    // user explicitly opts into native Wayland (plugins then use generic editors).
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsSet("GUIRO_WAYLAND") &&
        qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Guiro"));
    QApplication::setOrganizationName(QStringLiteral("Guiro"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setDesktopFileName(QStringLiteral("guiro"));
    theme::apply(app);

    Project project;
    AudioEngine engine(&project);
    engine.start();
    UiHub::instance()->init(&project, &engine);
    PluginManager::instance().loadCache();

    MainWindow window(&project, &engine);
    const QStringList args = app.arguments();
    if (args.size() > 1 && QFileInfo(args.last()).suffix() == QLatin1String("guiro") && QFileInfo::exists(args.last()))
        window.openProject(args.last());
    else if (args.contains(QStringLiteral("--demo")))
        project.createDefault();
    else
        project.createEmpty();
    if (QSettings().contains(QStringLiteral("window/geometry")))
        window.show();
    else
        window.showMaximized();   // first run: use the whole screen, like FL
    if (const int i = args.indexOf(QStringLiteral("--panel")); i >= 0 && i + 1 < args.size())
        window.showPanel(args[i + 1]);

    const int rc = app.exec();
    PluginWindow::closeAll();
    engine.shutdown();
    return rc;
}
