#include "LauncherController.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMap>
#include <QSaveFile>
#include <QWindow>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <filesystem>
#include <memory>
#include <system_error>

#include <signal.h>

#include "InputMode.h"
#include "KeyDelivery.h"
#include "omnios/Apps.h"
#include "omnios/CoverArt.h"
#include "omnios/GameScanner.h"
#include "omnios/KeyboardLayout.h"
#include "omnios/Paths.h"
#include "omnios/EmulatorSetup.h"
#include "omnios/Router.h"
#include "omnios/SteamLibrary.h"

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

// Asks these processes to stop, and three seconds on ends any that ignored
// it — a hung game does. Each is recognised by its start time as well as its
// number, so nothing that has taken a number since is ever hit.
void stopProcesses(QObject* context, const std::vector<int>& pids) {
    std::vector<std::pair<int, std::uint64_t>> noted;
    for (const int pid : pids) {
        if (const std::uint64_t start = omnios::processStartTime(pid)) noted.emplace_back(pid, start);
    }
    for (const auto& process : noted) ::kill(process.first, SIGTERM);
    if (noted.empty()) return;
    QTimer::singleShot(3000, context, [noted]() {
        for (const auto& process : noted) {
            if (omnios::processStartTime(process.first) == process.second)
                ::kill(process.first, SIGKILL);
        }
    });
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
//
// Steam's Big Picture is minimised on the way: left open behind the library it
// goes on reading the pad, and a press meant for the library could choose
// something in it and bring it forward (omni-kwin-activate).
void focusLauncherWindow() {
    QProcess::startDetached(QStringLiteral("omni-kwin-activate"),
                            {QStringLiteral("--minimize-big-picture"), QStringLiteral("omni-launcher")});
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
    connect(this, &LauncherController::gameRunningChanged, this, &LauncherController::updateNotificationInhibit);
    connect(this, &LauncherController::gameRunningChanged, this, &LauncherController::saveRunningState);
    adoptedPoll_.setInterval(2000);
    connect(&adoptedPoll_, &QTimer::timeout, this, &LauncherController::watchAdoptedGame);
    connect(&notifications_, &NotificationWatcher::countChanged, this,
            &LauncherController::standingNotificationsChanged);
    // A launcher that stopped mid-game left the user's critical-notification
    // setting changed; put it back.
    quietCriticalNotifications(false);

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
    // A pad picked up in the middle of a game: the keyboard's controls bar
    // has nothing more to say.
    connect(&gamepad_, &GamepadInput::activity, this, &LauncherController::keyboardControlsUnwanted);

    // Steam installs and removes games in its own window, not through the
    // shell, so the Games tab watches for the result rather than being told.
    steamPoll_.setInterval(10000);
    connect(&steamPoll_, &QTimer::timeout, this, &LauncherController::refreshIfSteamChanged);
    steamPoll_.start();
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                updatePadRouting();
                if (state == Qt::ApplicationActive) refreshIfSteamChanged();
                else takeBackFromSteam();
            });

    coverSave_.setSingleShot(true);
    coverSave_.setInterval(2000);
    connect(&coverSave_, &QTimer::timeout, this, [this]() {
        std::string error;
        model_.library().save(omnios::libraryCacheFile(), error);
    });
    connect(&covers_, &CoverFetcher::coverReady, this, [this](const QString& id, const QString& path) {
        model_.setCover(id, path);
        coverSave_.start();
    });
    connect(&covers_, &CoverFetcher::notFound, this, [this](const QString& id, bool offline) {
        const omnios::Game* game = findGame(model_.library(), id);
        const QString title = game ? QString::fromStdString(game->title) : id;
        setStatus(offline ? tr("Could not reach the cover art collection  -  is the network up?")
                          : tr("No cover art for %1 in the collection").arg(title));
    });

    steamGamePoll_.setInterval(2000);
    connect(&steamGamePoll_, &QTimer::timeout, this, &LauncherController::watchSteamGame);

    // Updates: looked for a minute in — not at once, when the network may not
    // be up yet and the session is busy starting — and every six hours after.
    // Never on the live image, which is not updated.
    restartRequired_ = QFile::exists(QStringLiteral("/run/omnios/reboot-required"));
    if (!liveImage()) {
        QTimer::singleShot(60 * 1000, this, [this]() { checkSystemUpdates(); });
        updateTimer_.setInterval(6 * 60 * 60 * 1000);
        connect(&updateTimer_, &QTimer::timeout, this, [this]() { checkSystemUpdates(); });
        updateTimer_.start();
    }

    adoptRunningGame();
    refresh();
}

void LauncherController::refreshIfSteamChanged() {
    // Behind a game or Steam itself nobody is looking at the grid; the
    // activation check catches up the moment the launcher is back.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive || scanning_) return;
    if (omnios::steamLibraryStamp(omnios::GameScanner().steamLibraries()) == steamStamp_) return;
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
    // The Steam client answers Guide too, by opening Big Picture over
    // everything, and it gets there after the launcher does. Noted, so that
    // losing the front straight after is recognised (takeBackFromSteam).
    if (omnios::steamClientRunning()) guidePressed_.start();
    else guidePressed_.invalidate();
}

void LauncherController::takeBackFromSteam() {
    // Only just after Guide, and only when the launcher did not hand the
    // front to something itself: every launch, resume and Steam action
    // forgets the press.
    if (!guidePressed_.isValid() || guidePressed_.elapsed() > 5000) return;
    guidePressed_.invalidate();
    qInfo("Guide: lost the front with Steam running; putting Big Picture away");
    // A Wayland app cannot see whose window came up, so this does not know it
    // was Big Picture; closing and minimising it when it is not open do
    // nothing. Minimised as well as closed: Steam did not always close it, and
    // open behind the library it went on reading the pad (omni-kwin-activate).
    const auto takeBack = []() {
        QProcess::startDetached(QStringLiteral("steam"), {QStringLiteral("steam://close/bigpicture")});
        focusLauncherWindow();
    };
    takeBack();
    // Again, twice: Big Picture may still be opening at the first, and
    // closing it hands the front to Steam's other window if it has one.
    QTimer::singleShot(1500, this, takeBack);
    QTimer::singleShot(4000, this, takeBack);
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

    // Taken before the scan, so a change landing during it is seen next time
    // rather than lost.
    steamStamp_ = omnios::steamLibraryStamp(omnios::GameScanner().steamLibraries());

    omnios::GameLibrary library;
    std::string error;
    // A corrupt cache is not fatal — the scan is the authority and rebuilds it.
    library.load(omnios::libraryCacheFile(), error);

    const omnios::ScanReport report = omnios::GameScanner().scan(library);
    library.save(omnios::libraryCacheFile(), error);

    model_.setLibrary(std::move(library));
    covers_.fetchMissing(model_.library());
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

    // What will run: from Flathub, when that is where the emulator is. A
    // plan that cannot run yet is described as it would be once it can.
    omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (!plan.ok) {
        omnios::LaunchOptions options;
        options.skipAvailabilityCheck = true;  // describing, not running
        plan = omnios::planLaunch(*game, options);
    }
    return QString::fromStdString(plan.commandLine());
}

QString LauncherController::installHint(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};
    return QString::fromStdString(omnios::planLaunch(*game, {}).installHint);
}

bool LauncherController::isPlayable(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    return game != nullptr && omnios::planLaunch(*game, {}).ok;
}

QString LauncherController::launchProblem(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};
    return QString::fromStdString(omnios::planLaunch(*game, {}).error);
}

QVariantMap LauncherController::missingEngine(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};
    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (plan.ok || plan.flatpakApp.empty()) return {};
    return {{QStringLiteral("app"), QString::fromStdString(plan.flatpakApp)},
            {QStringLiteral("name"), QString::fromStdString(plan.engineDisplayName)}};
}

void LauncherController::installEngine(const QString& gameId) {
    const QVariantMap engine = missingEngine(gameId);
    if (engine.isEmpty() || packageBusy()) return;
    const QString app = engine.value(QStringLiteral("app")).toString();
    const QString name = engine.value(QStringLiteral("name")).toString();

    // The first emulator brings a runtime of about a gigabyte, and Flatpak
    // needs room for it on top while it unpacks. Better said now than as a
    // failure half way: on the live USB, where everything installed lives in
    // memory, that room ran out in testing.
    constexpr qint64 kNeeded = 1536LL * 1024 * 1024;
    const qint64 free = freeBytes();
    if (free >= 0 && free < kNeeded) {
        setPackageStatus(liveImage()
                             ? tr("Not enough room to install %1 on the USB stick, whose space is memory  -  "
                                  "install OmniOS to a disk for it").arg(name)
                             : tr("Installing %1 needs about 1.5 GB free, and there is %2 MB")
                                   .arg(name).arg(free / (1024 * 1024)));
        return;
    }

    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto collected = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, collected, name]() {
        const QByteArray chunk = process->readAllStandardOutput();
        collected->append(chunk);
        // Flatpak says how far it has got as a percentage, the runtime first
        // (hundreds of MB the first time) and then the app itself.
        static const QRegularExpression kPercent(QStringLiteral("(\\d{1,3})%"));
        QRegularExpressionMatchIterator it = kPercent.globalMatch(QString::fromUtf8(chunk));
        QString last;
        while (it.hasNext()) last = it.next().captured(1);
        if (!last.isEmpty()) setPackageStatus(tr("Installing %1  ·  %2%").arg(name, last));
    });
    connect(process, &QProcess::finished, this, [this, process, collected, name](int code, QProcess::ExitStatus) {
        collected->append(process->readAll());
        QFile log(kPackageLog);
        if (log.open(QIODevice::Append)) log.write(*collected);
        package_ = nullptr;
        process->deleteLater();
        if (code == 0) {
            setPackageStatus(tr("%1 is installed").arg(name));
            refresh();  // the game list is a snapshot; its games are playable now
        } else {
            const QString output = QString::fromUtf8(*collected).trimmed();
            const QString cause = firstErrorLine(output);
            setPackageStatus(tr("Could not install %1  -  %2")
                                 .arg(name, cause.isEmpty() ? output.section(QLatin1Char('\n'), -1).trimmed()
                                                            : cause));
        }
        emit packageBusyChanged();
        emit storageChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;  // anything else ends in finished, above
        setPackageStatus(tr("Could not run Flatpak: %1").arg(process->errorString()));
        package_ = nullptr;
        process->deleteLater();
        emit packageBusyChanged();
    });

    package_ = process;
    emit packageBusyChanged();
    setPackageStatus(tr("Installing %1 from Flathub ...").arg(name));
    // For this user, so no password is asked. Flathub is added for the user
    // too: the system's copy of it is the system's to install into.
    process->start(QStringLiteral("sh"),
                   {QStringLiteral("-c"),
                    QStringLiteral("flatpak remote-add --user --if-not-exists flathub "
                                   "https://dl.flathub.org/repo/flathub.flatpakrepo && "
                                   "exec flatpak install --user --noninteractive -y flathub \"$1\""),
                    QStringLiteral("sh"), app});
}

QVariantMap LauncherController::setupStep(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return {};
    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (plan.ok || plan.setupArgv.empty()) return {};
    return {{QStringLiteral("label"), QString::fromStdString(plan.setupLabel)}};
}

void LauncherController::runSetupStep(const QString& gameId) {
    guidePressed_.invalidate();
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr || packageBusy()) return;
    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (plan.ok || plan.setupArgv.empty()) return;
    // Its own boxes first, so only the one that has to be answered is left.
    omnios::prepareEmulator(plan.engineId, gamepad_.controllers());

    auto* process = new QProcess(this);
    captureOutput(process);
    const QString label = QString::fromStdString(plan.setupLabel);
    connect(process, &QProcess::finished, this, [this, process, label](int, QProcess::ExitStatus) {
        package_ = nullptr;
        process->deleteLater();
        focusLauncherWindow();
        refresh();  // playable now, if it went through
        setPackageStatus(tr("%1  -  done").arg(label));
        emit packageBusyChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;  // anything else ends in finished, above
        package_ = nullptr;
        process->deleteLater();
        setPackageStatus(tr("Could not start: %1").arg(process->errorString()));
        emit packageBusyChanged();
    });
    // RPCS3 stays open after installing, in its own window, and would be
    // waited on for ever: once the game can be played, the step is done and
    // the emulator is closed.
    //
    // Closed through Flatpak when it came from there: the process started
    // here is only the sandbox's outside, and stopping it left the emulator
    // running inside.
    const omnios::Engine* engine = omnios::findEngine(plan.engineId);
    const QString flatpakApp =
        plan.setupArgv.front() == "flatpak" && engine != nullptr ? QString::fromStdString(std::string(engine->flatpak)) : QString();
    auto* watch = new QTimer(process);
    watch->setInterval(2000);
    connect(watch, &QTimer::timeout, this, [this, process, gameId, watch, flatpakApp]() {
        const omnios::Game* game = findGame(model_.library(), gameId);
        if (game == nullptr || !omnios::planLaunch(*game, {}).ok) return;
        watch->stop();
        if (!flatpakApp.isEmpty()) QProcess::startDetached(QStringLiteral("flatpak"), {QStringLiteral("kill"), flatpakApp});
        else process->terminate();
    });
    watch->start();

    QStringList args;
    for (std::size_t i = 1; i < plan.setupArgv.size(); ++i) args << QString::fromStdString(plan.setupArgv[i]);
    package_ = process;
    emit packageBusyChanged();
    setPackageStatus(tr("%1  -  answer its question with a mouse or a keyboard").arg(label));
    process->start(QString::fromStdString(plan.setupArgv.front()), args);
}

bool LauncherController::launch(const QString& gameId) {
    guidePressed_.invalidate();
    // Opening something new replaces what is running. Done before the router
    // is consulted so a refused launch does not close what was already there.
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) {
        qWarning("launch: no game with id %s", qPrintable(gameId));
        setStatus(tr("No game with id %1").arg(gameId));
        return false;
    }

    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    qInfo("launch %s: %s", qPrintable(gameId),
          plan.ok ? plan.commandLine().c_str() : ("refused: " + plan.error).c_str());
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

    if (game->platform == omnios::Platform::Steam) {
        // Detached, and never running_: stopping running_ terminates it, and
        // that process may be the Steam client itself.
        QProcess steam;
        QProcessEnvironment steamEnv = QProcessEnvironment::systemEnvironment();
        for (const auto& variable : plan.environment)
            steamEnv.insert(QString::fromStdString(variable.first),
                            QString::fromStdString(variable.second));
        steam.setProcessEnvironment(steamEnv);
        steam.setProgram(QString::fromStdString(plan.argv.front()));
        QStringList steamArgs;
        for (std::size_t i = 1; i < plan.argv.size(); ++i)
            steamArgs << QString::fromStdString(plan.argv[i]);
        steam.setArguments(steamArgs);
        if (!steam.startDetached()) {
            setStatus(tr("Could not start Steam"));
            return false;
        }
        steamGame_.appId = QString::fromStdString(game->launchId);
        steamGame_.title = QString::fromStdString(game->title);
        steamGame_.gameId = gameId;
        steamGame_.seen = false;
        steamGame_.since.start();
        steamGamePoll_.start();
        updatePadRouting();
        setStatus(tr("Starting %1 in Steam …").arg(steamGame_.title));
        emit gameRunningChanged();
        return true;
    }

    // An emulator from Flathub starts in its setup wizard unless told what
    // the wizard would ask, and most start on the keyboard unless given the
    // pad (EmulatorSetup.h). Not fatal: at worst the wizard, or the keyboard.
    if (!plan.engineId.empty() && !omnios::prepareEmulator(plan.engineId, gamepad_.controllers()))
        qWarning("could not prepare %s's settings", plan.engineId.c_str());

    auto* process = new QProcess(this);
    captureOutput(process);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // The launcher's own Qt platform is not a game's. Emulators built on Qt
    // choose theirs, and several choose X11 because Wayland breaks them: PCSX2
    // crashed and Dolphin aborted when handed the session's "wayland".
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    for (const auto& variable : plan.environment) {
        env.insert(QString::fromStdString(variable.first),
                   QString::fromStdString(variable.second));
    }
    process->setProcessEnvironment(env);

    QStringList args;
    for (std::size_t i = 1; i < plan.argv.size(); ++i)
        args << QString::fromStdString(plan.argv[i]);

    runningTitle_ = QString::fromStdString(game->title);
    runningGameId_ = gameId;
    runningEngine_ = QString::fromStdString(plan.engineId);

    // Phase 9.4: when the game exits, the grid comes back. Without this the
    // shell would be left staring at whatever the game left on screen.
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
                focusLauncherWindow();
                setStatus(status == QProcess::CrashExit
                              ? tr("%1 crashed  —  see %2").arg(runningTitle_, kAppLog)
                          : code == 0
                              ? tr("%1 exited").arg(runningTitle_)
                              : tr("%1 exited with code %2  —  see %3")
                                    .arg(runningTitle_).arg(code).arg(kAppLog));
                running_ = nullptr;
                updatePadRouting();
                runningTitle_.clear();
                runningGameId_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                // Only a start that failed ends here. A crash is reported as
                // an error too, and then as finished, which says so with its
                // title; handled here as well, it lost the title and read
                // " exited with code 11".
                if (error != QProcess::FailedToStart) return;
                focusLauncherWindow();
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                updatePadRouting();
                runningTitle_.clear();
                runningGameId_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    connect(process, &QProcess::started, this, &LauncherController::saveRunningState);
    process->start(QString::fromStdString(plan.argv.front()), args);
    running_ = process;
    updatePadRouting();
    setStatus(tr("Starting %1 …").arg(runningTitle_));
    emit gameRunningChanged();
    offerKeyboardControls(gameId, runningTitle_);
    return true;
}

void LauncherController::offerKeyboardControls(const QString& gameId, const QString& title) {
    // Only from the keyboard: a controller's buttons are where they always are.
    if (usingController_ || gameId.isEmpty()) return;
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return;
    const omnios::LaunchPlan plan = omnios::planLaunch(*game, {});
    if (!plan.ok) return;
    QVariantList rows;
    for (const omnios::KeyHint& hint : omnios::keyboardControls(*game, plan)) {
        QStringList keys;
        for (const std::string& key : hint.keys) keys << QString::fromStdString(key);
        rows << QVariantMap{{QStringLiteral("keys"), keys}, {QStringLiteral("button"), QString::fromStdString(hint.button)}};
    }
    if (!rows.isEmpty()) emit keyboardControlsWanted(title, rows);
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
    guidePressed_.invalidate();
    stopRunning(false);

    auto* process = new QProcess(this);
    captureOutput(process);
    QElapsedTimer startedAt;
    startedAt.start();
    runningTitle_ = title;
    runningEngine_.clear();

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
                updatePadRouting();
                runningTitle_.clear();
                runningGameId_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                // Only a start that failed ends here. A crash is reported as
                // an error too, and then as finished, which says so with its
                // title; handled here as well, it lost the title and read
                // " exited with code 11".
                if (error != QProcess::FailedToStart) return;
                focusLauncherWindow();
                setStatus(tr("%1 could not start: %2").arg(runningTitle_, process->errorString()));
                running_ = nullptr;
                updatePadRouting();
                runningTitle_.clear();
                runningGameId_.clear();
                process->deleteLater();
                emit gameRunningChanged();
            });

    connect(process, &QProcess::started, this, &LauncherController::saveRunningState);
    process->start(program, args);
    running_ = process;
    updatePadRouting();
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

    // Read as it comes, so a long pacman run can say how far it has got —
    // a system update is hundreds of packages, and "Updating ..." for twenty
    // minutes looks like a hang. Kept, for the summary at the end.
    auto collected = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, collected, verb]() {
        const QByteArray chunk = process->readAllStandardOutput();
        collected->append(chunk);
        static const QRegularExpression kStep(
            QStringLiteral("^\\(\\s*(\\d+)/(\\d+)\\) (?:upgrading|installing|reinstalling|removing) (\\S+)"));
        static const QRegularExpression kDownload(QStringLiteral("^\\s*(\\S+) downloading\\.\\.\\."));
        QString latest;
        for (const QString& line : QString::fromUtf8(chunk).split(QLatin1Char('\n'))) {
            if (const auto m = kStep.match(line); m.hasMatch())
                latest = tr("%1  %2 of %3  ·  %4").arg(verb, m.captured(1), m.captured(2), m.captured(3));
            else if (const auto d = kDownload.match(line); d.hasMatch())
                latest = tr("%1  ·  downloading %2").arg(verb, d.captured(1));
        }
        if (!latest.isEmpty()) setPackageStatus(latest);
    });

    // Output is read back rather than only written to the log: a user should not
    // have to open a log to find out that a mirror was unreachable.
    connect(process, &QProcess::finished, this,
            [this, process, pastTense, collected](int code, QProcess::ExitStatus) {
                collected->append(process->readAll());
                const QString output = QString::fromUtf8(*collected).trimmed();
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
                if (updating_) {
                    // What is left to update now, and whether the kernel moved.
                    updating_ = false;
                    restartRequired_ = QFile::exists(QStringLiteral("/run/omnios/reboot-required"));
                    emit updatesChanged();
                    checkSystemUpdates();
                }
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

    // A pacman app is updated by updating the system. "pacman -Sy <pkg>", which
    // this used to run, is a partial upgrade: the databases move on, one
    // package follows them, and everything it links against stays behind —
    // Arch's best-known way to end up with a system that does not start.
    updateSystem();
}

void LauncherController::checkSystemUpdates(bool announce) {
    if (updateCheck_ != nullptr || liveImage()) return;
    auto* check = new QProcess(this);
    updateCheck_ = check;
    connect(check, &QProcess::finished, this, [this, check, announce](int code, QProcess::ExitStatus) {
        const QStringList lines = QString::fromUtf8(check->readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        updateCheck_ = nullptr;
        check->deleteLater();
        // Offline, or a mirror down: keep what was known. An error every six
        // hours about a machine that is simply offline helps nobody; asked
        // for, it is said.
        if (code != 0) {
            if (announce) setStatus(tr("Could not check for updates  -  is the network up?"));
            return;
        }
        for (const QString& line : lines) {
            if (!line.startsWith(QLatin1String("total "))) continue;
            bool ok = false;
            const int total = line.mid(6).trimmed().toInt(&ok);
            if (!ok) continue;
            if (total != updateCount_) {
                updateCount_ = total;
                emit updatesChanged();
            }
            if (announce) {
                setStatus(total == 0 ? tr("The system is up to date")
                          : total == 1 ? tr("1 update available")
                                       : tr("%1 updates available").arg(total));
            }
        }
    });
    connect(check, &QProcess::errorOccurred, this, [this, check](QProcess::ProcessError) {
        updateCheck_ = nullptr;
        check->deleteLater();
    });
    check->start(QStringLiteral("omni-update"), {QStringLiteral("check")});
}

void LauncherController::updateSystem() {
    if (packageBusy() || liveImage()) return;
    updating_ = true;
    runSystemCommand(QStringLiteral("sudo -S -p '' omni-update apply"),
                     tr("Updating the system"), QString(),
                     tr("Updating the system changes it, so it needs your password."));
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

void LauncherController::steamAction(const QString& gameId, const QString& action) {
    guidePressed_.invalidate();
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr || game->platform != omnios::Platform::Steam) return;
    // The app id becomes part of a URL handed to Steam; the reader already
    // refuses anything but digits, and this does not rely on that.
    const QString appId = QString::fromStdString(game->launchId);
    static const QRegularExpression kDigits(QStringLiteral("^[0-9]{1,12}$"));
    if (!kDigits.match(appId).hasMatch()) return;

    const QString title = QString::fromStdString(game->title);
    QString url;
    QString said;
    if (action == QLatin1String("details")) {
        url = QStringLiteral("steam://nav/games/details/") + appId;
        said = tr("Opening %1 in Steam").arg(title);
    } else if (action == QLatin1String("validate")) {
        url = QStringLiteral("steam://validate/") + appId;
        said = tr("Steam is checking %1's files").arg(title);
    } else if (action == QLatin1String("uninstall")) {
        // Steam's own dialog, which asks first. When it is done the tile goes
        // by itself: the Games tab watches Steam's library (refreshIfSteamChanged).
        url = QStringLiteral("steam://uninstall/") + appId;
        said = tr("Steam will ask before uninstalling %1").arg(title);
    } else {
        return;
    }
    // Detached and not tracked as the running app: steam hands the link to the
    // client already running, or starts it, and exits straight away.
    if (!QProcess::startDetached(QStringLiteral("steam"), {url})) {
        setPackageStatus(tr("Could not start Steam"));
        return;
    }
    setPackageStatus(said);
}

void LauncherController::quitRunningGame() { stopRunning(true); }

bool LauncherController::coverFindable(const QString& gameId) const {
    const omnios::Game* game = findGame(model_.library(), gameId);
    return game != nullptr && !omnios::thumbnailSystem(*game).empty();
}

void LauncherController::findCover(const QString& gameId) {
    const omnios::Game* game = findGame(model_.library(), gameId);
    if (game == nullptr) return;
    setStatus(tr("Looking for cover art for %1 ...").arg(QString::fromStdString(game->title)));
    covers_.retry(*game);
}

void LauncherController::uninstallGame(const QString& gameId) {
    const omnios::Game* found = findGame(model_.library(), gameId);
    if (found == nullptr) return;
    if (found->platform == omnios::Platform::Steam) {
        steamAction(gameId, QStringLiteral("uninstall"));
        return;
    }
    const omnios::Game game = *found;  // the library changes under it below
    const QString title = QString::fromStdString(game.title);
    if (runningGameId() == gameId) stopRunning(false);

    int failed = 0;
    for (const std::filesystem::path& file : omnios::gameFiles(game)) {
        if (!QFile::moveToTrash(QString::fromStdString(file.string()))) {
            qWarning("uninstall %s: could not move %s to the Trash", qPrintable(gameId), file.string().c_str());
            ++failed;
        }
    }
    QFile::remove(QString::fromStdString(omnios::coverCacheFile(game).string()));
    refresh();
    setStatus(failed == 0 ? tr("%1 moved to the Trash").arg(title)
                          : tr("%1: some of its files could not be moved to the Trash").arg(title));
}

void LauncherController::showMenu() {
    guidePressed_.invalidate();
    focusLauncherWindow();
    emit menuWanted();
}

void LauncherController::resumeRunningGame() {
    guidePressed_.invalidate();
    QStringList pids;
    if (!steamGame_.appId.isEmpty()) {
        const std::string appId = steamGame_.appId.toStdString();
        for (const omnios::SteamGameProcess& game : omnios::runningSteamGames()) {
            if (game.appId != appId) continue;
            pids << QString::number(game.pid);
            for (const int pid : omnios::descendantsOf(game.pid)) pids << QString::number(pid);
        }
    } else if (adopted_.pid != 0) {
        pids << QString::number(adopted_.pid);
        for (const int pid : omnios::descendantsOf(adopted_.pid)) pids << QString::number(pid);
    } else if (running_ != nullptr && running_->processId() > 0) {
        // A wrapper script is common (Steam's own, emulators' launchers), so
        // the window can belong to a child.
        const int root = static_cast<int>(running_->processId());
        pids << QString::number(root);
        for (const int pid : omnios::descendantsOf(root)) pids << QString::number(pid);
    }
    if (pids.isEmpty()) {
        setStatus(gameRunning() ? tr("%1 has no window yet").arg(runningTitle())
                                : tr("Nothing is running"));
        return;
    }
    QProcess::startDetached(QStringLiteral("omni-kwin-activate"),
                            QStringList{QStringLiteral("--pid")} + pids);
    offerKeyboardControls(runningGameId(), runningTitle());
}

void LauncherController::watchSteamGame() {
    if (steamGame_.appId.isEmpty()) {
        steamGamePoll_.stop();
        return;
    }
    const std::string appId = steamGame_.appId.toStdString();
    bool running = false;
    for (const omnios::SteamGameProcess& game : omnios::runningSteamGames())
        running = running || game.appId == appId;

    if (running) {
        if (!steamGame_.seen) {
            steamGame_.seen = true;
            setStatus(tr("%1 is running").arg(steamGame_.title));
        }
        return;
    }
    if (steamGame_.seen) {
        // It was running and is not: the game has ended, and the library
        // comes back, as it does for any other game.
        endSteamGame(tr("%1 exited").arg(steamGame_.title), true);
        return;
    }
    // Never seen. Steam may be updating itself or the game, installing Proton,
    // or asking something first, all in its own window: the launcher stays
    // out of the way, and after ten minutes stops waiting.
    if (steamGame_.since.elapsed() > 10 * 60 * 1000)
        endSteamGame(tr("%1 has not started  -  see Steam").arg(steamGame_.title), false);
}

void LauncherController::endSteamGame(const QString& said, bool returnHome) {
    steamGame_ = SteamGame{};
    steamGamePoll_.stop();
    updatePadRouting();
    if (returnHome) focusLauncherWindow();
    setStatus(said);
    emit gameRunningChanged();
}

QString LauncherController::runningTitle() const {
    if (!steamGame_.appId.isEmpty()) return steamGame_.title;
    if (adopted_.pid != 0) return adopted_.title;
    return runningTitle_;
}

QString LauncherController::runningGameId() const {
    if (!steamGame_.appId.isEmpty()) return steamGame_.gameId;
    if (adopted_.pid != 0) return adopted_.gameId;
    return runningGameId_;
}

namespace {

// The runtime directory lasts as long as the login, like the game it records;
// a file left from before a reboot cannot be read as a game still running.
QString runningStateFile() {
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) runtime = QDir::tempPath();
    return runtime + QStringLiteral("/omnios-running.json");
}

}  // namespace

void LauncherController::saveRunningState() {
    QJsonObject state;
    if (!steamGame_.appId.isEmpty()) {
        state.insert(QStringLiteral("steamAppId"), steamGame_.appId);
    } else {
        int pid = adopted_.pid;
        if (running_ != nullptr && running_->processId() > 0) pid = static_cast<int>(running_->processId());
        // As text: a start time in clock ticks can outgrow what JSON's
        // doubles hold exactly.
        if (const std::uint64_t start = pid > 0 ? omnios::processStartTime(pid) : 0) {
            state.insert(QStringLiteral("pid"), pid);
            state.insert(QStringLiteral("start"), QString::number(start));
        }
    }
    const QString path = runningStateFile();
    if (state.isEmpty()) {
        QFile::remove(path);
        return;
    }
    state.insert(QStringLiteral("gameId"), runningGameId());
    state.insert(QStringLiteral("title"), runningTitle());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void LauncherController::adoptRunningGame() {
    QFile file(runningStateFile());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const QString title = state.value(QStringLiteral("title")).toString();
    const QString gameId = state.value(QStringLiteral("gameId")).toString();

    if (const QString appId = state.value(QStringLiteral("steamAppId")).toString(); !appId.isEmpty()) {
        // Only a game Steam is running now. One still starting — Steam
        // updating it, say — is let go: nothing here can tell that apart
        // from one that never will.
        bool running = false;
        for (const omnios::SteamGameProcess& game : omnios::runningSteamGames())
            running = running || QString::fromStdString(game.appId) == appId;
        if (!running) {
            QFile::remove(runningStateFile());
            return;
        }
        steamGame_.appId = appId;
        steamGame_.gameId = gameId;
        steamGame_.title = title;
        steamGame_.seen = true;
        steamGame_.since.start();
        steamGamePoll_.start();
    } else {
        const int pid = state.value(QStringLiteral("pid")).toInt();
        const std::uint64_t start = state.value(QStringLiteral("start")).toString().toULongLong();
        if (pid <= 0 || start == 0 || omnios::processStartTime(pid) != start) {
            QFile::remove(runningStateFile());
            return;
        }
        adopted_ = AdoptedGame{pid, start, gameId, title};
        adoptedPoll_.start();
    }
    qInfo("adopted %s, still running from before", qPrintable(gameId));
    updatePadRouting();
    setStatus(tr("%1 is running").arg(title));
    emit gameRunningChanged();
}

void LauncherController::watchAdoptedGame() {
    if (adopted_.pid == 0) {
        adoptedPoll_.stop();
        return;
    }
    if (omnios::processStartTime(adopted_.pid) == adopted_.start) return;
    const QString title = adopted_.title;
    adopted_ = AdoptedGame{};
    adoptedPoll_.stop();
    updatePadRouting();
    focusLauncherWindow();
    setStatus(tr("%1 exited").arg(title));
    emit gameRunningChanged();
}

void LauncherController::quietCriticalNotifications(bool quiet) {
    // Plasma shows its critical notifications through Do Not Disturb unless
    // told not to — "memory shortage", say — and they stay until closed with
    // a mouse. So for the length of a game that is turned off too, and the
    // user's own setting put back after. The setting they had is kept in a
    // file, not only in memory, so a launcher that dies mid-game still puts
    // it back the next time it starts (see the constructor).
    const QString saved = QDir::homePath() + QStringLiteral("/.local/state/omnios/critical-in-dnd");
    const QStringList where = {QStringLiteral("--file"), QStringLiteral("plasmanotifyrc"),
                               QStringLiteral("--group"), QStringLiteral("Notifications"),
                               QStringLiteral("--key"), QStringLiteral("CriticalInDndMode")};
    if (quiet) {
        if (!QFile::exists(saved)) {
            QProcess read;
            read.start(QStringLiteral("kreadconfig6"), where + QStringList{QStringLiteral("--default"), QStringLiteral("true")});
            if (!read.waitForFinished(2000)) return;  // no KDE here: leave it alone
            const QString theirs = QString::fromUtf8(read.readAllStandardOutput()).trimmed();
            QDir().mkpath(QFileInfo(saved).path());
            QFile file(saved);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
            file.write(theirs.toUtf8());
        }
        QProcess::startDetached(QStringLiteral("kwriteconfig6"),
                                where + QStringList{QStringLiteral("--notify"), QStringLiteral("false")});
    } else {
        QFile file(saved);
        if (!file.open(QIODevice::ReadOnly)) return;  // never changed
        const QString theirs = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        QProcess::startDetached(QStringLiteral("kwriteconfig6"),
                                where + QStringList{QStringLiteral("--notify"),
                                                    theirs.isEmpty() ? QStringLiteral("true") : theirs});
        file.remove();
    }
}

void LauncherController::updateNotificationInhibit() {
    if (inhibitPending_) return;  // looked at again when Plasma answers
    const bool wanted = gameRunning();
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    const QString path = QStringLiteral("/org/freedesktop/Notifications");

    if (wanted && notificationInhibit_ == 0) {
        QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Inhibit"));
        call << QStringLiteral("omni-launcher") << tr("A game is running") << QVariantMap();
        inhibitPending_ = true;
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            const QDBusPendingReply<uint> reply = *w;
            w->deleteLater();
            inhibitPending_ = false;
            // Not Plasma, or none running: nothing to quieten, and nothing lost.
            if (reply.isError()) {
                qWarning("notifications not inhibited: %s", qPrintable(reply.error().message()));
                return;
            }
            notificationInhibit_ = reply.value();
            quietCriticalNotifications(true);
            updateNotificationInhibit();  // the game may have ended while asking
        });
    } else if (!wanted && notificationInhibit_ != 0) {
        QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("UnInhibit"));
        call << notificationInhibit_;
        QDBusConnection::sessionBus().asyncCall(call);
        notificationInhibit_ = 0;
        quietCriticalNotifications(false);
    }
}

void LauncherController::updatePadRouting() {
    gamepad_.setAppRunning(gameRunning()
                           && QGuiApplication::applicationState() != Qt::ApplicationActive);
    // Escape changes hands at the same moments.
    updateEscapeKey();
}

void LauncherController::updateEscapeKey() {
    const bool wanted = running_ != nullptr && omnios::escapeOpensGameMenu(runningEngine_.toStdString())
                        && QGuiApplication::applicationState() != Qt::ApplicationActive;
    // A call still running looks again when it ends.
    if (wanted == escapeForMenu_ || escapeKeyCall_ != nullptr) return;
    QString stateDir = qEnvironmentVariable("XDG_STATE_HOME");
    if (stateDir.isEmpty()) stateDir = QDir::homePath() + QStringLiteral("/.local/state");
    if (!QFile::exists(stateDir + QStringLiteral("/omnios/game-mode-meta"))) {
        // Not Game Mode's Meta, or no longer: leaving Game Mode took the
        // menu's keys away, Escape with them, and gave Meta back to Plasma.
        escapeForMenu_ = false;
        return;
    }
    escapeForMenu_ = wanted;
    // Qt's key codes: Meta alone, and Escape.
    QStringList args{QStringLiteral("--user"), QStringLiteral("call"), QStringLiteral("org.kde.kglobalaccel"),
                     QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"),
                     QStringLiteral("setForeignShortcutKeys"), QStringLiteral("asa(ai)"),
                     QStringLiteral("4"), QStringLiteral("kwin"), QStringLiteral("OmniOS Menu"),
                     QStringLiteral("KWin"), QStringLiteral("OmniOS: game menu"),
                     wanted ? QStringLiteral("2") : QStringLiteral("1"),
                     QStringLiteral("4"), QStringLiteral("16777250"), QStringLiteral("0"), QStringLiteral("0"),
                     QStringLiteral("0")};
    if (wanted) {
        args << QStringLiteral("4") << QStringLiteral("16777216") << QStringLiteral("0") << QStringLiteral("0")
             << QStringLiteral("0");
    }
    auto* call = new QProcess(this);
    escapeKeyCall_ = call;
    const auto done = [this, call]() {
        escapeKeyCall_ = nullptr;
        call->deleteLater();
        updateEscapeKey();
    };
    connect(call, &QProcess::finished, this, done);
    connect(call, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) done();
    });
    call->start(QStringLiteral("busctl"), args);
}

void LauncherController::stopRunning(bool returnHome) {
    if (!steamGame_.appId.isEmpty()) {
        // The reaper and everything under it, which is the game and whatever
        // it started. Not Steam: that stays, as it would on any PC.
        // Listed in one pass, before anything is signalled: once the reaper
        // goes, whatever it leaves behind is no longer below it.
        const std::string appId = steamGame_.appId.toStdString();
        std::vector<int> pids;
        for (const omnios::SteamGameProcess& game : omnios::runningSteamGames()) {
            if (game.appId != appId) continue;
            pids.push_back(game.pid);
            for (const int pid : omnios::descendantsOf(game.pid)) pids.push_back(pid);
        }
        stopProcesses(this, pids);
        endSteamGame(tr("%1 closed").arg(steamGame_.title), returnHome);
    }
    if (adopted_.pid != 0) {
        // Not a child of this launcher, so no QProcess to stop: the process
        // and everything under it, as for a Steam game — if it is still the
        // process that was adopted.
        std::vector<int> pids;
        if (omnios::processStartTime(adopted_.pid) == adopted_.start) {
            pids.push_back(adopted_.pid);
            for (const int pid : omnios::descendantsOf(adopted_.pid)) pids.push_back(pid);
        }
        stopProcesses(this, pids);
        const QString title = adopted_.title;
        adopted_ = AdoptedGame{};
        adoptedPoll_.stop();
        updatePadRouting();
        if (returnHome) focusLauncherWindow();
        setStatus(tr("%1 closed").arg(title));
        emit gameRunningChanged();
    }
    if (running_ == nullptr) return;

    QProcess* process = running_;

    // Detach the handlers first. The exit we are about to cause would
    // otherwise report "X exited" over the status of whatever is starting, and
    // raise the launcher again just as the new app arrives over it.
    process->disconnect(this);
    running_ = nullptr;
    updatePadRouting();
    const QString previous = runningTitle_;
    runningTitle_.clear();
    runningGameId_.clear();

    // What it started too: a game behind a wrapper script is the script's
    // child, and would outlive the script. Listed before anything is signalled,
    // while they are all still its descendants.
    const std::vector<int> children = process->processId() > 0
        ? omnios::descendantsOf(static_cast<int>(process->processId()))
        : std::vector<int>{};
    stopProcesses(this, children);
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
