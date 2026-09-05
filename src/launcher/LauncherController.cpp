#include "LauncherController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcessEnvironment>

#include "omnios/Apps.h"
#include "omnios/GameScanner.h"
#include "omnios/Paths.h"
#include "omnios/Router.h"

namespace {

const omnios::Game* findGame(const omnios::GameLibrary& library, const QString& id) {
    return library.find(id.toStdString());
}

// Workspace 1 holds the launcher; anything it starts belongs on 2.
constexpr const char* kLauncherWorkspace = "1";
constexpr const char* kAppWorkspace      = "2";

// Everything the launcher starts writes here. Without it a program that dies
// on startup leaves no trace at all: the tile flashes, the launcher comes
// back, and there is nothing to read. That is the single most expensive kind
// of failure to diagnose.
const QString kAppLog = QStringLiteral("/tmp/omnios-app.log");

// Package operations get their own log. Mixing them into kAppLog would mean a
// failed install erased the record of the crash the user was investigating.
const QString kPackageLog = QStringLiteral("/tmp/omnios-pkg.log");

void captureOutput(QProcess* process) {
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setStandardOutputFile(kAppLog, QIODevice::Truncate);
}

// Hyprland places a new window on the active workspace, so switching before
// spawning puts the app on its own workspace without needing a window rule —
// which matters because this Hyprland rejects the windowrule syntax outright
// and its replacement is still migrating to a Lua config.
//
// Silent when hyprctl is missing: the launcher must still work under another
// compositor, or none, just without the workspace switch.
void switchWorkspace(const char* workspace) {
    QProcess::execute(QStringLiteral("hyprctl"),
                      {QStringLiteral("dispatch"), QStringLiteral("workspace"),
                       QString::fromLatin1(workspace)});
}

// Switching workspace does not focus anything when the pointer has not moved,
// and an unfocused launcher receives no keys at all. Ask for it explicitly
// whenever the library comes back.
void focusLauncherWindow() {
    QProcess::execute(QStringLiteral("hyprctl"),
                      {QStringLiteral("dispatch"), QStringLiteral("focuswindow"),
                       QStringLiteral("class:omni-launcher")});
}

// Everything here runs through one shell so pacman output can be tee'd to the
// log while the exit status still comes from pacman rather than from tee.
// pipefail is why: without it a failed install would report success.
// The first line pacman marked as an error, with the prefix stripped. Empty
// when there is none.
QString firstErrorLine(const QString& output) {
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("error:"), Qt::CaseInsensitive))
            return trimmed.mid(6).trimmed();
    }
    return {};
}

QString shellWrap(const QString& body) {
    return QStringLiteral("set -o pipefail; { %1 ; } 2>&1 | tee -a %2")
        .arg(body, kPackageLog);
}

}  // namespace

LauncherController::LauncherController(QObject* parent) : QObject(parent) {
    std::string error;
    omnios::ensureDirectories(error);
    refresh();
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
                switchWorkspace(kLauncherWorkspace);
                focusLauncherWindow();
                setStatus(code == 0
                              ? tr("%1 exited").arg(runningTitle_)
                              : tr("%1 exited with code %2  —  see %3")
                                    .arg(runningTitle_).arg(code).arg(kAppLog));
                running_ = nullptr;
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                switchWorkspace(kLauncherWorkspace);
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    switchWorkspace(kAppWorkspace);
    process->start(QString::fromStdString(plan.argv.front()), args);
    running_ = process;
    setStatus(tr("Starting %1 …").arg(runningTitle_));
    emit gameRunningChanged();
    return true;
}

bool LauncherController::launchApp(const QString& appId) {
    const omnios::App* app = omnios::findApp(appId.toStdString());
    if (app == nullptr) {
        // An app installed from the store is a tile on the same tab and has to
        // open the same way. It has no argument list of its own — nothing about
        // VLC needs tuning the way mpv did — so it is started bare.
        if (const omnios::StoreApp* extra = omnios::findStoreApp(appId.toStdString()))
            return launchExtra(*extra);
        setStatus(tr("No app with id %1").arg(appId));
        return false;
    }
    if (!omnios::appAvailable(*app)) {
        // Same courtesy the router gives a missing emulator: name the package.
        setStatus(tr("%1 is not installed  —  install with: pacman -S %2")
                      .arg(QString::fromUtf8(app->name.data(), int(app->name.size())),
                           QString::fromUtf8(app->package.data(), int(app->package.size()))));
        return false;
    }

    const std::vector<std::string> argv = omnios::appArgv(*app);
    QStringList args;
    for (std::size_t i = 1; i < argv.size(); ++i) args << QString::fromStdString(argv[i]);

    return startApp(QString::fromUtf8(app->name.data(), int(app->name.size())),
                    QString::fromStdString(argv.front()), args);
}

bool LauncherController::launchExtra(const omnios::StoreApp& app) {
    const QString command = QString::fromUtf8(app.command.data(), int(app.command.size()));
    const QString name    = QString::fromUtf8(app.name.data(), int(app.name.size()));
    if (!omnios::storeAppInstalled(app)) {
        setStatus(tr("%1 is not installed").arg(name));
        return false;
    }
    return startApp(name, command, {});
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
                switchWorkspace(kLauncherWorkspace);
                focusLauncherWindow();
                // An app that closes within a couple of seconds did not
                // "close", it failed. Say so, and say where to look.
                setStatus(startedAt.elapsed() < 2500
                              ? tr("%1 closed immediately  -  see %2").arg(runningTitle_).arg(kAppLog)
                              : tr("%1 closed").arg(runningTitle_));
                running_ = nullptr;
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                switchWorkspace(kLauncherWorkspace);
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                runningTitle_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    switchWorkspace(kAppWorkspace);
    process->start(program, args);
    running_ = process;
    setStatus(tr("Opening %1 ...").arg(runningTitle_));
    emit gameRunningChanged();
    return true;
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

void LauncherController::setPackageStatus(const QString& text) {
    if (packageStatus_ == text) return;
    packageStatus_ = text;
    emit packageStatusChanged();
}

LauncherController::PackageRef LauncherController::packageFor(const QString& appId) const {
    const std::string id = appId.toStdString();
    if (const omnios::App* app = omnios::findApp(id)) {
        return {QString::fromUtf8(app->package.data(), int(app->package.size())),
                QString::fromUtf8(app->name.data(), int(app->name.size()))};
    }
    if (const omnios::StoreApp* app = omnios::findStoreApp(id)) {
        return {QString::fromUtf8(app->package.data(), int(app->package.size())),
                QString::fromUtf8(app->name.data(), int(app->name.size()))};
    }
    return {};
}

void LauncherController::runPackageCommand(const QString& script, const QString& verb,
                                           const QString& pastTense) {
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
                // Both tabs change: an install adds an Apps tile and flips a
                // store tile to installed.
                apps_.refresh();
                store_.refresh();
                emit packageBusyChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                setPackageStatus(tr("Could not run pacman: %1").arg(process->errorString()));
                package_ = nullptr;
                process->deleteLater();
                emit packageBusyChanged();
            });

    process->start(QStringLiteral("sh"), {QStringLiteral("-c"), shellWrap(script)});
    package_ = process;
    setPackageStatus(verb);
    emit packageBusyChanged();
}

void LauncherController::installApp(const QString& appId) {
    if (packageBusy()) return;

    // Both registries, not just the catalogue: a built-in whose package the
    // image was built without is exactly the case where installing it from its
    // own tile is the obvious repair, and refusing there would leave a dead
    // menu entry on a tile that already says it cannot run.
    const PackageRef ref = packageFor(appId);
    if (ref.package.isEmpty()) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }

    // -Sy before the install, not -Syu. A live image ships no sync databases,
    // so without the refresh pacman cannot find the package at all; a full
    // upgrade is the safer Arch habit but would pull the whole system into a
    // RAM overlay sized for one package.
    runPackageCommand(
        QStringLiteral("sudo pacman -Sy --noconfirm --needed %1").arg(ref.package),
        tr("Installing %1 ...").arg(ref.title), tr("%1 installed").arg(ref.title));
}

void LauncherController::removeApp(const QString& appId) {
    if (packageBusy()) return;

    const PackageRef ref = packageFor(appId);
    if (ref.package.isEmpty()) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }
    if (omnios::packageIsProtected(ref.package.toStdString())) {
        setPackageStatus(tr("%1 is part of OmniOS and cannot be removed").arg(ref.title));
        return;
    }

    // -Rns takes the dependencies the package brought in with it and its
    // configuration. Leaving them behind is how a console accumulates a
    // gigabyte of things nothing uses.
    runPackageCommand(QStringLiteral("sudo pacman -Rns --noconfirm %1").arg(ref.package),
                      tr("Removing %1 ...").arg(ref.title), tr("%1 removed").arg(ref.title));
}

void LauncherController::checkForUpdate(const QString& appId) {
    if (packageBusy()) return;

    const PackageRef ref = packageFor(appId);
    if (ref.package.isEmpty()) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }

    // checkupdates exits 2 with no output when everything is current, which is
    // not a failure. awk decides the message and the exit code, so "up to date"
    // and "no such package" cannot be confused with a mirror being down.
    const QString script =
        QStringLiteral("checkupdates 2>/dev/null | awk -v p=%1 "
                       "'$1==p {print \"Update available: \" $2 \" -> \" $4; f=1} "
                       "END {if (!f) print p \" is up to date\"}'")
            .arg(ref.package);
    runPackageCommand(script, tr("Checking %1 for updates ...").arg(ref.title), QString());
}

void LauncherController::updateApp(const QString& appId) {
    if (packageBusy()) return;

    const PackageRef ref = packageFor(appId);
    if (ref.package.isEmpty()) {
        setPackageStatus(tr("Nothing known as %1").arg(appId));
        return;
    }
    runPackageCommand(QStringLiteral("sudo pacman -Sy --noconfirm %1").arg(ref.package),
                      tr("Updating %1 ...").arg(ref.title), tr("%1 is up to date").arg(ref.title));
}

void LauncherController::quitRunningGame() { stopRunning(true); }

void LauncherController::stopRunning(bool returnHome) {
    if (running_ == nullptr) return;

    QProcess* process = running_;

    // Detach the handlers first. The exit we are about to cause would
    // otherwise report "X exited" over the status of whatever is starting, and
    // switch the workspace home again just as the new app arrives there.
    process->disconnect(this);
    running_ = nullptr;
    const QString previous = runningTitle_;
    runningTitle_.clear();

    process->terminate();
    if (!process->waitForFinished(2000)) {
        // Some programs ignore SIGTERM. A console cannot sit waiting on one.
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();

    if (returnHome) {
        switchWorkspace(kLauncherWorkspace);
        focusLauncherWindow();
    }
    if (!previous.isEmpty()) setStatus(tr("%1 closed").arg(previous));
    emit gameRunningChanged();
}
