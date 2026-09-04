#include "Router.h"

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
    {"proton", "Proton", "proton", "steam", Tier::ApiLayer,
     "Windows NT API layer with DXVK/VKD3D"},
    {"wine", "Wine", "wine", "wine", Tier::ApiLayer,
     "Windows API layer without Steam's runtime"},
    {"shadps4", "shadPS4", "shadps4", "shadps4-bin", Tier::ApiLayer,
     "PS4 Orbis API layer"},
    {"ryubing", "Ryubing", "Ryujinx", "ryubing", Tier::Jit,
     "Switch ARM64 JIT (maintained Ryujinx fork)"},
    {"rpcs3", "RPCS3", "rpcs3", "rpcs3-bin", Tier::Emulator,
     "PS3 Cell/PowerPC recompiler"},
    {"pcsx2", "PCSX2", "pcsx2", "pcsx2", Tier::Emulator,
     "PS2 MIPS recompiler"},
    {"duckstation", "DuckStation", "duckstation-qt", "duckstation-bin", Tier::Emulator,
     "PS1 MIPS dynarec"},
    {"dolphin", "Dolphin", "dolphin-emu", "dolphin-emu", Tier::Emulator,
     "GameCube and Wii PowerPC JIT"},
    {"azahar", "Azahar", "azahar", "azahar-bin", Tier::Emulator,
     "3DS emulator (maintained Citra successor)"},
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

    if (game.path.empty()) {
        plan.error = "\"" + game.title + "\" has no file on disk to launch.";
        return plan;
    }

    if (!options.skipAvailabilityCheck && !engine->command.empty() &&
        !commandExists(engine->command)) {
        plan.error = std::string(engine->displayName) + " is not installed, so " +
                     game.title + " cannot start.";
        if (!engine->package.empty())
            plan.installHint = "yay -S " + std::string(engine->package);
        return plan;
    }

    std::vector<std::string> argv;
    if (options.gameMode) argv.emplace_back(kGameModeCommand);
    if (options.mangoHud) argv.emplace_back(kMangoHudCommand);

    if (engine->id == "native") {
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
    } else {
        argv.emplace_back(std::string(engine->command));
        argv.push_back(plan.target);
    }

    plan.argv = std::move(argv);
    plan.ok   = true;

    if (options.mangoHud) plan.environment["MANGOHUD"] = "1";

    return plan;
}

}  // namespace omnios
