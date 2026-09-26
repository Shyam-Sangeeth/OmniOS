// omni-launcher — the OmniOS shell (Phase 10, OmniOS.md §12).
//
// Boots straight into the tile grid. There is no desktop behind it and no way
// out except quitting, which is the point: this is the whole user interface.
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <cstdlib>

#include "GreeterController.h"
#include "InstallerController.h"
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
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // --install is the disk installer (Phase 13.2): the same binary, so it has
    // the launcher's look and its controller support, but a different window
    // and none of the launcher's work — no library scan, no Hyprland socket.
    if (QCoreApplication::arguments().contains(QStringLiteral("--install"))) {
        QGuiApplication::setApplicationName(QStringLiteral("omni-installer"));
        // The name of its desktop entry, so the desktop can find its icon and
        // the portal its app ID. And not "omni-launcher": that name is how
        // Hyprland recognises the launcher, and the launcher moves every other
        // window off its workspace. The installer has to be one of those.
        QGuiApplication::setDesktopFileName(QStringLiteral("omnios-install"));

        InstallerController installer;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("Installer"), &installer);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("omnios", "InstallerWindow");
        if (engine.rootObjects().isEmpty()) return 1;
        return app.exec();
    }

    // --greeter is the sign-in screen greetd shows when a machine was installed
    // with "sign in automatically" off. Same binary for the same reasons as
    // the installer; it runs as greetd's unprivileged greeter user, under cage.
    if (QCoreApplication::arguments().contains(QStringLiteral("--greeter"))) {
        QGuiApplication::setApplicationName(QStringLiteral("omnios-greeter"));
        QGuiApplication::setDesktopFileName(QStringLiteral("omnios-greeter"));

        GreeterController greeter;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("Greeter"), &greeter);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("omnios", "GreeterWindow");
        if (engine.rootObjects().isEmpty()) return 1;
        return app.exec();
    }

    QGuiApplication::setApplicationName(QStringLiteral("omni-launcher"));
    // Hyprland matches this against its fullscreen window rule.
    QGuiApplication::setDesktopFileName(QStringLiteral("omni-launcher"));

    LauncherController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("GameLibrary"), controller.games());
    engine.rootContext()->setContextProperty(QStringLiteral("AppLibrary"), controller.apps());
    engine.rootContext()->setContextProperty(QStringLiteral("System"), controller.system());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Resolved through the QML module rather than a hand-written qrc URL, so a
    // change to the resource layout cannot silently produce a blank screen.
    engine.loadFromModule("omnios", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    return app.exec();
}
