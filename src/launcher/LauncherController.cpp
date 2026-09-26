#include "LauncherController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMap>
#include <QWindow>
#include <QProcessEnvironment>

#include <filesystem>
#include <system_error>

#include "InputMode.h"
#include "KeyDelivery.h"
#include "omnios/Apps.h"
#include "omnios/GameScanner.h"
#include "omnios/Paths.h"
#include "omnios/Router.h"

namespace {

const omnios::Game* findGame(const omnios::GameLibrary& library, const QString& id) {
    return library.find(id.toStdString());
}

// Everything the launcher starts writes here. Without it a program that dies
// on startup leaves no trace at all: the tile flashes, the launcher comes
// back, and there is nothing to read. That is the single most expensive kind
// of failure to diagnose.
const QString kAppLog = QStringLiteral("/tmp/omnios-app.log");

// Everything the shell runs on the system rather than for the user — installs,
// removals, pairing — writes here. Kept apart from kAppLog so that a failed
// install does not erase the record of the crash somebody was investigating.
const QString kPackageLog = QStringLiteral("/tmp/omnios-pkg.log");

void captureOutput(QProcess* process) {
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setStandardOutputFile(kAppLog, QIODevice::Truncate);
}

// Brings the launcher's window to the front. A Wayland app cannot raise itself
// — KWin treats that as focus stealing and flashes the taskbar entry instead —
// so omni-kwin-activate asks KWin to, through its scripting interface. That is
// what gets the library back over a running game, and back into focus after an
// app closes, when nothing has focus and a controller's presses would land
// nowhere.
//
// Detached and unchecked: the launcher must still work under another
// compositor, or none, just without being raised.
void focusLauncherWindow() {
    QProcess::startDetached(QStringLiteral("omni-kwin-activate"), {QStringLiteral("omni-launcher")});
}

// The first line the package manager marked as an error, with the prefix
// stripped. Empty when there is none.
QString firstErrorLine(const QString& output) {
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("error:"), Qt::CaseInsensitive))
            return trimmed.mid(6).trimmed();
    }
    return {};
}

// Everything runs through one shell so the output can be tee'd to the log while
// the exit status still comes from the package manager rather than from tee.
// pipefail is why: without it a failed removal would report success.
QString shellWrap(const QString& body) {
    return QStringLiteral("set -o pipefail; { %1 ; } 2>&1 | tee -a %2")
        .arg(body, kPackageLog);
}

}  // namespace

LauncherController::LauncherController(QObject* parent) : QObject(parent) {
    std::string error;
    omnios::ensureDirectories(error);

    // Network and bluetooth actions are long enough to be worth narrating, and
    // the shell already has one place that narrates a running system command.
    connect(&system_, &SystemStatus::runRequested, this,
            [this](const QString& script, const QString& verb) {
                runSystemCommand(script, verb, QString());
            });
    connect(&system_, &SystemStatus::message, this,
            [this](const QString& text) { setPackageStatus(text); });

    connect(&gamepad_, &GamepadInput::keyPressed, this, &LauncherController::deliverKey);
    QCoreApplication::instance()->installEventFilter(this);
    connect(&gamepad_, &GamepadInput::homeRequested, this, &LauncherController::goHome);
    connect(&gamepad_, &GamepadInput::kindChanged, this, &LauncherController::controllerKindChanged);

    // Controller use counts as someone being there. Told to KDE through the
    // freedesktop screensaver interface it implements, at most every half
    // minute: the idle timeouts are minutes long, and a process per press
    // would be waste.
    connect(&gamepad_, &GamepadInput::activity, this, [this]() {
        if (activityPinged_.isValid() && activityPinged_.elapsed() < 30000) return;
        activityPinged_.start();
        QProcess::startDetached(QStringLiteral("dbus-send"),
                                {QStringLiteral("--session"), QStringLiteral("--type=method_call"),
                                 QStringLiteral("--dest=org.freedesktop.ScreenSaver"),
                                 QStringLiteral("/ScreenSaver"),
                                 QStringLiteral("org.freedesktop.ScreenSaver.SimulateUserActivity")});
    });

    refresh();
}

QVariantMap LauncherController::buttonNames() const { return buttonNamesFor(gamepad_.kind()); }

void LauncherController::setUsingController(bool on) {
    if (usingController_ == on) return;
    usingController_ = on;
    emit usingControllerChanged();
}

bool LauncherController::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::KeyPress:
            // Controller presses are posted with a scan code no keyboard sends.
            if (static_cast<QKeyEvent*>(event)->nativeScanCode() != kControllerScanCode)
                setUsingController(false);
            break;
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
            setUsingController(false);
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void LauncherController::deliverKey(int key) {
    setUsingController(true);
    // Focus first, but not only focus. SDL reads the controller straight from
    // the input devices, so a press arrives whether or not the compositor has
    // given the shell keyboard focus — and there are several ordinary ways to
    // have none: just after an app exits, or after the session has been away
    // on another virtual terminal. Dropping the press in those cases is how a
    // controller ends up looking dead.
    //
    // The launcher has exactly one window, so falling back to it is not a
    // guess.
    // Posting a key at a window with no focused item inside it drops the key:
    // QQuickWindow hands key events to its active focus item, and there is none
    // while the window is inactive. The shell is asked to take focus first, so
    // a controller press lands whatever state the compositor left things in.
    emit focusWanted();

    QWindow* window = QGuiApplication::focusWindow();
    if (window == nullptr) {
        focusLauncherWindow();
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow* candidate : windows) {
            if (candidate != nullptr && candidate->isVisible()) {
                window = candidate;
                break;
            }
        }
        if (window == nullptr) {
            qWarning("omni-launcher: controller key %d had nowhere to go", key);
            return;
        }
    }

    // A press and a release, because Qt's key handling expects both and some
    // of the shell's handlers are on release.
    QGuiApplication::postEvent(window, controllerKeyEvent(QEvent::KeyPress, key));
    QGuiApplication::postEvent(window, controllerKeyEvent(QEvent::KeyRelease, key));
}

void LauncherController::goHome() {
    // Whatever is running keeps running; this is the console's "show me the
    // library" button, not a way to close a game.
    focusLauncherWindow();
}

QString LauncherController::gamesPath() const {
    return QString::fromStdString(omnios::gamesDir().generic_string());
}

QString LauncherController::version() const { return QStringLiteral("0.1.0"); }

void LauncherController::setStatus(const QString& text) {
    if (status_ == text) return;
    status_ = text;
    emit statusChanged();
}

void LauncherController::refresh() {
    scanning_ = true;
    emit scanningChanged();

    omnios::GameLibrary library;
    std::string error;
    // A corrupt cache is not fatal — the scan is the authority and rebuilds it.
    library.load(omnios::libraryCacheFile(), error);

    const omnios::ScanReport report = omnios::GameScanner().scan(library);
    library.save(omnios::libraryCacheFile(), error);

    model_.setLibrary(std::move(library));
    apps_.refresh();
    emit storageChanged();

    if (model_.rowCount() == 0) {
        setStatus(tr("No games in %1").arg(gamesPath()));
    } else if (!report.unidentified.empty()) {
        // Surfaced rather than swallowed: a file the scanner could not place is
        // the most likely reason a user's game is missing from the grid.
        setStatus(tr("%1 games · %2 file(s) not recognised")
                      .arg(model_.rowCount())
                      .arg(static_cast<int>(report.unidentified.size())));
    } else {
        setStatus(tr("%1 games").arg(model_.rowCount()));
    }

    scanning_ = false;
    emit scanningChanged();
}

QString LauncherController::launchCommand(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};

    omnios::LaunchOptions options;
    options.skipAvailabilityCheck = true;  // describing, not running
    return QString::fromStdString(omnios::planLaunch(*game, options).commandLine());
}

QString LauncherController::installHint(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};
    return QString::fromStdString(omnios::planLaunch(*game, {}).installHint);
}

bool LauncherController::launch(const QString& gameId) {
    // Opening something new replaces what is running. Done before the router
    // is consulted so a refused launch does not close what was already there.
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) {
        setStatus(tr("No game with id %1").arg(gameId));
        return false;
    }

    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (!plan.ok) {
        // The router's message is already written for a person; pass it
        // through rather than replacing it with something vaguer.
        setStatus(plan.installHint.empty()
                      ? QString::fromStdString(plan.error)
                      : tr("%1  —  install with: %2")
                            .arg(QString::fromStdString(plan.error),
                                 QString::fromStdString(plan.installHint)));
        return false;
    }

    stopRunning(false);

    auto* process = new QProcess(this);
    captureOutput(process);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const auto& variable : plan.environment) {
        env.insert(QString::fromStdString(variable.first),
                   QString::fromStdString(variable.second));
    }
    process->setProcessEnvironment(env);

    QStringList args;
    for (std::size_t i = 1; i < plan.argv.size(); ++i)
        args << QString::fromStdString(plan.argv[i]);

    runningTitle_ = QString::fromStdString(game->title);

    // Phase 9.4: when the game exits, the grid comes back. Without this the
    // shell would be left staring at whatever the game left on screen.
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus) {
                focusLauncherWindow();
                setStatus(code == 0
                              ? tr("%1 exited").arg(runningTitle_)
                              : tr("%1 exited with code %2  —  see %3")
                                    .arg(runningTitle_).arg(code).arg(kAppLog));
                running_ = nullptr;
                gamepad_.setAppRunning(false);
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                focusLauncherWindow();
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                gamepad_.setAppRunning(false);
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    process->start(QString::fromStdString(plan.argv.front()), args);
    running_ = process;
    gamepad_.setAppRunning(true);
    setStatus(tr("Starting %1 …").arg(runningTitle_));
    emit gameRunningChanged();
    return true;
}

bool LauncherController::launchApp(const QString& appId) {
    // The model is the authority on what a tile runs. A built-in carries the
    // arguments mpv and chromium needed before they would work here; a
    // discovered app carries the Exec line from its own desktop entry. Asking
    // the model for both means an app installed from the store opens exactly
    // the same way as one that shipped with the image.
    const QStringList argv = apps_.argvFor(appId);
    if (argv.isEmpty()) {
        setStatus(tr("No app with id %1").arg(appId));
        return false;
    }

    if (const omnios::App* app = omnios::findApp(appId.toStdString())) {
        if (!omnios::appAvailable(*app)) {
            // Same courtesy the router gives a missing emulator: name the
            // package that would provide it.
            setStatus(tr("%1 is not installed  —  install with: pacman -S %2")
                          .arg(QString::fromUtf8(app->name.data(), int(app->name.size())),
                               QString::fromUtf8(app->package.data(), int(app->package.size()))));
            return false;
        }
    }

    QString title = appId;
    for (int row = 0; row < apps_.rowCount(); ++row) {
        const QVariantMap entry = apps_.get(row);
        if (entry.value(QStringLiteral("appId")).toString() == appId) {
            title = entry.value(QStringLiteral("title")).toString();
            break;
        }
    }

    return startApp(title, argv.front(), argv.mid(1));
}

bool LauncherController::startApp(const QString& title, const QString& program,
                                  const QStringList& args) {
    stopRunning(false);

    auto* process = new QProcess(this);
    captureOutput(process);
    QElapsedTimer startedAt;
    startedAt.start();
    runningTitle_ = title;

    connect(process, &QProcess::finished, this,
            [this, process, startedAt](int, QProcess::ExitStatus) {
                focusLauncherWindow();
                // An app that closes within a couple of seconds did not
                // "close", it failed. Say so, and say where to look.
                if (startedAt.elapsed() >= 2500) {
                    setStatus(tr("%1 closed").arg(runningTitle_));
                } else if (storageCritical()) {
                    // The most likely reason by a distance, and the one the log
                    // is least likely to spell out: a program that cannot write
                    // anything usually dies without saying why.
                    setStatus(tr("%1 could not start — the disk is full").arg(runningTitle_));
                } else {
                    setStatus(tr("%1 closed immediately  -  see %2")
                                  .arg(runningTitle_).arg(kAppLog));
                }
                emit storageChanged();
                running_ = nullptr;
                gamepad_.setAppRunning(false);
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                focusLauncherWindow();
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                gamepad_.setAppRunning(false);
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    process->start(program, args);
    running_ = process;
    gamepad_.setAppRunning(true);
    setStatus(tr("Opening %1 ...").arg(runningTitle_));
    emit gameRunningChanged();
    return true;
}

namespace {

// Below this, installing anything is hopeless and most apps will not even
// start. Chosen because a Flatpak runtime alone is larger than this, and
// because Steam downloads its own client on first run.
constexpr qint64 kLowSpaceBytes = 700LL * 1024 * 1024;

QString humanSize(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
}

}  // namespace

qint64 LauncherController::freeBytes() const {
    std::error_code ec;
    const std::filesystem::space_info space =
        std::filesystem::space(omnios::gamesDir(), ec);
    if (ec) return -1;
    return static_cast<qint64>(space.available);
}

bool LauncherController::storageCritical() const {
    const qint64 free = freeBytes();
    return free >= 0 && free < kLowSpaceBytes;
}

QString LauncherController::storageNotice() const {
    const qint64 free = freeBytes();
    if (free < 0) return {};

    // The live-image warning and the space warning are the same sentence,
    // because they are the same subject and two stacked notices on a console
    // read as clutter rather than as emphasis.
    if (free < kLowSpaceBytes) {
        return tr("Only %1 left — apps will fail to start until something is removed")
            .arg(humanSize(free));
    }
    if (ephemeral()) {
        return tr("Live image — %1 free, and anything installed here is gone at the next boot")
            .arg(humanSize(free));
    }
    return {};
}

bool LauncherController::ephemeral() const {
    // archiso creates this and nothing else does, so it is the direct answer.
    // It is checked first because the mount test below turned out not to fire
    // on the live image it was written for.
    if (QFile::exists(QStringLiteral("/run/archiso"))) return true;

    // Falls back to the mount type, which catches any other live image that
    // runs the root from a RAM overlay, and stays false once OmniOS is
    // installed to a disk.
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    while (!mounts.atEnd()) {
        const QList<QByteArray> fields = mounts.readLine().split(' ');
        if (fields.size() > 2 && fields[1] == "/" && fields[2] == "overlay") return true;
    }
    return false;
}

// What to hand pacman -Qoq for this app. A discovered app is identified by its
// desktop file; a built-in has no desktop file of its own worth trusting, so
// its registry package name is used directly and "pacman -Qoq" is skipped by
// naming the package's own binary path instead.
QString LauncherController::packagePathFor(const QString& appId) const {
    const QStringList argv = apps_.argvFor(appId);
    const AppListModel::Removal ref = apps_.removalFor(appId);
    if (!ref.flatpak && !ref.target.isEmpty()) return ref.target;
    // Built-in: ask which package owns the executable itself.
    return argv.isEmpty() ? QString() : QStringLiteral("$(command -v %1)").arg(argv.front());
}

void LauncherController::setPackageStatus(const QString& text) {
    if (packageStatus_ == text) return;
    packageStatus_ = text;
    emit packageStatusChanged();
}

namespace {

// Whether sudo will run without asking. Yes on the live image and on an install
// whose account has no password (sudoers there says NOPASSWD, since there is no
// password to type); no on an install with one.
bool sudoWithoutPassword() {
    QProcess probe;
    probe.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    probe.closeWriteChannel();
    return probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit &&
           probe.exitCode() == 0;
}

}  // namespace

void LauncherController::runSystemCommand(const QString& script, const QString& verb,
                                           const QString& pastTense, const QString& rootReason) {
    if (!rootReason.isEmpty() && !sudoWithoutPassword()) {
        pendingRoot_ = {true, script, verb, pastTense, rootReason};
        passwordError_.clear();
        emit passwordChanged();
        emit packageBusyChanged();
        return;
    }
    startSystemCommand(script, verb, pastTense, QByteArray());
}

void LauncherController::submitPassword(const QString& password) {
    if (!pendingRoot_.active || passwordCheck_ != nullptr) return;
    QByteArray secret = password.toUtf8();
    if (secret.isEmpty()) return;

    // Checked on its own before anything runs. Handing a wrong password
    // straight to the real command would fail it halfway through a script,
    // with sudo's complaint as the only thing to show for it.
    auto* check = new QProcess(this);
    check->setProcessChannelMode(QProcess::MergedChannels);
    passwordCheck_ = check;
    connect(check, &QProcess::finished, this,
            [this, check, secret](int code, QProcess::ExitStatus status) mutable {
                const QString output = QString::fromUtf8(check->readAll());
                passwordCheck_ = nullptr;
                check->deleteLater();
                if (!pendingRoot_.active) {  // cancelled while it was checking
                    secret.fill('\0');
                    emit passwordChanged();
                    return;
                }
                if (status == QProcess::NormalExit && code == 0) {
                    const PendingRoot job = pendingRoot_;
                    pendingRoot_ = PendingRoot();
                    passwordError_.clear();
                    emit passwordChanged();
                    startSystemCommand(job.script, job.verb, job.pastTense, secret);
                } else {
                    // pam_faillock locks the account for ten minutes after three
                    // misses in fifteen, and from then on even the right
                    // password fails — silently, as Arch configures it, and its
                    // records are root's to read. So the misses are counted
                    // here: saying "not right" to a locked account is a lie
                    // that invites a fourth try, which happened in testing.
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    passwordFailures_.append(now);
                    while (!passwordFailures_.isEmpty() &&
                           now - passwordFailures_.first() > 15 * 60 * 1000)
                        passwordFailures_.removeFirst();
                    const bool maybeLocked =
                        passwordFailures_.size() >= 3 ||
                        output.contains(QLatin1String("locked"), Qt::CaseInsensitive);
                    passwordError_ = maybeLocked
                                         ? tr("Still refused. After three wrong tries the account is "
                                              "locked for ten minutes, so wait before the next one.")
                                         : tr("That password is not right");
                    emit passwordChanged();
                }
                secret.fill('\0');
            });
    connect(check, &QProcess::errorOccurred, this, [this, check](QProcess::ProcessError) {
        if (passwordCheck_ != check) return;
        passwordCheck_ = nullptr;
        passwordError_ = tr("Could not run sudo: %1").arg(check->errorString());
        check->deleteLater();
        emit passwordChanged();
    });

    // -k: check this password, not a timestamp left by an earlier sudo.
    check->start(QStringLiteral("sudo"), {QStringLiteral("-S"), QStringLiteral("-k"),
                                          QStringLiteral("-p"), QString(), QStringLiteral("true")});
    check->write(secret + '\n');
    check->closeWriteChannel();
    secret.fill('\0');
    emit passwordChanged();
}

void LauncherController::cancelPassword() {
    if (!pendingRoot_.active) return;
    pendingRoot_ = PendingRoot();
    passwordError_.clear();
    emit passwordChanged();
    emit packageBusyChanged();
    setPackageStatus(tr("Cancelled"));
}

void LauncherController::startSystemCommand(const QString& script, const QString& verb,
                                             const QString& pastTense, QByteArray password) {
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    // Output is read back rather than only written to the log: a user should not
    // have to open a log to find out that a mirror was unreachable.
    connect(process, &QProcess::finished, this,
            [this, process, pastTense](int code, QProcess::ExitStatus) {
                const QString output = QString::fromUtf8(process->readAll()).trimmed();
                const QString lastLine = output.section(QLatin1Char('\n'), -1).trimmed();
                if (code != 0) {
                    // pacman's *last* line is its summary — "Errors occurred, no
                    // packages were upgraded" — which says nothing about what
                    // went wrong. The first "error:" line is the root cause and
                    // the ones after it are the cascade. Reporting the summary
                    // instead cost a rebuild and a login on tty2 to discover
                    // that the keyring was not writable.
                    const QString cause = firstErrorLine(output);
                    setPackageStatus(cause.isEmpty()
                                         ? (lastLine.isEmpty()
                                                ? tr("Failed  -  see %1").arg(kPackageLog)
                                                : lastLine)
                                         : tr("%1  -  see %2").arg(cause, kPackageLog));
                } else {
                    // An empty pastTense means the command reports its own
                    // result, as the update check does.
                    setPackageStatus(pastTense.isEmpty() ? lastLine : pastTense);
                }
                package_ = nullptr;
                process->deleteLater();
                // An app can have appeared or vanished, so the grid is rebuilt
                // from what is on disk rather than patched.
                apps_.refresh();
                system_.refresh();
                emit storageChanged();
                emit packageBusyChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                setPackageStatus(tr("Could not run the package manager: %1")
                                     .arg(process->errorString()));
                package_ = nullptr;
                process->deleteLater();
                emit packageBusyChanged();
            });

    process->start(QStringLiteral("sh"), {QStringLiteral("-c"), shellWrap(script)});
    // The password goes down stdin, never into args: every process on the
    // machine can read another's command line. With no password stdin is
    // simply closed, so a sudo that unexpectedly asks gets end-of-file and
    // fails rather than waiting for ever.
    if (!password.isEmpty()) {
        process->write(password + '\n');
        password.fill('\0');
    }
    process->closeWriteChannel();
    package_ = process;
    setPackageStatus(verb);
    emit packageBusyChanged();
}

void LauncherController::removeApp(const QString& appId) {
    if (packageBusy()) return;

    const AppListModel::Removal removal = apps_.removalFor(appId);
    if (!removal.known) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }
    if (!removal.refusal.isEmpty()) {
        setPackageStatus(removal.refusal);
        return;
    }

    if (removal.flatpak) {
        // --delete-data as well, because a console that keeps the settings of
        // an app you removed is just accumulating rubbish you cannot see.
        runSystemCommand(
            QStringLiteral("flatpak uninstall --assumeyes --delete-data %1").arg(removal.target),
            tr("Removing %1 ...").arg(removal.title), tr("%1 removed").arg(removal.title));
        return;
    }

    // Only pacman knows which package owns a desktop entry, and asking it is
    // more reliable than guessing from the name: thunar's entry belongs to
    // "thunar", but plenty of apps ship an entry from a package named nothing
    // like the binary. -Rns then takes the dependencies it brought in with it,
    // which is how a console avoids accumulating a gigabyte nothing uses.
    //
    // packagePathFor, as Update uses: a built-in tile has no desktop entry of
    // its own, so it is the program's path that pacman is asked about. Steam's
    // Uninstall asked about an empty path until this was shared.
    const QString script =
        QStringLiteral("pkg=$(pacman -Qoq %1 2>/dev/null) || { echo \"error: no package owns %1\"; "
                       "exit 1; }; sudo -S -p '' pacman -Rns --noconfirm \"$pkg\"")
            .arg(packagePathFor(appId));
    runSystemCommand(script, tr("Removing %1 ...").arg(removal.title),
                      tr("%1 removed").arg(removal.title),
                      tr("Removing %1 changes the system, so it needs your password.")
                          .arg(removal.title));
}

void LauncherController::checkForUpdate(const QString& appId) {
    if (packageBusy()) return;

    const AppListModel::Removal ref = apps_.removalFor(appId);
    if (!ref.known) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }

    if (ref.flatpak) {
        // flatpak has no dry run, so this asks the remote what it has and
        // compares commits. Nothing is downloaded.
        runSystemCommand(
            QStringLiteral("flatpak remote-info --show-commit flathub %1 >/dev/null 2>&1 && "
                           "echo \"%2: use Update, or the Store\" || echo \"%2: no update info\"")
                .arg(ref.target, ref.title),
            tr("Checking %1 ...").arg(ref.title), QString());
        return;
    }

    // checkupdates compares against a throwaway database rather than running
    // "pacman -Sy", which on Arch would leave the system one partial upgrade
    // away from a mismatched libc. It exits 2 with no output when everything is
    // current, which is not a failure — awk decides the message and the exit
    // code, so "up to date" cannot be confused with a mirror being down.
    const QString script =
        QStringLiteral("pkg=$(pacman -Qoq %1 2>/dev/null) || { echo \"error: no package owns %1\"; "
                       "exit 1; }; checkupdates 2>/dev/null | awk -v p=\"$pkg\" "
                       "'$1==p {print \"Update available: \" $2 \" -> \" $4; f=1} "
                       "END {if (!f) print p \" is up to date\"}'")
            .arg(packagePathFor(appId));
    runSystemCommand(script, tr("Checking %1 for updates ...").arg(ref.title), QString());
}

void LauncherController::updateApp(const QString& appId) {
    if (packageBusy()) return;

    const AppListModel::Removal ref = apps_.removalFor(appId);
    if (!ref.known) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }

    if (ref.flatpak) {
        runSystemCommand(QStringLiteral("flatpak update --assumeyes %1").arg(ref.target),
                          tr("Updating %1 ...").arg(ref.title),
                          tr("%1 is up to date").arg(ref.title));
        return;
    }

    const QString script =
        QStringLiteral("pkg=$(pacman -Qoq %1 2>/dev/null) || { echo \"error: no package owns %1\"; "
                       "exit 1; }; sudo -S -p '' pacman -Sy --noconfirm \"$pkg\"")
            .arg(packagePathFor(appId));
    runSystemCommand(script, tr("Updating %1 ...").arg(ref.title),
                      tr("%1 is up to date").arg(ref.title),
                      tr("Updating %1 changes the system, so it needs your password.")
                          .arg(ref.title));
}

void LauncherController::pairController() {
    if (packageBusy()) return;

    // Long by design: the scan has to outlast somebody walking to the console
    // and holding the pairing button down. The script says what it found, so
    // there is no pastTense to add on top of it.
    runSystemCommand(QStringLiteral("omni-pair-controller 25"),
                     tr("Hold the pairing button on the controller ..."), QString());
}

void LauncherController::powerAction(const QString& action) {
    // A fixed table, not string interpolation. The action arrives from QML, and
    // "hand a string from the UI to a process that turns the machine off" is
    // not a sentence worth writing without a whitelist in it.
    static const QMap<QString, QString> kActions = {
        {QStringLiteral("suspend"), QStringLiteral("suspend")},
        {QStringLiteral("reboot"), QStringLiteral("reboot")},
        {QStringLiteral("poweroff"), QStringLiteral("poweroff")},
    };
    const auto it = kActions.constFind(action);
    if (it == kActions.constEnd()) {
        setStatus(tr("Unknown power action %1").arg(action));
        return;
    }

    // Whatever is running is ended first. Suspending with a game mid-frame and
    // resuming into it is a good way to find out which emulators survive a
    // suspend, and a console should not conduct that experiment for you.
    stopRunning(true);

    // logind allows an active local session to do all three without a password,
    // and /etc/polkit-1/rules.d says so explicitly for this image. No sudo:
    // going through systemctl means logind still gets to run the inhibitors,
    // so anything holding a shutdown off is respected.
    setStatus(action == QStringLiteral("suspend") ? tr("Suspending …")
                                                  : tr("Shutting down …"));

    // Not startDetached. A machine that refuses to suspend — no S3 firmware, an
    // inhibitor holding it off — would otherwise leave "Suspending …" on screen
    // for ever with nothing to say why, which is the worst of both: it looks
    // like the console froze rather than like the request was declined.
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus) {
                if (code != 0) {
                    const QString output = QString::fromUtf8(process->readAll()).trimmed();
                    setStatus(output.isEmpty()
                                  ? tr("That did not work — the system refused")
                                  : output.section(QLatin1Char('\n'), -1).trimmed());
                }
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                setStatus(tr("Could not run systemctl: %1").arg(process->errorString()));
                process->deleteLater();
            });
    process->start(QStringLiteral("systemctl"), {*it});
}

bool LauncherController::liveImage() const {
    // Only archiso's own directory, not the RAM-overlay test ephemeral() falls
    // back to: the installer copies from the squashfs archiso mounted, so any
    // other kind of live system has nothing for it to install.
    return QFile::exists(QStringLiteral("/run/archiso"));
}

void LauncherController::installOmniOS() {
    if (!liveImage()) {
        setStatus(tr("OmniOS is already installed"));
        return;
    }
    startApp(tr("Install OmniOS"), QCoreApplication::applicationFilePath(),
             {QStringLiteral("--install")});
}

void LauncherController::switchToDesktop() {
    // Whatever is running stays running. Both modes are the one Plasma session,
    // so leaving Game Mode only closes the launcher, and the game or app it
    // started carries on, on the desktop's taskbar.
    setStatus(tr("Switching to the desktop ..."));

    // Waited on, like powerAction and for the same reason: if the switch is
    // refused, "Switching ..." must not sit on screen for ever. On success
    // there is nothing to wait for — omni-session-select closes this process.
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus) {
                if (code != 0) {
                    const QString output = QString::fromUtf8(process->readAll()).trimmed();
                    setStatus(output.isEmpty()
                                  ? tr("Could not switch to the desktop")
                                  : output.section(QLatin1Char('\n'), -1).trimmed());
                }
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                setStatus(tr("Could not run omni-session-select: %1").arg(process->errorString()));
                process->deleteLater();
            });
    process->start(QStringLiteral("omni-session-select"), {QStringLiteral("desktop")});
}

void LauncherController::quitRunningGame() { stopRunning(true); }

void LauncherController::stopRunning(bool returnHome) {
    if (running_ == nullptr) return;

    QProcess* process = running_;

    // Detach the handlers first. The exit we are about to cause would
    // otherwise report "X exited" over the status of whatever is starting, and
    // raise the launcher again just as the new app arrives over it.
    process->disconnect(this);
    running_ = nullptr;
    gamepad_.setAppRunning(false);
    const QString previous = runningTitle_;
    runningTitle_.clear();

    process->terminate();
    if (!process->waitForFinished(2000)) {
        // Some programs ignore SIGTERM. A console cannot sit waiting on one.
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();

    if (returnHome) focusLauncherWindow();
    if (!previous.isEmpty()) setStatus(tr("%1 closed").arg(previous));
    emit gameRunningChanged();
}
