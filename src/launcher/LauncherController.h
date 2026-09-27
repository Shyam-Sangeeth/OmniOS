// The launcher's one object exposed to QML — Phase 10 (OmniOS.md §12).
//
// Everything the UI can do goes through here: scan, launch, and report what
// went wrong. Games run detached and the launcher watches for the process to
// exit (Phase 9.4), because the whole point of a console shell is that
// quitting a game returns you to the grid rather than to a black screen.
#pragma once

#include <string>

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "AppListModel.h"
#include "GameListModel.h"
#include "GamepadInput.h"
#include "SystemStatus.h"

class LauncherController : public QObject {
    Q_OBJECT
    Q_PROPERTY(GameListModel* games READ games CONSTANT)
    Q_PROPERTY(AppListModel* apps READ apps CONSTANT)
    Q_PROPERTY(SystemStatus* system READ system CONSTANT)
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
    // Free space where apps install and write, as a sentence to put on screen.
    // Empty when there is plenty.
    Q_PROPERTY(QString storageNotice READ storageNotice NOTIFY storageChanged)
    // Running from the ISO, so there is something to install. The installer
    // is offered only then: on an installed system it would have nothing to
    // copy from.
    Q_PROPERTY(bool liveImage READ liveImage CONSTANT)
    // True when there is so little left that things will start failing.
    Q_PROPERTY(bool storageCritical READ storageCritical NOTIFY storageChanged)
    // An action that only root may do, waiting for the user's password. Only an
    // install whose account has a password ever asks: the live image and a
    // passwordless console account keep sudo without one.
    Q_PROPERTY(bool passwordWanted READ passwordWanted NOTIFY passwordChanged)
    Q_PROPERTY(QString passwordReason READ passwordReason NOTIFY passwordChanged)
    Q_PROPERTY(QString passwordError READ passwordError NOTIFY passwordChanged)
    Q_PROPERTY(bool passwordChecking READ passwordChecking NOTIFY passwordChanged)
    // Whether the last thing pressed was on a controller rather than a keyboard
    // or a mouse. A text box shows the on-screen keyboard only then: someone
    // holding a keyboard wants to type on it, not to steer a grid of keys.
    Q_PROPERTY(bool usingController READ usingController NOTIFY usingControllerChanged)
    // What the buttons are called on the pad last used, for anything on
    // screen that names one: {south, east, west, north, l1, r1, start, guide}.
    // ✕ ○ □ △ on a PlayStation pad, A B X Y on an Xbox one, and so on.
    Q_PROPERTY(QVariantMap buttonNames READ buttonNames NOTIFY controllerKindChanged)

public:
    explicit LauncherController(QObject* parent = nullptr);

    GameListModel* games() { return &model_; }
    AppListModel*  apps() { return &apps_; }
    SystemStatus*  system() { return &system_; }
    QString gamesPath() const;
    QString version() const;
    bool    scanning() const { return scanning_; }
    QString status() const { return status_; }
    bool    gameRunning() const { return running_ != nullptr; }
    QString runningTitle() const { return runningTitle_; }
    // A prompt waiting for a password counts: the operation behind it has been
    // asked for, and a second one started meanwhile would race it for the lock.
    bool    packageBusy() const { return package_ != nullptr || pendingRoot_.active; }
    bool    passwordWanted() const { return pendingRoot_.active; }
    QString passwordReason() const { return pendingRoot_.reason; }
    QString passwordError() const { return passwordError_; }
    bool    passwordChecking() const { return passwordCheck_ != nullptr; }
    bool    usingController() const { return usingController_; }
    QVariantMap buttonNames() const;
    QString packageStatus() const { return packageStatus_; }
    bool    ephemeral() const;
    QString storageNotice() const;
    bool    storageCritical() const;

    // Bytes free on the filesystem holding the home directory. Negative when it
    // cannot be determined, which is treated as "do not warn" rather than as
    // an emergency.
    Q_INVOKABLE qint64 freeBytes() const;

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

    // ---- managing what is installed ---------------------------------------
    // Installing is GNOME Software's job; OmniOS only launches it. What is left
    // here is what a tile's own menu should be able to do without leaving the
    // grid. All of it is a no-op while another operation is running: both
    // pacman and flatpak take a lock, and two at once fail with an error the
    // user cannot act on.

    // Removes an app. Refused for anything a system app needs — the check is in
    // the model and repeated here, so a removal cannot get through by another
    // path. Flatpaks are removed with flatpak, native apps with pacman, and
    // which one is decided by where the desktop entry came from.
    Q_INVOKABLE void removeApp(const QString& appId);

    // Reports whether a newer version exists, without changing anything.
    Q_INVOKABLE void checkForUpdate(const QString& appId);

    // Upgrades one app in place.
    Q_INVOKABLE void updateApp(const QString& appId);

    // Pairs a controller over bluetooth. Wired needs nothing; this is for the
    // wireless half, and it is in the system menu because holding the pairing
    // button is the one thing you cannot do from a controller you have not got
    // working yet.
    Q_INVOKABLE void pairController();

    // Answers the password prompt. The password is checked with sudo on its
    // own first, so a wrong one is asked for again rather than half-starting
    // the operation; then the operation runs with it on stdin, never in argv.
    Q_INVOKABLE void submitPassword(const QString& password);
    Q_INVOKABLE void cancelPassword();

    // ---- power --------------------------------------------------------------
    // "suspend", "reboot" or "poweroff". Anything else is refused rather than
    // handed to systemctl, because the argument comes from a QML string.
    Q_INVOKABLE void powerAction(const QString& action);

    // Leaves Game Mode for the Plasma desktop underneath: the launcher closes,
    // and whatever it started stays open. See omni-session-select.
    Q_INVOKABLE void switchToDesktop();

    // Opens the disk installer as an app of its own, so the Guide button can
    // come back to the library without stopping it.
    Q_INVOKABLE void installOmniOS();
    bool liveImage() const;


signals:
    void scanningChanged();
    void statusChanged();
    void gameRunningChanged();
    void packageBusyChanged();
    void packageStatusChanged();
    void storageChanged();
    void passwordChanged();
    void usingControllerChanged();
    void controllerKindChanged();
    // Asks the shell to make sure something inside it holds QML focus, before a
    // key is delivered to it.
    void focusWanted();

protected:
    // Watches the whole application's input for keys and clicks that did not
    // come from the controller, to clear usingController.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setStatus(const QString& text);
    void setUsingController(bool on);

    // Turns a controller press into the key the shell already answers to, and
    // posts it wherever focus is. One mapping, no second copy of the
    // navigation to drift out of step with the keyboard's.
    void deliverKey(int key);

    // The Guide button: back to the library from wherever you are, including
    // from inside a running game.
    void goHome();

    // Ends whatever is running and clears the state. Opening something new
    // replaces what is on screen — a console runs one thing at a time — so
    // this is called before every launch rather than refusing.
    //
    // returnHome brings the launcher back to the front; a launch that is about
    // to open something over it passes false.
    void stopRunning(bool returnHome);

    // The half of launching that is the same for every app: replace whatever is
    // running, open the new one over the launcher, and come home when it
    // exits. Games do not share it because they carry an environment and a
    // router plan of their own.
    bool startApp(const QString& title, const QString& program, const QStringList& args);

    // Runs one long system command detached, streaming to the system log, and
    // refreshes the app list when it finishes. `verb` is what to say while it
    // runs; `pastTense` what to say when it worked, or empty when the command
    // reports its own result.
    //
    // `rootReason` non-empty means the script uses sudo, spelled
    // "sudo -S -p ''" so it can take a password on stdin. When sudo would ask
    // for one, the command waits behind the password prompt, with rootReason
    // as the prompt's explanation.
    void runSystemCommand(const QString& script, const QString& verb, const QString& pastTense,
                          const QString& rootReason = QString());
    void startSystemCommand(const QString& script, const QString& verb,
                            const QString& pastTense, QByteArray password);

    void setPackageStatus(const QString& text);

    // What to hand "pacman -Qoq" to find the package behind an app.
    QString packagePathFor(const QString& appId) const;

    // Rescans when the Steam games the tab would show have changed since the
    // last scan: a game finished installing, or was uninstalled, in Steam. Checked when
    // the launcher comes back to the front and every few seconds while it is
    // there, so a download finishing behind it still gets its tile.
    void refreshIfSteamChanged();

    GamepadInput   gamepad_;
    SystemStatus   system_;
    GameListModel  model_;
    AppListModel   apps_;
    QProcess*      package_ = nullptr;
    QString        packageStatus_;

    struct PendingRoot {
        bool    active = false;
        QString script;
        QString verb;
        QString pastTense;
        QString reason;
    };
    PendingRoot    pendingRoot_;
    QString        passwordError_;
    QProcess*      passwordCheck_ = nullptr;
    // When the password was last refused, for spotting pam_faillock's lock.
    QList<qint64>  passwordFailures_;
    bool           usingController_ = false;
    QElapsedTimer  activityPinged_;
    // Steam's games as of the last scan (omnios::steamLibraryStamp).
    std::string    steamStamp_;
    QTimer         steamPoll_;
    QProcess*     running_ = nullptr;
    QString       runningTitle_;
    QString       status_;
    bool          scanning_ = false;
};
