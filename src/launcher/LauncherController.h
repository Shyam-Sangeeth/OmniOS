// The launcher's one object exposed to QML — Phase 10 (OmniOS.md §12).
//
// Everything the UI can do goes through here: scan, launch, and report what
// went wrong. Games run detached and the launcher watches for the process to
// exit (Phase 9.4), because the whole point of a console shell is that
// quitting a game returns you to the grid rather than to a black screen.
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include "AppListModel.h"
#include "GameListModel.h"
#include "StoreListModel.h"

#include "omnios/Apps.h"

class LauncherController : public QObject {
    Q_OBJECT
    Q_PROPERTY(GameListModel* games READ games CONSTANT)
    Q_PROPERTY(AppListModel* apps READ apps CONSTANT)
    Q_PROPERTY(StoreListModel* store READ store CONSTANT)
    Q_PROPERTY(QString gamesPath READ gamesPath CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(QString runningTitle READ runningTitle NOTIFY gameRunningChanged)
    Q_PROPERTY(bool packageBusy READ packageBusy NOTIFY packageBusyChanged)
    Q_PROPERTY(QString packageStatus READ packageStatus NOTIFY packageStatusChanged)
    // True when the running system is a live image, where anything installed
    // lives in a RAM overlay and is gone at the next boot. The store says so
    // rather than letting a user find out by rebooting.
    Q_PROPERTY(bool ephemeral READ ephemeral CONSTANT)

public:
    explicit LauncherController(QObject* parent = nullptr);

    GameListModel* games() { return &model_; }
    AppListModel*  apps() { return &apps_; }
    StoreListModel* store() { return &store_; }
    QString gamesPath() const;
    QString version() const;
    bool    scanning() const { return scanning_; }
    QString status() const { return status_; }
    bool    gameRunning() const { return running_ != nullptr; }
    QString runningTitle() const { return runningTitle_; }
    bool    packageBusy() const { return package_ != nullptr; }
    QString packageStatus() const { return packageStatus_; }
    bool    ephemeral() const;

    // Rescans ~/Games and rewrites the cache. Safe to call repeatedly.
    Q_INVOKABLE void refresh();

    // Starts a game. Returns false and sets `status` when the router refuses —
    // a missing emulator must say which one and how to install it, not fail
    // silently.
    Q_INVOKABLE bool launch(const QString& gameId);

    // Starts a built-in app (video player, file manager). Same contract as
    // launch(): false plus a status message when it cannot run.
    Q_INVOKABLE bool launchApp(const QString& appId);

    // What pressing Play would run, for the detail screen.
    Q_INVOKABLE QString launchCommand(const QString& gameId) const;

    // Install hint for a game whose engine is missing; empty otherwise.
    Q_INVOKABLE QString installHint(const QString& gameId) const;

    Q_INVOKABLE void quitRunningGame();

    // ---- store ------------------------------------------------------------
    // All three take a tile's app id and are no-ops while another package
    // operation is running: pacman takes a lock, and two of these at once would
    // simply fail with a database error the user cannot act on.

    // Installs a catalogue app.
    Q_INVOKABLE void installApp(const QString& appId);

    // Removes an app, built-in or catalogue. Refused for any package a system
    // app needs — the check is here and not in the UI, so a removal cannot get
    // through by some other path.
    Q_INVOKABLE void removeApp(const QString& appId);

    // Reports whether a newer version exists, without touching the installed
    // system. Uses checkupdates, which compares against a throwaway database.
    Q_INVOKABLE void checkForUpdate(const QString& appId);

    // Upgrades one app in place.
    Q_INVOKABLE void updateApp(const QString& appId);


signals:
    void scanningChanged();
    void statusChanged();
    void gameRunningChanged();
    void packageBusyChanged();
    void packageStatusChanged();

private:
    void setStatus(const QString& text);

    // Ends whatever is running and clears the state. Opening something new
    // replaces what is on screen — a console runs one thing at a time — so
    // this is called before every launch rather than refusing.
    //
    // returnHome switches back to the launcher's workspace; a launch that is
    // about to switch to the app workspace itself passes false.
    void stopRunning(bool returnHome);

    // A catalogue app installed from the store, launched from the Apps tab.
    bool launchExtra(const omnios::StoreApp& app);

    // The half of launching that is the same for every app: replace whatever is
    // running, put the new window on the app workspace, and come home when it
    // exits. Games do not share it because they carry an environment and a
    // router plan of their own.
    bool startApp(const QString& title, const QString& program, const QStringList& args);

    // Package name behind an app id, from either registry, plus its display
    // name. Empty package means the id is unknown.
    struct PackageRef {
        QString package;
        QString title;
    };
    PackageRef packageFor(const QString& appId) const;

    // Runs one pacman operation detached, streaming to the package log, and
    // refreshes both models when it finishes. `verb` is what to say while it
    // runs; `pastTense` what to say when it worked.
    void runPackageCommand(const QString& script, const QString& verb, const QString& pastTense);

    void setPackageStatus(const QString& text);

    GameListModel  model_;
    AppListModel   apps_;
    StoreListModel store_;
    QProcess*      package_ = nullptr;
    QString        packageStatus_;
    QProcess*     running_ = nullptr;
    QString       runningTitle_;
    QString       status_;
    bool          scanning_ = false;
};
