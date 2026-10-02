// The launcher's one object exposed to QML — Phase 10 (OmniOS.md §12).
//
// Everything the UI can do goes through here: scan, launch, and report what
// went wrong. Games run detached and the launcher watches for the process to
// exit (Phase 9.4), because the whole point of a console shell is that
// quitting a game returns you to the grid rather than to a black screen.
#pragma once

#include <cstdint>
#include <string>

#include <QElapsedTimer>
#include <QObject>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "AppListModel.h"
#include "CoverFetcher.h"
#include "GameListModel.h"
#include "GamepadInput.h"
#include "NotificationWatcher.h"
#include "RecentGamesModel.h"
#include "SystemStatus.h"

class LauncherController : public QObject {
    Q_OBJECT
    Q_PROPERTY(GameListModel* games READ games CONSTANT)
    Q_PROPERTY(RecentGamesModel* recentGames READ recentGames CONSTANT)
    Q_PROPERTY(AppListModel* apps READ apps CONSTANT)
    Q_PROPERTY(SystemStatus* system READ system CONSTANT)
    Q_PROPERTY(QString gamesPath READ gamesPath CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    // The running game can be saved and loaded from the game menu: one in
    // RetroArch that this launcher started, so it holds RetroArch's input.
    Q_PROPERTY(bool canSaveState READ canSaveState NOTIFY gameRunningChanged)
    Q_PROPERTY(QString runningTitle READ runningTitle NOTIFY gameRunningChanged)
    // The library id of the game running now; empty for none, or for an app.
    Q_PROPERTY(QString runningGameId READ runningGameId NOTIFY gameRunningChanged)
    // How many packages and apps a system update would change; -1 until a
    // check has answered (or on the live image, which is never updated).
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged)
    // An update replaced the running kernel; a restart finishes it.
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY updatesChanged)
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
    // Notifications that stay on screen until closed; the system menu offers
    // to close them (NotificationWatcher.h).
    Q_PROPERTY(int standingNotifications READ standingNotifications NOTIFY standingNotificationsChanged)

public:
    explicit LauncherController(QObject* parent = nullptr);

    GameListModel* games() { return &model_; }
    RecentGamesModel* recentGames() { return &recent_; }
    AppListModel*  apps() { return &apps_; }
    SystemStatus*  system() { return &system_; }
    QString gamesPath() const;
    QString version() const;
    bool    scanning() const { return scanning_; }
    QString status() const { return status_; }
    bool    gameRunning() const {
        return running_ != nullptr || !steamGame_.appId.isEmpty() || adopted_.pid != 0;
    }
    QString runningTitle() const;
    QString runningGameId() const;
    int     updateCount() const { return updateCount_; }
    bool    restartRequired() const { return restartRequired_; }
    // A prompt waiting for a password counts: the operation behind it has been
    // asked for, and a second one started meanwhile would race it for the lock.
    bool    packageBusy() const { return package_ != nullptr || pendingRoot_.active; }
    bool    passwordWanted() const { return pendingRoot_.active; }
    QString passwordReason() const { return pendingRoot_.reason; }
    QString passwordError() const { return passwordError_; }
    bool    passwordChecking() const { return passwordCheck_ != nullptr; }
    bool    usingController() const { return usingController_; }
    QVariantMap buttonNames() const;
    int     standingNotifications() const { return notifications_.count(); }
    Q_INVOKABLE void clearNotifications() { notifications_.clearAll(); }
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

    // Hands a Steam game to Steam for what only Steam can do with it, through
    // its steam:// links: "details" (its library page, where Properties is),
    // "validate" (verify the game's files) or "uninstall" (Steam asks to
    // confirm). Steam's window opens over the launcher; Guide comes back.
    Q_INVOKABLE void steamAction(const QString& gameId, const QString& action);

    // Starts a built-in app (video player, file manager). Same contract as
    // launch(): false plus a status message when it cannot run.
    Q_INVOKABLE bool launchApp(const QString& appId);

    // What pressing Play would run, for the detail screen.
    Q_INVOKABLE QString launchCommand(const QString& gameId) const;

    // Install hint for a game whose engine is missing; empty otherwise.
    Q_INVOKABLE QString installHint(const QString& gameId) const;
    // Asked afresh rather than read from the game list, which is a snapshot
    // taken at the last scan: an emulator installed since changes all three.
    Q_INVOKABLE bool isPlayable(const QString& gameId) const;
    // Why it cannot start, in words for a person; empty when it can.
    Q_INVOKABLE QString launchProblem(const QString& gameId) const;
    // The emulator a game is waiting for, when installing it from Flathub
    // would make it playable: { app: "net.pcsx2.PCSX2", name: "PCSX2" }.
    // Empty otherwise.
    Q_INVOKABLE QVariantMap missingEngine(const QString& gameId) const;
    // Installs that emulator for this user, from Flathub, with its progress
    // in packageStatus; no password, and nothing to compile.
    Q_INVOKABLE void installEngine(const QString& gameId);
    // A one-time step the game's emulator needs first, as the router names it
    // ({ label: "Install PS3 system software" }); empty when there is none.
    Q_INVOKABLE QVariantMap setupStep(const QString& gameId) const;
    // Runs that step: the emulator's own window, which the user answers.
    Q_INVOKABLE void runSetupStep(const QString& gameId);

    // Ends whatever is running and comes back to the library. The system menu
    // asks first: whatever was not saved is lost.
    Q_INVOKABLE void quitRunningGame();

    // Asks omni-update what an update would change, in the background; nothing
    // is changed. Done by itself a minute after starting and every six hours.
    // `announce` says what was found, even "up to date" or "could not
    // check" — for a check someone asked for; the timed ones stay quiet.
    Q_INVOKABLE void checkSystemUpdates(bool announce = false);
    // Updates the whole system — pacman, then Flatpak apps — behind the
    // password prompt when the account has one. See omni-update.
    Q_INVOKABLE void updateSystem();

    // Back into what is running, from the launcher: its window to the front.
    // Found by process, since a game's window class is whatever its engine
    // chose; see omni-kwin-activate --pid.
    Q_INVOKABLE void resumeRunningGame();

    // Save states for the running game (canSaveState), in kStateSlots slots,
    // through RetroArch's command interface on its standard input
    // (stdin_cmd_enable, in /usr/share/omnios/retroarch.cfg): nothing listens
    // on the network. Saving says when the state is written, or that it was
    // not; loading goes back into the game, at the state.
    static constexpr int kStateSlots = 6;
    bool canSaveState() const;
    Q_INVOKABLE bool hasSavedState() const;
    // Each slot, for the save and load panel (SaveSlots.qml):
    // { slot, used, time (ms), picture (a file URL, or ""), when ("Today 01:43") }.
    Q_INVOKABLE QVariantList stateSlots() const;
    Q_INVOKABLE void saveState(int slot);
    Q_INVOKABLE void loadState(int slot);
    // Meta pressed in Game Mode (LauncherBus): the launcher to the front, and
    // the menu for what is running.
    Q_INVOKABLE void showMenu();
    // The OmniOS mark in the panel's corner, clicked in Game Mode: the
    // launcher to the front, and its system menu.
    Q_INVOKABLE void showSystemMenu();
    // Whether libretro's collection could have box art for the game, and a
    // look for it now (the game's menu, "Look for cover art").
    Q_INVOKABLE bool coverFindable(const QString& gameId) const;
    Q_INVOKABLE void findCover(const QString& gameId);
    // A game's menu, "Uninstall": a Steam game through Steam (which asks); any
    // other game's files moved to the Trash (omnios::gameFiles), so a mistake
    // can be put back from the desktop, after it is stopped if it runs.
    Q_INVOKABLE void uninstallGame(const QString& gameId);

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
    void updatesChanged();
    void packageBusyChanged();
    void packageStatusChanged();
    void storageChanged();
    void passwordChanged();
    void usingControllerChanged();
    void controllerKindChanged();
    void standingNotificationsChanged();
    // Asks the shell to make sure something inside it holds QML focus, before a
    // key is delivered to it.
    void focusWanted();
    // A game was started or resumed from the keyboard: show which key is which
    // of its console's buttons (ControlsOverlay). `rows` is a list of
    // {keys: [...], button: "..."}.
    void keyboardControlsWanted(const QString& title, const QVariantList& rows);
    // A controller was used: the keyboard's controls are not what matters.
    void keyboardControlsUnwanted();
    // Meta in Game Mode (showMenu): the shell opens the running game's menu,
    // or the system menu when nothing runs.
    void menuWanted();
    // The mark in the panel's corner (showSystemMenu): the system menu.
    void systemMenuWanted();

protected:
    // Watches the whole application's input for keys and clicks that did not
    // come from the controller, to clear usingController.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // A sentence for the status line, said each time it is set: the same
    // sentence again is an answer again (a second rescan finding the same
    // games, a second check finding nothing new), and was swallowed when
    // repeats were dropped. Nothing sets one over and over on a timer.
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

    // A Steam game is not a process of ours: `steam steam://rungameid/<id>`
    // hands the id to the client and exits at once, or, with no client
    // running, becomes the client and stays. Neither says when the game ends.
    // Steam's reaper does (omnios::runningSteamGames); this follows it — seen
    // appearing, then gone, is the game ending.
    void watchSteamGame();
    // Forgets the Steam game being watched, saying `said`; the launcher comes
    // to the front only when `returnHome`.
    void endSteamGame(const QString& said, bool returnHome);

    // What is running is written to a file in the runtime directory whenever
    // it changes, so a launcher that starts again — back from Desktop Mode,
    // which closes it, or after a crash — takes up the game the last one left
    // running. Without that it knew of no game, offered no Resume or Quit, and
    // Play started a second copy beside the first, both reading the pad.
    // Guide was pressed with the Steam client running, and the launcher lost
    // the front straight after: Steam's Big Picture, which it closes.
    void takeBackFromSteam();

    // The keyboard's controls for game `gameId`, for keyboardControlsWanted;
    // empty for one OmniOS sets up no keyboard for, or when the keyboard is
    // not what is being used.
    void offerKeyboardControls(const QString& gameId, const QString& title);

    // Game `gameId` started now: its time in the library, saved, for the
    // "Continue playing" row and the detail page.
    void notePlayed(const QString& gameId);

    void saveRunningState();
    void adoptRunningGame();
    // An adopted game is not a child of this launcher, so nothing reports its
    // end; its process is watched for instead.
    void watchAdoptedGame();

    // The pad drives the launcher unless something runs over it. With a game
    // running but the launcher brought to the front (Guide), it drives the
    // launcher again: nothing else is listening then.
    void updatePadRouting();

    // Escape opens the game menu too, beside Meta, while a game whose
    // emulator has no pause menu of its own (omnios::escapeOpensGameMenu) is
    // in front; not while the launcher is, where Escape goes back. Through
    // kglobalaccel, as omni-session-select gives Meta to that menu, and only
    // while Game Mode has Meta (its state file), or Escape would outlive it.
    void updateEscapeKey();

    // Do Not Disturb while a game runs: Plasma otherwise draws its
    // notifications over it, and in the corner of a full-screen game they
    // cover it until someone dismisses them — with a mouse. Asked for through
    // Plasma's Inhibit on this process's own D-Bus connection, which Plasma
    // lifts by itself if the launcher goes, so it can never be left on.
    void updateNotificationInhibit();
    void quietCriticalNotifications(bool quiet);
    uint notificationInhibit_ = 0;  // Plasma's cookie; 0 while not inhibited
    bool inhibitPending_ = false;

    // Rescans when the Steam games the tab would show have changed since the
    // last scan: a game finished installing, or was uninstalled, in Steam. Checked when
    // the launcher comes back to the front and every few seconds while it is
    // there, so a download finishing behind it still gets its tile.
    void refreshIfSteamChanged();

    GamepadInput   gamepad_;
    NotificationWatcher notifications_;
    SystemStatus   system_;
    GameListModel  model_;
    // The Games tab's "Continue playing" row, over model_.
    RecentGamesModel recent_{&model_};
    // Box art for games that have none, after each scan; the library cache
    // is saved a moment after covers arrive, once for a batch of them.
    CoverFetcher   covers_;
    QTimer         coverSave_;
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
    // The Steam game started from a tile, while it is starting or running.
    struct SteamGame {
        QString       appId;   // empty: none
        QString       gameId;  // the library's, "steam.<appId>"
        QString       title;
        bool          seen = false;
        QElapsedTimer since;
    };
    SteamGame      steamGame_;
    QTimer         steamGamePoll_;
    // A game or app an earlier launcher started (adoptRunningGame). The start
    // time tells it from whatever takes its process id after it ends.
    struct AdoptedGame {
        int           pid = 0;  // 0: none
        std::uint64_t start = 0;
        QString       gameId;
        QString       title;
    };
    AdoptedGame    adopted_;
    QTimer         adoptedPoll_;
    // When Guide was last pressed with Steam running; invalid otherwise.
    QElapsedTimer  guidePressed_;

    int            updateCount_ = -1;
    bool           restartRequired_ = false;
    bool           updating_ = false;
    // From the last update check (omni-update check): the pacman packages and
    // the Flatpak apps with an update waiting.
    QSet<QString>  pacmanUpdates_;
    QSet<QString>  flatpakUpdates_;
    // Which pacman package each app's tile belongs to, as pacman said; asked
    // once per app, and only while some package has an update.
    QHash<QString, QString> appPackages_;
    QProcess*      ownerCheck_ = nullptr;
    // An app's "Check for update", answered when the check is done.
    QString        appCheck_;
    // Which apps have an update, from the sets above, into the Apps model.
    void markAppUpdates();
    void finishAppCheck();
    QProcess*      updateCheck_ = nullptr;
    QTimer         updateTimer_;
    QProcess*     running_ = nullptr;
    QString       runningTitle_;
    QString       runningGameId_;
    QString       runningEngine_;           // running_'s emulator, if a game's
    QString       runningContent_;          // the file RetroArch was given, for its states
    QTimer        stateWatch_;              // a save asked for, until its file is written
    QElapsedTimer stateAsked_;
    QList<qint64> stateBefore_;             // each slot's file time before it, in ms; 0 for none
    // RetroArch's slot now, as far as the launcher knows: the one it starts
    // the game on (omnios::retroarchStartSlot), then moved one step at a time.
    int           stateSlotNow_ = 0;
    // Commands for RetroArch, one per stateFeed_ tick, and what follows them.
    QStringList   stateQueue_;
    QTimer        stateFeed_;
    bool          stateLoading_ = false;    // the queue ends in a load: back into the game after
    // Steps RetroArch's slot to `slot`.
    void queueSlotSteps(int slot);
    qint64 stateTime(int slot) const;
    bool          escapeForMenu_ = false;   // updateEscapeKey's last word
    QProcess*     escapeKeyCall_ = nullptr; // one at a time, so none overtakes another
    QString       status_;
    bool          scanning_ = false;
};
