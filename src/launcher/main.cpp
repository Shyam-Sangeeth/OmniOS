// omni-launcher — the OmniOS shell (Phase 10, OmniOS.md §12).
//
// Boots straight into the tile grid. There is no desktop behind it and no way
// out except quitting, which is the point: this is the whole user interface.
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <cstdlib>

#include "ControlsOverlay.h"
#include "GreeterController.h"
#include "InstallerController.h"
#include "LauncherBus.h"
#include "LauncherController.h"
#include "PadGlyphs.h"
#include "TvController.h"

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
    // and none of the launcher's work — no library scan, no controller polling of its own.
    if (QCoreApplication::arguments().contains(QStringLiteral("--install"))) {
        QGuiApplication::setApplicationName(QStringLiteral("omni-installer"));
        // The name of its desktop entry, so the desktop can find its icon and
        // the portal its app ID. And not "omni-launcher": that name is how
        // KWin is asked to raise the launcher, and the installer is a window
        // of its own.
        QGuiApplication::setDesktopFileName(QStringLiteral("omnios-install"));

        InstallerController installer;
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("pad"), new PadGlyphProvider);  // the engine owns it
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
        engine.addImageProvider(QStringLiteral("pad"), new PadGlyphProvider);  // the engine owns it
        engine.rootContext()->setContextProperty(QStringLiteral("Greeter"), &greeter);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("omnios", "GreeterWindow");
        if (engine.rootObjects().isEmpty()) return 1;
        return app.exec();
    }

    // --tv is OmniOS TV: free-to-air channels from iptv-org. A window of its
    // own, like the installer, opened from its tile on the Apps tab or from the
    // desktop's application menu.
    if (QCoreApplication::arguments().contains(QStringLiteral("--tv"))) {
        QGuiApplication::setApplicationName(QStringLiteral("omnios-tv"));
        QGuiApplication::setDesktopFileName(QStringLiteral("omnios-tv"));
        // The picture is mpv's, drawn with OpenGL into the scene (MpvItem),
        // so the window is OpenGL whatever Qt would have picked. With no GPU
        // that is Mesa's software OpenGL, which works, slowly.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

        TvController tv;
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("pad"), new PadGlyphProvider);  // the engine owns it
        engine.rootContext()->setContextProperty(QStringLiteral("Tv"), &tv);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("omnios", "TvWindow");
        if (engine.rootObjects().isEmpty()) return 1;
        return app.exec();
    }

    QGuiApplication::setApplicationName(QStringLiteral("omni-launcher"));
    // Its app id: the taskbar matches it to omni-launcher.desktop for a name and
    // icon, and omni-kwin-activate raises the launcher by it.
    QGuiApplication::setDesktopFileName(QStringLiteral("omni-launcher"));

    LauncherController controller;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("pad"), new PadGlyphProvider);  // the engine owns it
    engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("GameLibrary"), controller.games());
    engine.rootContext()->setContextProperty(QStringLiteral("RecentGames"), controller.recentGames());
    engine.rootContext()->setContextProperty(QStringLiteral("AppLibrary"), controller.apps());
    engine.rootContext()->setContextProperty(QStringLiteral("System"), controller.system());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Resolved through the QML module rather than a hand-written qrc URL, so a
    // change to the resource layout cannot silently produce a blank screen.
    engine.loadFromModule("omnios", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    // Which key is which, along the bottom of a game started from the
    // keyboard; gone once the library is in front again, a pad is used, or
    // the game has ended.
    ControlsOverlay controls(&engine);
    QObject::connect(&controller, &LauncherController::keyboardControlsWanted, &controls, &ControlsOverlay::show);
    QObject::connect(&controller, &LauncherController::keyboardControlsUnwanted, &controls, [&controls]() {
        qInfo("keyboard controls: a pad was used");
        controls.hide();
    });
    QObject::connect(&controller, &LauncherController::gameRunningChanged, &controls, [&controls, &controller]() {
        if (!controller.gameRunning()) controls.hide();
    });

    // On the session bus for KWin, which calls ShowMenu for Meta in Game
    // Mode. A second launcher (there should not be one) finds the name taken
    // and goes without.
    LauncherBus bus;
    QObject::connect(&bus, &LauncherBus::menuWanted, &controller, &LauncherController::showMenu);
    QObject::connect(&bus, &LauncherBus::systemMenuWanted, &controller, &LauncherController::showSystemMenu);
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.registerService(QStringLiteral("org.omnios.Launcher")) ||
        !session.registerObject(QStringLiteral("/Launcher"), &bus, QDBusConnection::ExportScriptableSlots))
        qWarning("omni-launcher: not on the session bus: %s", qPrintable(session.lastError().message()));

    return app.exec();
}
