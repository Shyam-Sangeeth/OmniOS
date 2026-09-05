#include "LauncherController.h"

#include <QCoreApplication>
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
    if (running_ != nullptr) {
        setStatus(tr("A game is already running"));
        return false;
    }

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

    auto* process = new QProcess(this);
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
                setStatus(code == 0 ? tr("%1 exited").arg(runningTitle_)
                                    : tr("%1 exited with code %2").arg(runningTitle_).arg(code));
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
    if (running_ != nullptr) {
        setStatus(tr("Something is already running"));
        return false;
    }

    const omnios::App* app = omnios::findApp(appId.toStdString());
    if (app == nullptr) {
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

    auto* process = new QProcess(this);
    runningTitle_ = QString::fromUtf8(app->name.data(), int(app->name.size()));

    connect(process, &QProcess::finished, this,
            [this, process](int, QProcess::ExitStatus) {
                switchWorkspace(kLauncherWorkspace);
                setStatus(tr("%1 closed").arg(runningTitle_));
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
    process->start(QString::fromStdString(argv.front()), args);
    running_ = process;
    setStatus(tr("Opening %1 …").arg(runningTitle_));
    emit gameRunningChanged();
    return true;
}

void LauncherController::quitRunningGame() {
    if (running_ == nullptr) return;
    running_->terminate();
    if (!running_->waitForFinished(3000)) running_->kill();
}
