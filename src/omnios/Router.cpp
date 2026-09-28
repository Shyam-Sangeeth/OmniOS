#include "Router.h"

#include "Detector.h"
#include "Paths.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace omnios {
namespace {

namespace fs = std::filesystem;

// Engine registry. One row per execution layer (OmniOS.md §20).
//
// Switch and 3DS point at maintained forks: upstream Ryujinx shut down in
// October 2024 and Citra was taken down in March 2024, so the AUR packages the
// design doc names no longer build. The ids stay generic so a future swap is a
// one-line change here.
const std::vector<Engine> kEngines = {
    {"native", "Native", "", "", Tier::Native,
     "runs directly on the host CPU"},
    // Booting the ISO showed Proton as "not installed" even though steam is on
    // the image, and that is accurate: Proton is not a binary on PATH. Steam
    // downloads it into ~/.steam/root/compatibilitytools.d and runs it through
    // its own runtime, so detecting and launching it needs to go through Steam
    // rather than exec a command. Left as-is until that path is built; the
    // failure is at least honest and the install hint is right.
    {"proton", "Proton", "proton", "steam", Tier::ApiLayer,
     "Windows NT API layer with DXVK/VKD3D"},
    {"wine", "Wine", "wine", "wine", Tier::ApiLayer,
     "Windows API layer without Steam's runtime"},
    {"shadps4", "shadPS4", "shadps4", "shadps4-bin", Tier::ApiLayer,
     "PS4 Orbis API layer", "net.shadps4.shadPS4"},
    {"ryubing", "Ryubing", "Ryujinx", "ryubing", Tier::Jit,
     "Switch ARM64 JIT (maintained Ryujinx fork)", "io.github.ryubing.Ryujinx"},
    // Steam starts its own games. OmniOS hands it an app id and gets out of
    // the way, which is also the only thing that works: a Steam game needs the
    // client running for its DRM, its overlay and its cloud saves, so running
    // the executable directly is not an option even when you can find it.
    {"steam", "Steam", "steam", "steam", Tier::Native,
     "Steam runs the game itself, with its own runtime."},

    {"rpcs3", "RPCS3", "rpcs3", "rpcs3-bin", Tier::Emulator,
     "PS3 Cell/PowerPC recompiler", "net.rpcs3.RPCS3"},
    // PCSX2 was dropped from the official repos, so the install hint has to
    // point at the AUR. PCSX2 2.x ships its binary as pcsx2-qt, matching
    // DuckStation's naming.
    {"pcsx2", "PCSX2", "pcsx2-qt", "pcsx2-git", Tier::Emulator,
     "PS2 MIPS recompiler", "net.pcsx2.PCSX2"},
    {"duckstation", "DuckStation", "duckstation-qt", "duckstation-bin", Tier::Emulator,
     "PS1 MIPS dynarec", "org.duckstation.DuckStation"},
    {"dolphin", "Dolphin", "dolphin-emu", "dolphin-emu", Tier::Emulator,
     "GameCube and Wii PowerPC JIT"},
    {"azahar", "Azahar", "azahar", "azahar-bin", Tier::Emulator,
     "3DS emulator (maintained Citra successor)", "org.azahar_emu.Azahar"},
    {"retroarch", "RetroArch", "retroarch", "retroarch", Tier::Emulator,
     "multi-system frontend for retro cores"},
    {"waydroid", "Waydroid", "waydroid", "waydroid", Tier::Jit,
     "Android LXC container with ARM translation"},
};

struct RouteRow {
    Platform         platform;
    std::string_view engineId;
};

// Platform -> default engine. PS5 deliberately routes to shadPS4 as the closest
// available Orbis layer; it only runs a subset of PS5 titles today, which the
// launcher surfaces as a warning rather than pretending support is complete.
const RouteRow kRoutes[] = {
    {Platform::Linux,    "native"},
    {Platform::Windows,  "proton"},
    {Platform::PS5,      "shadps4"},
    {Platform::PS4,      "shadps4"},
    {Platform::Steam,    "steam"},
    {Platform::PS3,      "rpcs3"},
    {Platform::PS2,      "pcsx2"},
    {Platform::PS1,      "duckstation"},
    {Platform::Switch,   "ryubing"},
    {Platform::GameCube, "dolphin"},
    {Platform::Wii,      "dolphin"},
    {Platform::N3DS,     "azahar"},
    {Platform::GBA,      "retroarch"},
    {Platform::Android,  "waydroid"},
    {Platform::Retro,    "retroarch"},
};

// RetroArch plays nothing by itself: each system is a core, a library it
// loads, and started without one it opens its own menu instead of the game. So
// the core is chosen here, by the file's extension. All of these are in Arch's
// repositories, and on the image.
struct CoreRow {
    std::string_view extension;
    std::string_view core;     // /usr/lib/libretro/<core>_libretro.so
    std::string_view package;  // the Arch package that provides it
};

const CoreRow kCores[] = {
    {"nes", "nestopia",          "libretro-nestopia"},
    {"sfc", "snes9x",            "libretro-snes9x"},
    {"smc", "snes9x",            "libretro-snes9x"},
    {"gb",  "mgba",              "libretro-mgba"},
    {"gbc", "mgba",              "libretro-mgba"},
    {"gba", "mgba",              "libretro-mgba"},
    {"nds", "melonds",           "libretro-melonds"},
    {"n64", "mupen64plus_next",  "libretro-mupen64plus-next"},
    {"z64", "mupen64plus_next",  "libretro-mupen64plus-next"},
    {"v64", "mupen64plus_next",  "libretro-mupen64plus-next"},
    {"md",  "genesis_plus_gx",   "libretro-genesis-plus-gx"},
    {"gen", "genesis_plus_gx",   "libretro-genesis-plus-gx"},
    {"smd", "genesis_plus_gx",   "libretro-genesis-plus-gx"},
    {"sms", "genesis_plus_gx",   "libretro-genesis-plus-gx"},
    {"gg",  "genesis_plus_gx",   "libretro-genesis-plus-gx"},
};

const CoreRow* coreFor(std::string_view extension) {
    for (const CoreRow& row : kCores)
        if (row.extension == extension) return &row;
    return nullptr;
}

fs::path libretroDir() {
    const char* override = std::getenv("OMNIOS_LIBRETRO_DIR");
    return override != nullptr && *override != '\0' ? fs::path(override) : fs::path("/usr/lib/libretro");
}

// OmniOS's own RetroArch settings, laid over the user's: full screen, and a
// way back to the menu (and from there, out) with a controller alone.
constexpr std::string_view kRetroArchConfig = "/usr/share/omnios/retroarch.cfg";

// Wrappers, outermost first: gamemoderun raises scheduling priority for
// everything below it, mangohud injects the overlay into the actual renderer.
constexpr std::string_view kGameModeCommand = "gamemoderun";
constexpr std::string_view kMangoHudCommand = "mangohud";

bool needsQuoting(const std::string& argument) {
    return argument.empty() ||
           argument.find_first_of(" \t\"'\\$&|<>();") != std::string::npos;
}

}  // namespace

std::string LaunchPlan::commandLine() const {
    std::string line;
    for (const auto& entry : environment) {
        line += entry.first + "=" + entry.second + " ";
    }
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) line.push_back(' ');
        if (needsQuoting(argv[i])) {
            line.push_back('"');
            for (const char c : argv[i]) {
                if (c == '"' || c == '\\') line.push_back('\\');
                line.push_back(c);
            }
            line.push_back('"');
        } else {
            line += argv[i];
        }
    }
    return line;
}

const std::vector<Engine>& allEngines() { return kEngines; }

const Engine* findEngine(std::string_view id) {
    const auto it = std::find_if(kEngines.begin(), kEngines.end(),
                                 [id](const Engine& engine) { return engine.id == id; });
    return it == kEngines.end() ? nullptr : &*it;
}

const Engine* defaultEngine(Platform platform) {
    for (const RouteRow& row : kRoutes) {
        if (row.platform == platform) return findEngine(row.engineId);
    }
    return nullptr;
}

bool flatpakInstalled(std::string_view appId) {
    if (appId.empty()) return false;
    const std::string id(appId);
    std::error_code ec;
    // An app is installed when its "current" deployment exists, per user or
    // system-wide; Flatpak removes the link when it uninstalls.
    return fs::exists(homeDir() / ".local/share/flatpak/app" / id / "current", ec) ||
           fs::exists(fs::path("/var/lib/flatpak/app") / id / "current", ec);
}

fs::path biosDir() { return gamesDir() / "bios"; }

fs::path openBiosImage() {
    if (const char* path = std::getenv("OMNIOS_OPENBIOS"); path != nullptr && *path != '\0') return path;
    return "/usr/share/omnios/bios/openbios.bin";
}

fs::path flatpakConfigDir(std::string_view appId) {
    return homeDir() / ".var/app" / std::string(appId) / "config";
}

bool commandExists(std::string_view command) {
    if (command.empty()) return false;

    const char* raw = std::getenv("PATH");
    if (raw == nullptr) return false;

    // Windows dev hosts separate PATH entries with ';' and need the extension.
#ifdef _WIN32
    constexpr char kSeparator = ';';
    const std::vector<std::string> suffixes = {".exe", ".cmd", ".bat", ""};
#else
    constexpr char kSeparator = ':';
    const std::vector<std::string> suffixes = {""};
#endif

    const std::string path(raw);
    std::size_t       start = 0;
    std::error_code   ec;

    while (start <= path.size()) {
        const std::size_t end = path.find(kSeparator, start);
        const std::string entry =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!entry.empty()) {
            for (const std::string& suffix : suffixes) {
                const fs::path candidate = fs::path(entry) / (std::string(command) + suffix);
                if (fs::is_regular_file(candidate, ec)) return true;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

LaunchPlan planLaunch(const Game& game, const LaunchOptions& options) {
    LaunchPlan plan;

    if (game.platform == Platform::Unknown) {
        plan.error = "OmniOS could not tell what platform \"" + game.title +
                     "\" is for, so it has no way to launch it.";
        return plan;
    }

    // Precedence: explicit request > the package's own preference > the
    // platform default. A packager knows more than the table; a user knows more
    // than the packager.
    std::string requestedId = options.engine;
    if (requestedId.empty()) requestedId = game.engineOverride;

    const Engine* engine = nullptr;
    if (!requestedId.empty()) {
        engine = findEngine(requestedId);
        if (engine == nullptr) {
            plan.error = "No engine named \"" + requestedId + "\" is registered.";
            return plan;
        }
    } else {
        engine = defaultEngine(game.platform);
        if (engine == nullptr) {
            plan.error = "No execution layer is registered for " +
                         std::string(platformDisplayName(game.platform)) + ".";
            return plan;
        }
    }

    plan.engineId          = std::string(engine->id);
    plan.engineDisplayName = std::string(engine->displayName);
    plan.tier              = engine->tier;

    // Resolve what actually gets handed to the engine: a directory-based title
    // points at its executable, everything else at the file itself.
    fs::path target = game.path;
    if (!game.executable.empty()) target /= game.executable;
    plan.target = target.generic_string();

    // A path is only required when a path is how the thing is addressed. A
    // Steam game is started by app id, and Steam owns the files: refusing for
    // want of a path would block the one launch that cannot use one.
    if (game.path.empty() && game.launchId.empty()) {
        plan.error = "\"" + game.title + "\" has no file on disk to launch.";
        return plan;
    }

    // Installed natively (from the AUR, by hand) or from Flathub; the native
    // one first, since someone installed it on purpose. Described without
    // checking, a Flathub install is still described as one.
    bool viaFlatpak = options.skipAvailabilityCheck && !engine->command.empty() &&
                      !engine->flatpak.empty() && !commandExists(engine->command) &&
                      flatpakInstalled(engine->flatpak);
    if (!options.skipAvailabilityCheck && !engine->command.empty() &&
        !commandExists(engine->command)) {
        if (flatpakInstalled(engine->flatpak)) {
            viaFlatpak = true;
        } else {
            plan.error = std::string(engine->displayName) + " is not installed, so " +
                         game.title + " cannot start.";
            if (!engine->flatpak.empty()) {
                plan.flatpakApp = std::string(engine->flatpak);
                plan.installHint = "flatpak install flathub " + plan.flatpakApp;
            } else if (!engine->package.empty()) {
                plan.installHint = "yay -S " + std::string(engine->package);
            }
            return plan;
        }
    }

    // PS1 and PS2 games start from a BIOS. Sony's cannot come with OmniOS.
    // For the PS1 there is a free one, OpenBIOS, on the image: DuckStation
    // plays with it when the user has put none of their own in Games/bios
    // (EmulatorSetup copies it there). Nothing like it exists for the PS2,
    // whose BIOS is a 4 MB file only a console of their own can give them.
    // Said here, in words, rather than by an emulator's error dialog after it
    // has taken over the screen.
    if (!options.skipAvailabilityCheck && (engine->id == "duckstation" || engine->id == "pcsx2")) {
        // A PS2 BIOS is 4 MB (8 with its extra ROMs); a PS1 one 512 KB. By
        // size, since the same folder holds the PS3's 200 MB update and the
        // Switch's small keys.
        const bool ps2 = engine->id == "pcsx2";
        const std::uintmax_t minimum = ps2 ? 4u * 1024 * 1024 : 512u * 1024;
        const std::uintmax_t maximum = ps2 ? 8u * 1024 * 1024 : 512u * 1024;
        std::error_code ec;
        bool found = !ps2 && fs::exists(openBiosImage(), ec);
        for (fs::directory_iterator it(biosDir(), ec), end; !found && !ec && it != end; it.increment(ec)) {
            const std::uintmax_t size = it->is_regular_file(ec) ? it->file_size(ec) : 0;
            if (size >= minimum && size <= maximum) found = true;
        }
        if (!found) {
            plan.error = std::string(platformDisplayName(game.platform)) +
                         " games need the BIOS from your own console. Copy it into Games/bios, then play.";
            return plan;
        }
    }

    // What the Switch and the PS3 need of their own, looked for where their
    // Flathub installs keep it. (A native install keeps it elsewhere, and is
    // someone who set it up by hand.)
    if (!options.skipAvailabilityCheck && viaFlatpak) {
        std::error_code ec;
        if (engine->id == "ryubing") {
            // Switch games are encrypted with the console's keys, which only
            // the user's own Switch can give. prepareEmulator() copies them
            // from Games/bios into place.
            if (!fs::exists(biosDir() / "prod.keys", ec) &&
                !fs::exists(flatpakConfigDir(engine->flatpak) / "Ryujinx/system/prod.keys", ec)) {
                plan.error = "Switch games need the prod.keys file from your own Switch. Copy it into "
                             "Games/bios, then play.";
                return plan;
            }
        } else if (engine->id == "rpcs3") {
            // PS3 games run on the PS3's own system software, which Sony gives
            // away as PS3UPDAT.PUP. RPCS3 installs it once, and asks first.
            if (!fs::exists(flatpakConfigDir(engine->flatpak) / "rpcs3/dev_flash/vsh/module/vsh.self", ec)) {
                const fs::path pup = biosDir() / "PS3UPDAT.PUP";
                if (!fs::exists(pup, ec)) {
                    plan.error = "PS3 games need the PS3 system software. Download PS3UPDAT.PUP from "
                                 "playstation.com into Games/bios, then play.";
                    return plan;
                }
                plan.error = "The PS3 system software is not installed in RPCS3 yet. Install it once: RPCS3 "
                             "asks to confirm, and a mouse or a keyboard (Alt+Y) answers.";
                plan.setupLabel = "Install PS3 system software";
                plan.setupArgv = {"flatpak", "run", "--filesystem=" + biosDir().generic_string() + ":ro",
                                  std::string(engine->flatpak), "--installfw", pup.generic_string()};
                return plan;
            }
        }
    }

    // RetroArch needs its core, which is a file, not a command.
    const CoreRow* core = nullptr;
    fs::path corePath;
    if (engine->id == "retroarch") {
        core = coreFor(fileExtension(target.filename().string()));
        if (core == nullptr) {
            plan.error = "RetroArch has no system for \"" + target.filename().string() +
                         "\" here, so " + game.title + " cannot start.";
            return plan;
        }
        corePath = libretroDir() / (std::string(core->core) + "_libretro.so");
        std::error_code ec;
        if (!options.skipAvailabilityCheck && !fs::is_regular_file(corePath, ec)) {
            plan.error = "RetroArch's " + std::string(core->core) + " core is not installed, so " +
                         game.title + " cannot start.";
            plan.installHint = "sudo pacman -S " + std::string(core->package);
            return plan;
        }
    }

    std::vector<std::string> argv;

    // Only wrap an engine that runs the game itself. Putting gamemoderun in
    // front of "steam" governs the client, not the game — Steam starts that as
    // a child of its own, out of reach — so the wrapper would cost a process
    // and buy nothing. MANGOHUD stays in the environment below, because that
    // one children do inherit.
    //
    // Nor one run from Flathub: both wrappers work by preloading a library of
    // the host's, which ends up inside the sandbox, built against another
    // system's libraries. There GameMode's could not register the game, and
    // PCSX2 crashed on start under it.
    const bool enginePlaysTheGame = engine->id != "steam" && !viaFlatpak;
    if (enginePlaysTheGame && options.gameMode) argv.emplace_back(kGameModeCommand);
    if (enginePlaysTheGame && options.mangoHud) argv.emplace_back(kMangoHudCommand);

    if (engine->id == "steam") {
        // steam://rungameid is the documented way in, and it is what a desktop
        // shortcut created by Steam itself uses. It starts the client first
        // when it is not already running, which a console needs: the tile is
        // often the first thing touched after a boot.
        argv.emplace_back(std::string(engine->command));
        argv.push_back("steam://rungameid/" + game.launchId);
    } else if (engine->id == "native") {
        argv.push_back(plan.target);
    } else if (engine->id == "proton") {
        // Proton's own CLI: `proton run <exe>` (OmniOS.md §20).
        argv.emplace_back(std::string(engine->command));
        argv.emplace_back("run");
        argv.push_back(plan.target);
    } else if (engine->id == "waydroid") {
        // An APK is installed into the container, then launched by package id;
        // the install step is the useful half of a first launch.
        argv.emplace_back(std::string(engine->command));
        argv.emplace_back("app");
        argv.emplace_back("install");
        argv.push_back(plan.target);
    } else if (engine->id == "retroarch") {
        argv.emplace_back(std::string(engine->command));
        argv.emplace_back("--fullscreen");
        argv.push_back("--appendconfig=" + std::string(kRetroArchConfig));
        argv.emplace_back("-L");
        argv.push_back(corePath.generic_string());
        argv.push_back(plan.target);
    } else if (engine->id == "dolphin") {
        // Straight into the game (-e), full screen, and gone when it stops
        // (-b, batch): Dolphin otherwise opens its game list first and stays
        // open after, neither of which a console wants.
        argv.emplace_back(std::string(engine->command));
        argv.emplace_back("-b");
        argv.emplace_back("-C");
        argv.emplace_back("Dolphin.Display.Fullscreen=True");
        argv.emplace_back("-C");
        argv.emplace_back("Dolphin.Interface.ConfirmStop=False");
        argv.emplace_back("-e");
        argv.push_back(plan.target);
    } else {
        if (viaFlatpak) {
            // In its sandbox an app sees only what it is given, and most of
            // these are given nothing of the home folder: ~/Games, to read.
            argv.emplace_back("flatpak");
            argv.emplace_back("run");
            argv.push_back("--filesystem=" + gamesDir().generic_string() + ":ro");
            argv.emplace_back(std::string(engine->flatpak));
        } else {
            argv.emplace_back(std::string(engine->command));
        }
        // Straight into the game, full screen, and gone when it ends, rather
        // than into the emulator's own game list.
        // Each says it its own way (their --help, read on the image).
        if (engine->id == "duckstation" || engine->id == "pcsx2") {
            argv.emplace_back("-batch");
            argv.emplace_back("-fullscreen");
            argv.emplace_back("--");
            argv.push_back(plan.target);
        } else if (engine->id == "rpcs3") {
            argv.emplace_back("--no-gui");
            argv.emplace_back("--fullscreen");
            argv.push_back(plan.target);
        } else if (engine->id == "shadps4") {
            // Its launcher (-g, the game; -d, its default emulator build) and,
            // after "--", its emulator's own option.
            argv.emplace_back("-d");
            argv.emplace_back("-g");
            argv.push_back(plan.target);
            argv.emplace_back("--");
            argv.emplace_back("--fullscreen");
            argv.emplace_back("true");
        } else if (engine->id == "ryubing") {
            argv.emplace_back("--fullscreen");
            argv.emplace_back("--hide-updates");
            argv.push_back(plan.target);
        } else if (engine->id == "azahar") {
            argv.emplace_back("-f");
            argv.push_back(plan.target);
        } else {
            argv.push_back(plan.target);
        }
    }

    plan.argv = std::move(argv);
    plan.ok   = true;
    plan.viaFlatpak = viaFlatpak;

    if (options.mangoHud) plan.environment["MANGOHUD"] = "1";
    // Dolphin's window is X11 only (through XWayland). Given the Wayland
    // platform the session hands everything, it aborts before drawing
    // anything; left unset it picks X11 itself, and set it is certain.
    if (engine->id == "dolphin") plan.environment["QT_QPA_PLATFORM"] = "xcb";
    // PCSX2 from Flathub, in a Plasma Wayland session, loads KDE's platform
    // theme inside its sandbox and crashes on start (SIGSEGV, reproduced with
    // the session's environment; gone with any of XDG_SESSION_TYPE,
    // XDG_CURRENT_DESKTOP or GTK_RC_FILES removed). Qt's plain theme only
    // changes how its own dialogs look.
    if (engine->id == "pcsx2") plan.environment["QT_QPA_PLATFORMTHEME"] = "generic";
    // shadPS4's profiler refuses a CPU without an invariant TSC and the
    // emulator dies with it (seen in the VM, whose CPU hides it; some real
    // ones do too). Nothing is lost by not checking.
    if (engine->id == "shadps4") plan.environment["TRACY_NO_INVARIANT_CHECK"] = "1";

    return plan;
}

}  // namespace omnios
