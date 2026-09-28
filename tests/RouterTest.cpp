#include "Test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include "omnios/Router.h"

using namespace omnios;
namespace fs = std::filesystem;

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

        // RetroArch's systems are told apart by the file, so give it one of
        // theirs; everything else takes the generic name.
        const bool retroArch = engine->id == std::string_view("retroarch");
        const LaunchPlan plan = planLaunch(
            retroArch ? gameOn(info.platform, "/home/omni/Games/retro/title.nes") : gameOn(info.platform),
            plain());
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
    CHECK_EQ(plan.argv[2], std::string("pcsx2-qt"));
    CHECK_EQ(plan.argv[3], std::string("-batch"));
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
        CHECK_EQ(plan.installHint, std::string("flatpak install flathub net.rpcs3.RPCS3"));
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

TEST("router: RetroArch is given the core for the file, full screen") {
    const LaunchPlan plan =
        planLaunch(gameOn(Platform::GBA, "/home/omni/Games/gba/Some Game.gba"), plain());
    CHECK(plan.ok);
    CHECK_EQ(plan.engineId, std::string("retroarch"));
    CHECK_EQ(plan.argv.size(), std::size_t(6));
    CHECK_EQ(plan.argv[0], std::string("retroarch"));
    CHECK_EQ(plan.argv[1], std::string("--fullscreen"));
    CHECK_EQ(plan.argv[2], std::string("--appendconfig=/usr/share/omnios/retroarch.cfg"));
    CHECK_EQ(plan.argv[3], std::string("-L"));
    CHECK_EQ(plan.argv[4], std::string("/usr/lib/libretro/mgba_libretro.so"));
    CHECK_EQ(plan.argv[5], std::string("/home/omni/Games/gba/Some Game.gba"));

    CHECK(planLaunch(gameOn(Platform::Retro, "/g/a.NES"), plain()).argv[4].find("nestopia") != std::string::npos);
    CHECK(planLaunch(gameOn(Platform::Retro, "/g/a.sfc"), plain()).argv[4].find("snes9x") != std::string::npos);
    CHECK(planLaunch(gameOn(Platform::Retro, "/g/a.z64"), plain()).argv[4].find("mupen64plus_next") != std::string::npos);
    CHECK(planLaunch(gameOn(Platform::Retro, "/g/a.md"), plain()).argv[4].find("genesis_plus_gx") != std::string::npos);
}

TEST("router: a file RetroArch has no core for is refused, not opened in its menu") {
    const LaunchPlan plan = planLaunch(gameOn(Platform::Retro, "/home/omni/Games/retro/x.bin"), plain());
    CHECK(!plan.ok);
    CHECK(plan.error.find("RetroArch") != std::string::npos);
}

TEST("router: a missing RetroArch core says which package brings it") {
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    // RetroArch itself may be absent too on a dev host; only look when it is not.
    if (!commandExists("retroarch")) return;
    const LaunchPlan plan = planLaunch(gameOn(Platform::Retro, "/g/a.nes"), options);
    if (plan.ok) return;  // this machine has the core
    CHECK_EQ(plan.installHint, std::string("sudo pacman -S libretro-nestopia"));
}

TEST("router: Dolphin boots the game straight away, full screen") {
    const LaunchPlan plan =
        planLaunch(gameOn(Platform::GameCube, "/home/omni/Games/gamecube/demo.dol"), plain());
    CHECK(plan.ok);
    CHECK_EQ(plan.argv.front(), std::string("dolphin-emu"));
    CHECK_EQ(plan.argv[1], std::string("-b"));
    CHECK_EQ(plan.argv[plan.argv.size() - 2], std::string("-e"));
    CHECK_EQ(plan.argv.back(), std::string("/home/omni/Games/gamecube/demo.dol"));
    CHECK_EQ(plan.environment.at("QT_QPA_PLATFORM"), std::string("xcb"));
}

namespace {

// Points HOME and the games folder at a scratch directory for one test.
struct ScratchHome {
    fs::path root = fs::temp_directory_path() / "omnios-router-home";
    std::string oldHome = std::getenv("HOME") ? std::getenv("HOME") : "";
    ScratchHome() {
        fs::remove_all(root);
        fs::create_directories(root / "Games");
        set("HOME", root.string());
        set("OMNIOS_GAMES_DIR", (root / "Games").string());
    }
    ~ScratchHome() {
        set("OMNIOS_GAMES_DIR", "");
        set("HOME", oldHome);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    static void set(const char* name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        if (value.empty()) unsetenv(name); else setenv(name, value.c_str(), 1);
#endif
    }
};

}  // namespace

TEST("router: an emulator only on Flathub is offered for install") {
    if (commandExists("pcsx2-qt")) return;  // installed natively here
    ScratchHome home;
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    const LaunchPlan plan = planLaunch(gameOn(Platform::PS2, (home.root / "Games/ps2/g.iso").string()), options);
    CHECK(!plan.ok);
    CHECK_EQ(plan.flatpakApp, std::string("net.pcsx2.PCSX2"));
    CHECK_EQ(plan.installHint, std::string("flatpak install flathub net.pcsx2.PCSX2"));
}

TEST("router: an emulator installed from Flathub runs through flatpak, with the games folder") {
    if (commandExists("pcsx2-qt")) return;
    ScratchHome home;
    fs::create_directories(home.root / ".local/share/flatpak/app/net.pcsx2.PCSX2/current");
    fs::create_directories(home.root / "Games/bios");
    { std::ofstream(home.root / "Games/bios/scph.bin") << std::string(4 * 1024 * 1024, 'x'); }  // a PS2 BIOS's size
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    const std::string game = (home.root / "Games/ps2/g.iso").string();
    const LaunchPlan plan = planLaunch(gameOn(Platform::PS2, game), options);
    CHECK(plan.ok);
    CHECK_EQ(plan.argv[0], std::string("flatpak"));
    CHECK_EQ(plan.argv[1], std::string("run"));
    CHECK(plan.argv[2].rfind("--filesystem=", 0) == 0);
    CHECK_EQ(plan.argv[3], std::string("net.pcsx2.PCSX2"));
    CHECK_EQ(plan.argv[4], std::string("-batch"));
    CHECK_EQ(plan.argv.back(), fs::path(game).generic_string());
}

TEST("router: a PS1 or PS2 game with no BIOS says where to put one") {
    if (commandExists("pcsx2-qt")) return;
    ScratchHome home;
    fs::create_directories(home.root / ".local/share/flatpak/app/net.pcsx2.PCSX2/current");
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    const LaunchPlan plan = planLaunch(gameOn(Platform::PS2, (home.root / "Games/ps2/g.iso").string()), options);
    CHECK(!plan.ok);
    CHECK(plan.error.find("Games/bios") != std::string::npos);
    CHECK(plan.flatpakApp.empty());
}

TEST("router: a PS1 game plays on the free BIOS; a PS2 game needs a PS2 BIOS") {
    if (commandExists("pcsx2-qt") || commandExists("duckstation-qt")) return;
    ScratchHome home;
    fs::create_directories(home.root / ".local/share/flatpak/app/net.pcsx2.PCSX2/current");
    fs::create_directories(home.root / ".local/share/flatpak/app/org.duckstation.DuckStation/current");
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    const auto ps1 = [&] { return planLaunch(gameOn(Platform::PS1, (home.root / "Games/ps1/g.cue").string()), options); };
    const auto ps2 = [&] { return planLaunch(gameOn(Platform::PS2, (home.root / "Games/ps2/g.iso").string()), options); };

    ScratchHome::set("OMNIOS_OPENBIOS", (home.root / "none.bin").string());
    CHECK(!ps1().ok);  // no OpenBIOS on this image, and none of the user's

    const fs::path openbios = home.root / "openbios.bin";
    { std::ofstream(openbios) << std::string(512 * 1024, 'x'); }
    ScratchHome::set("OMNIOS_OPENBIOS", openbios.string());
    CHECK(ps1().ok);

    // A PS1 BIOS, or OpenBIOS itself, in Games/bios is not a PS2 BIOS, nor is
    // the PS3's update that sits beside them.
    fs::create_directories(home.root / "Games/bios");
    fs::copy_file(openbios, home.root / "Games/bios/openbios.bin");
    { std::ofstream(home.root / "Games/bios/PS3UPDAT.PUP") << std::string(9 * 1024 * 1024, 'p'); }
    CHECK(!ps2().ok);
    { std::ofstream(home.root / "Games/bios/scph39001.bin") << std::string(4 * 1024 * 1024, 's'); }
    CHECK(ps2().ok);
    ScratchHome::set("OMNIOS_OPENBIOS", "");
}

namespace {

// A Flathub install of `app` under the scratch home.
void fakeFlatpak(const ScratchHome& home, const std::string& app) {
    fs::create_directories(home.root / ".local/share/flatpak/app" / app / "current");
}

void touch(const fs::path& file) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

LaunchOptions checked() {
    LaunchOptions options = plain();
    options.skipAvailabilityCheck = false;
    return options;
}

}  // namespace

TEST("router: a PS3 game needs the system software, then offers to install it, then plays") {
    if (commandExists("rpcs3")) return;
    ScratchHome home;
    fakeFlatpak(home, "net.rpcs3.RPCS3");
    const std::string eboot = (home.root / "Games/ps3/G/USRDIR/EBOOT.BIN").string();

    LaunchPlan plan = planLaunch(gameOn(Platform::PS3, eboot), checked());
    CHECK(!plan.ok);
    CHECK(plan.error.find("PS3UPDAT.PUP") != std::string::npos);
    CHECK(plan.setupArgv.empty());

    touch(home.root / "Games/bios/PS3UPDAT.PUP");
    plan = planLaunch(gameOn(Platform::PS3, eboot), checked());
    CHECK(!plan.ok);
    CHECK_EQ(plan.setupLabel, std::string("Install PS3 system software"));
    CHECK(std::find(plan.setupArgv.begin(), plan.setupArgv.end(), "--installfw") != plan.setupArgv.end());

    touch(home.root / ".var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash/vsh/module/vsh.self");
    plan = planLaunch(gameOn(Platform::PS3, eboot), checked());
    CHECK(plan.ok);
    CHECK_EQ(plan.argv[4], std::string("--no-gui"));
    CHECK_EQ(plan.argv[5], std::string("--fullscreen"));
}

TEST("router: a Switch game needs the user's keys") {
    if (commandExists("Ryujinx")) return;
    ScratchHome home;
    fakeFlatpak(home, "io.github.ryubing.Ryujinx");
    const std::string nsp = (home.root / "Games/switch/g.nsp").string();

    LaunchPlan plan = planLaunch(gameOn(Platform::Switch, nsp), checked());
    CHECK(!plan.ok);
    CHECK(plan.error.find("prod.keys") != std::string::npos);

    touch(home.root / "Games/bios/prod.keys");
    plan = planLaunch(gameOn(Platform::Switch, nsp), checked());
    CHECK(plan.ok);
    CHECK_EQ(plan.argv[4], std::string("--fullscreen"));
    CHECK_EQ(plan.argv[5], std::string("--hide-updates"));
}

TEST("router: shadPS4 and Azahar start their game full screen") {
    if (commandExists("shadps4") || commandExists("azahar")) return;
    ScratchHome home;
    fakeFlatpak(home, "net.shadps4.shadPS4");
    fakeFlatpak(home, "org.azahar_emu.Azahar");

    const LaunchPlan ps4 = planLaunch(gameOn(Platform::PS4, (home.root / "Games/ps4/C/eboot.bin").string()), checked());
    CHECK(ps4.ok);
    CHECK_EQ(ps4.argv[4], std::string("-d"));
    CHECK_EQ(ps4.argv[5], std::string("-g"));
    CHECK_EQ(ps4.argv[ps4.argv.size() - 2], std::string("--fullscreen"));
    CHECK_EQ(ps4.environment.at("TRACY_NO_INVARIANT_CHECK"), std::string("1"));

    const LaunchPlan n3ds = planLaunch(gameOn(Platform::N3DS, (home.root / "Games/3ds/g.3ds").string()), checked());
    CHECK(n3ds.ok);
    CHECK_EQ(n3ds.argv[4], std::string("-f"));
}
