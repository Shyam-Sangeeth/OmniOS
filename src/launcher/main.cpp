// omni-launcher — the OmniOS shell (Phase 10, OmniOS.md §12).
//
// Boots straight into the tile grid. There is no desktop behind it and no way
// out except quitting, which is the point: this is the whole user interface.
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <cstdlib>

#include "LauncherController.h"

int main(int argc, char* argv[]) {
    // A VM has no GPU worth the name; Qt Quick's default RHI path on llvmpipe
    // is slow enough to feel broken. The software renderer draws this UI
    // perfectly well — it is tiles and text — so fall back when there is no
    // render node rather than shipping something that crawls.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") &&
        !QFile::exists(QStringLiteral("/dev/dri/renderD128"))) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("omni-launcher"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Hyprland matches this against its fullscreen window rule.
    QGuiApplication::setDesktopFileName(QStringLiteral("omni-launcher"));

    LauncherController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("GameLibrary"), controller.games());
    engine.rootContext()->setContextProperty(QStringLiteral("AppLibrary"), controller.apps());
    engine.rootContext()->setContextProperty(QStringLiteral("AppStore"), controller.store());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Resolved through the QML module rather than a hand-written qrc URL, so a
    // change to the resource layout cannot silently produce a blank screen.
    engine.loadFromModule("omnios", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    return app.exec();
}
