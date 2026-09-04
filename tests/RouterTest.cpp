#include "Test.h"
#include "omnios/Router.h"

using namespace omnios;

namespace {

Game gameOn(Platform platform, std::string path = "/home/omni/Games/title.bin") {
    Game game;
    game.title    = "Test Title";
    game.platform = platform;
    game.path     = path;
    game.id       = makeGameId(platform, game.title);
    return game;
}

LaunchOptions plain() {
    LaunchOptions options;
    options.gameMode              = false;
    options.skipAvailabilityCheck = true;  // no emulators on a dev host
    return options;
}

}  // namespace

TEST("router: every platform routes to a registered engine") {
    for (const PlatformInfo& info : allPlatforms()) {
        const Engine* engine = defaultEngine(info.platform);
        CHECK(engine != nullptr);
        if (engine == nullptr) continue;

        const LaunchPlan plan = planLaunch(gameOn(info.platform), plain());
        CHECK(plan.ok);
        CHECK(!plan.argv.empty());
    }
}

TEST("router: a native title is executed directly") {
    const LaunchPlan plan = planLaunch(gameOn(Platform::Linux, "/home/omni/Games/pc/x"), plain());

    CHECK(plan.ok);
    CHECK_EQ(plan.engineId, std::string("native"));
    CHECK_EQ(plan.argv.size(), std::size_t(1));
    CHECK_EQ(plan.argv[0], std::string("/home/omni/Games/pc/x"));
    CHECK(plan.tier == Tier::Native);
}

TEST("router: a Windows title goes through Proton's run subcommand") {
    const LaunchPlan plan =
        planLaunch(gameOn(Platform::Windows, "/home/omni/Games/pc/game.exe"), plain());

    CHECK(plan.ok);
    CHECK_EQ(plan.engineId, std::string("proton"));
    CHECK_EQ(plan.argv.size(), std::size_t(3));
    CHECK_EQ(plan.argv[0], std::string("proton"));
    CHECK_EQ(plan.argv[1], std::string("run"));
}

TEST("router: PS4 and PS5 both reach the Orbis layer") {
    CHECK_EQ(planLaunch(gameOn(Platform::PS4), plain()).engineId, std::string("shadps4"));
    CHECK_EQ(planLaunch(gameOn(Platform::PS5), plain()).engineId, std::string("shadps4"));
}

TEST("router: an APK is installed into the Waydroid container") {
    const LaunchPlan plan =
        planLaunch(gameOn(Platform::Android, "/home/omni/Games/android/g.apk"), plain());

    CHECK(plan.ok);
    CHECK_EQ(plan.argv[0], std::string("waydroid"));
    CHECK_EQ(plan.argv[1], std::string("app"));
    CHECK_EQ(plan.argv[2], std::string("install"));
}

TEST("router: a directory title launches its executable, not the folder") {
    Game game = gameOn(Platform::Windows, "/home/omni/Games/pc/Hades");
    game.executable = "Hades.exe";

    const LaunchPlan plan = planLaunch(game, plain());
    CHECK_EQ(plan.target, std::string("/home/omni/Games/pc/Hades/Hades.exe"));
    CHECK_EQ(plan.argv.back(), std::string("/home/omni/Games/pc/Hades/Hades.exe"));
}

TEST("router: wrappers are applied outermost first") {
    LaunchOptions options = plain();
    options.gameMode = true;
    options.mangoHud = true;

    const LaunchPlan plan = planLaunch(gameOn(Platform::PS2), options);
    CHECK_EQ(plan.argv[0], std::string("gamemoderun"));
    CHECK_EQ(plan.argv[1], std::string("mangohud"));
    CHECK_EQ(plan.argv[2], std::string("pcsx2"));
    CHECK_EQ(plan.environment.at("MANGOHUD"), std::string("1"));
}

TEST("router: the package's engine preference overrides the platform default") {
    Game game = gameOn(Platform::Windows);
    game.engineOverride = "wine";

    const LaunchPlan plan = planLaunch(game, plain());
    CHECK_EQ(plan.engineId, std::string("wine"));
}

TEST("router: an explicit request overrides the package's preference") {
    Game game = gameOn(Platform::Windows);
    game.engineOverride = "wine";

    LaunchOptions options = plain();
    options.engine = "proton";

    CHECK_EQ(planLaunch(game, options).engineId, std::string("proton"));
}

TEST("router: an unknown platform fails with an explanation, not a crash") {
    const LaunchPlan plan = planLaunch(gameOn(Platform::Unknown), plain());

    CHECK(!plan.ok);
    CHECK(plan.argv.empty());
    CHECK(plan.error.find("Test Title") != std::string::npos);
}

TEST("router: an unregistered engine name is rejected") {
    LaunchOptions options = plain();
    options.engine = "yuzu";

    const LaunchPlan plan = planLaunch(gameOn(Platform::Switch), options);
    CHECK(!plan.ok);
    CHECK(plan.error.find("yuzu") != std::string::npos);
}

TEST("router: a missing engine yields an install hint") {
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;

    // No emulator is installed on a build host, so this exercises the real path.
    const LaunchPlan plan = planLaunch(gameOn(Platform::PS3), options);
    if (!plan.ok) {
        CHECK(plan.error.find("RPCS3") != std::string::npos);
        CHECK_EQ(plan.installHint, std::string("yay -S rpcs3-bin"));
    }
}

TEST("router: a game with no file on disk cannot be launched") {
    Game game = gameOn(Platform::PS2);
    game.path.clear();

    const LaunchPlan plan = planLaunch(game, plain());
    CHECK(!plan.ok);
}

TEST("router: the command line quotes paths containing spaces") {
    const LaunchPlan plan =
        planLaunch(gameOn(Platform::PS2, "/home/omni/Games/ps2/Shadow of the Colossus.iso"),
                   plain());

    CHECK(plan.commandLine().find("\"/home/omni/Games/ps2/Shadow of the Colossus.iso\"") !=
          std::string::npos);
}

TEST("router: every engine row is complete") {
    for (const Engine& engine : allEngines()) {
        CHECK(!engine.id.empty());
        CHECK(!engine.displayName.empty());
        // Only the native engine has no command of its own.
        CHECK(engine.id == "native" || !engine.command.empty());
        CHECK(engine.id == "native" || !engine.package.empty());
        CHECK(findEngine(engine.id) != nullptr);
    }
}
