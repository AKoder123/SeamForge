#include "MainWindow.h"
#include <QTimer>

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    // fixed-function pipeline rendering needs a compatibility context
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("SeamForge Reverse");
    MainWindow w;
    w.show();
    // automated-check flags:
    //   --open <mesh|.sfrproj>   load a file on startup
    //   --screenshot <out.png>   grab the window after 1.5s
    //   --smoke                  quit shortly after startup (exit 0 = alive)
    QString openPath, shotPath;
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        QString a = argv[i];
        if (a == "--smoke") smoke = true;
        else if (a == "--open" && i + 1 < argc) openPath = argv[++i];
        else if (a == "--screenshot" && i + 1 < argc) shotPath = argv[++i];
    }
    if (!openPath.isEmpty()) QTimer::singleShot(0, &w, [&w, openPath] { w.openPath(openPath); });
    if (!shotPath.isEmpty())
        QTimer::singleShot(1500, &w, [&w, shotPath] { w.grab().save(shotPath); });
    if (smoke || !shotPath.isEmpty())
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);
    return app.exec();
}
