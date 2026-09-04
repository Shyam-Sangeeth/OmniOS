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

#include "GameListModel.h"

class LauncherController : public QObject {
    Q_OBJECT
    Q_PROPERTY(GameListModel* games READ games CONSTANT)
    Q_PROPERTY(QString gamesPath READ gamesPath CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(QString runningTitle READ runningTitle NOTIFY gameRunningChanged)

public:
    explicit LauncherController(QObject* parent = nullptr);

    GameListModel* games() { return &model_; }
    QString gamesPath() const;
    QString version() const;
    bool    scanning() const { return scanning_; }
    QString status() const { return status_; }
    bool    gameRunning() const { return running_ != nullptr; }
    QString runningTitle() const { return runningTitle_; }

    // Rescans ~/Games and rewrites the cache. Safe to call repeatedly.
    Q_INVOKABLE void refresh();

    // Starts a game. Returns false and sets `status` when the router refuses —
    // a missing emulator must say which one and how to install it, not fail
    // silently.
    Q_INVOKABLE bool launch(const QString& gameId);

    // What pressing Play would run, for the detail screen.
    Q_INVOKABLE QString launchCommand(const QString& gameId) const;

    // Install hint for a game whose engine is missing; empty otherwise.
    Q_INVOKABLE QString installHint(const QString& gameId) const;

    Q_INVOKABLE void quitRunningGame();

signals:
    void scanningChanged();
    void statusChanged();
    void gameRunningChanged();

private:
    void setStatus(const QString& text);

    GameListModel model_;
    QProcess*     running_ = nullptr;
    QString       runningTitle_;
    QString       status_;
    bool          scanning_ = false;
};
