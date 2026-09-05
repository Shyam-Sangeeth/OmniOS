// omnictl — the command-line face of the OmniOS core.
//
// The QML launcher (Phase 10) will call the same library through the same
// entry points, so every phase is usable and debuggable before the shell
// exists. Nothing here launches a game without being asked: `launch` prints the
// plan unless --run is given.
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "omnios/Apps.h"
#include "omnios/Detector.h"
#include "omnios/GameLibrary.h"
#include "omnios/GameScanner.h"
#include "omnios/Manifest.h"
#include "omnios/Paths.h"
#include "omnios/Router.h"

namespace fs = std::filesystem;
using namespace omnios;

namespace {

constexpr const char* kUsage = R"(omnictl — OmniOS control

usage:
  omnictl init                    create ~/Games and ~/.omnios
  omnictl scan [--quiet]          rescan ~/Games and rewrite the library cache
  omnictl list [--platform <id>] [--search <text>]
  omnictl info <game-id>
  omnictl detect <path>           identify one file
  omnictl launch <game-id> [--run] [--engine <id>] [--mangohud] [--no-gamemode]
  omnictl platforms               list known platforms
  omnictl engines                 list execution layers and whether they are installed
  omnictl apps                    list built-in apps and whether they are installed
  omnictl verify <manifest.json>  validate an .opkg manifest

paths come from OMNIOS_GAMES_DIR and OMNIOS_DATA_DIR when those are set.
)";

struct Args {
    std::string              command;
    std::vector<std::string> positional;

    bool has(std::string_view flag) const {
        for (const std::string& value : flags) {
            if (value == flag) return true;
        }
        return false;
    }

    // Value of "--name <value>"; empty when absent.
    std::string value(std::string_view name) const {
        for (std::size_t i = 0; i + 1 < flags.size(); ++i) {
            if (flags[i] == name) return flags[i + 1];
        }
        return {};
    }

    std::vector<std::string> flags;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string token = argv[i];
        if (args.command.empty() && !token.empty() && token[0] != '-') {
            args.command = token;
        } else if (token.rfind("--", 0) == 0) {
            args.flags.push_back(token);
            // A flag's value is captured too, so value() can find it.
            if (i + 1 < argc && argv[i + 1][0] != '-') args.flags.emplace_back(argv[i + 1]);
        } else if (!args.flags.empty() && args.flags.back() == token) {
            // already captured as a flag value
        } else {
            args.positional.push_back(token);
        }
    }
    return args;
}

// Loads the cache, falling back to a fresh scan when it is missing or stale so
// no command ever fails just because the cache is out of date.
GameLibrary loadLibrary(bool& rescanned) {
    GameLibrary library;
    std::string error;
    rescanned = false;

    if (!library.load(libraryCacheFile(), error)) {
        std::cerr << "note: " << error << "\n";
        library.clear();
    }
    if (library.empty()) {
        GameScanner().scan(library);
        rescanned = true;
    }
    return library;
}

int cmdInit() {
    std::string error;
    if (!ensureDirectories(error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    std::cout << "games:   " << gamesDir().generic_string() << "\n"
              << "data:    " << dataDir().generic_string() << "\n"
              << "library: " << libraryCacheFile().generic_string() << "\n";
    return 0;
}

int cmdScan(const Args& args) {
    const bool quiet = args.has("--quiet");

    GameLibrary library;
    std::string error;
    if (!library.load(libraryCacheFile(), error)) {
        std::cerr << "note: " << error << "\n";
        library.clear();
    }

    GameScanner scanner;
    const ScanReport report = scanner.scan(library);

    for (const std::string& warning : report.warnings) std::cerr << "warning: " << warning << "\n";

    if (!library.save(libraryCacheFile(), error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    if (!quiet) {
        std::cout << "scanned " << scanner.root().generic_string() << "\n"
                  << "  " << report.added << " added, " << report.updated << " updated, "
                  << report.removed << " removed, " << report.skipped << " skipped\n";
        if (!report.unidentified.empty()) {
            std::cout << "  could not identify:\n";
            for (const std::string& item : report.unidentified)
                std::cout << "    " << item << "\n";
        }
    }
    return 0;
}

int cmdList(const Args& args) {
    bool              rescanned = false;
    const GameLibrary library   = loadLibrary(rescanned);

    const std::string platformFilter = args.value("--platform");
    const std::string search         = args.value("--search");

    std::vector<const Game*> games = library.search(search);

    if (!platformFilter.empty()) {
        const Platform wanted = platformFromId(platformFilter);
        if (wanted == Platform::Unknown) {
            std::cerr << "error: unknown platform \"" << platformFilter << "\"\n";
            return 1;
        }
        std::vector<const Game*> filtered;
        for (const Game* game : games) {
            if (game->platform == wanted) filtered.push_back(game);
        }
        games = std::move(filtered);
    }

    if (games.empty()) {
        std::cout << "no games found in " << gamesDir().generic_string() << "\n";
        return 0;
    }

    for (const Game* game : games) {
        std::cout << "  [" << platformDisplayName(game->platform) << "] " << game->title << "\n"
                  << "      id " << game->id << "  " << game->displaySize() << "\n";
    }
    std::cout << "\n" << games.size() << " game" << (games.size() == 1 ? "" : "s") << "\n";
    return 0;
}

int cmdInfo(const Args& args) {
    if (args.positional.empty()) {
        std::cerr << "error: info needs a game id (see: omnictl list)\n";
        return 1;
    }
    bool              rescanned = false;
    const GameLibrary library   = loadLibrary(rescanned);
    const Game*       game      = library.find(args.positional.front());
    if (game == nullptr) {
        std::cerr << "error: no game with id \"" << args.positional.front() << "\"\n";
        return 1;
    }

    std::cout << game->title << "\n"
              << "  id         " << game->id << "\n"
              << "  platform   " << platformDisplayName(game->platform) << "\n"
              << "  path       " << game->path.generic_string() << "\n";
    if (!game->executable.empty())  std::cout << "  executable " << game->executable << "\n";
    if (!game->developer.empty())   std::cout << "  developer  " << game->developer << "\n";
    if (!game->publisher.empty())   std::cout << "  publisher  " << game->publisher << "\n";
    std::cout << "  size       " << game->displaySize() << "\n"
              << "  format     " << (game->format.empty() ? "unknown" : game->format) << "\n"
              << "  detected   " << detectionSourceName(game->detectionSource) << "\n";
    if (!game->coverPath.empty()) std::cout << "  cover      " << game->coverPath.generic_string() << "\n";
    if (!game->description.empty()) std::cout << "\n  " << game->description << "\n";

    LaunchOptions options;
    options.skipAvailabilityCheck = true;
    const LaunchPlan plan = planLaunch(*game, options);
    if (plan.ok) {
        std::cout << "\n  engine     " << plan.engineDisplayName << " (" << tierName(plan.tier)
                  << ")\n  command    " << plan.commandLine() << "\n";
    } else {
        std::cout << "\n  " << plan.error << "\n";
    }
    return 0;
}

int cmdDetect(const Args& args) {
    if (args.positional.empty()) {
        std::cerr << "error: detect needs a path\n";
        return 1;
    }
    const fs::path path = args.positional.front();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::cerr << "error: " << path.generic_string() << " does not exist\n";
        return 1;
    }

    const Detection detection = detectFile(path);
    std::cout << path.filename().generic_string() << "\n"
              << "  platform   " << platformDisplayName(detection.platform) << "\n"
              << "  format     " << detection.format << "\n"
              << "  source     " << detectionSourceName(detection.source) << "\n"
              << "  confidence " << detection.confidence << "\n"
              << "  evidence   " << detection.evidence << "\n";
    return detection.recognised() ? 0 : 2;
}

int cmdLaunch(const Args& args) {
    if (args.positional.empty()) {
        std::cerr << "error: launch needs a game id (see: omnictl list)\n";
        return 1;
    }

    bool              rescanned = false;
    const GameLibrary library   = loadLibrary(rescanned);
    const Game*       game      = library.find(args.positional.front());
    if (game == nullptr) {
        std::cerr << "error: no game with id \"" << args.positional.front() << "\"\n";
        return 1;
    }

    LaunchOptions options;
    options.gameMode = !args.has("--no-gamemode");
    options.mangoHud = args.has("--mangohud");
    options.engine   = args.value("--engine");
    // Without --run this is a dry run, so report the plan even for engines that
    // are not installed on this machine.
    options.skipAvailabilityCheck = !args.has("--run");

    const LaunchPlan plan = planLaunch(*game, options);
    if (!plan.ok) {
        std::cerr << "error: " << plan.error << "\n";
        if (!plan.installHint.empty())
            std::cerr << "       install it with: " << plan.installHint << "\n";
        return 1;
    }

    std::cout << plan.commandLine() << "\n";
    if (!args.has("--run")) {
        std::cout << "(dry run — pass --run to actually start it)\n";
        return 0;
    }

    for (const auto& variable : plan.environment) {
#ifdef _WIN32
        _putenv_s(variable.first.c_str(), variable.second.c_str());
#else
        setenv(variable.first.c_str(), variable.second.c_str(), 1);
#endif
    }
    return std::system(plan.commandLine().c_str());
}

int cmdPlatforms() {
    for (const PlatformInfo& info : allPlatforms()) {
        const Engine* engine = defaultEngine(info.platform);
        std::cout << "  " << info.id;
        for (std::size_t i = info.id.size(); i < 10; ++i) std::cout << ' ';
        std::cout << info.displayName;
        for (std::size_t i = info.displayName.size(); i < 12; ++i) std::cout << ' ';
        std::cout << "~/Games/" << info.folder;
        for (std::size_t i = info.folder.size(); i < 10; ++i) std::cout << ' ';
        std::cout << (engine != nullptr ? engine->displayName : "none") << "\n";
    }
    return 0;
}

int cmdEngines() {
    for (const Engine& engine : allEngines()) {
        const bool installed = engine.command.empty() || commandExists(engine.command);
        std::cout << "  " << (installed ? "[installed]    " : "[not installed]") << " "
                  << engine.displayName << " (" << tierName(engine.tier) << ")\n"
                  << "      " << engine.notes << "\n";
        if (!installed && !engine.package.empty())
            std::cout << "      install: yay -S " << engine.package << "\n";
    }
    return 0;
}

int cmdApps() {
    for (const App& app : allApps()) {
        const bool installed = appAvailable(app);
        std::cout << "  " << (installed ? "[installed]    " : "[not installed]") << " "
                  << app.name << "\n      " << app.description << "\n";
        if (!installed)
            std::cout << "      install: pacman -S " << app.package << "\n";
        const std::vector<std::string> argv = appArgv(app);
        std::cout << "      command:";
        for (const std::string& part : argv) std::cout << ' ' << part;
        std::cout << "\n";
    }
    return 0;
}

int cmdVerify(const Args& args) {
    if (args.positional.empty()) {
        std::cerr << "error: verify needs a path to a manifest.json\n";
        return 1;
    }
    const fs::path path = args.positional.front();
    std::ifstream  in(path, std::ios::binary);
    if (!in) {
        std::cerr << "error: could not read " << path.generic_string() << "\n";
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    const ManifestResult result = parseManifest(text);
    for (const std::string& warning : result.warnings) std::cout << "warning: " << warning << "\n";
    for (const std::string& error : result.errors) std::cout << "error:   " << error << "\n";

    if (!result.ok()) {
        std::cout << "\n" << path.filename().generic_string() << " is not installable\n";
        return 1;
    }

    const Manifest& manifest = result.manifest;
    std::cout << "\n" << manifest.title << " (" << platformDisplayName(manifest.platform) << ")"
              << "\n  installs to " << platformDir(manifest.platform).generic_string() << "/"
              << slugify(manifest.title) << "\n  manifest is valid\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);

    if (args.command.empty() || args.command == "help" || args.has("--help")) {
        std::cout << kUsage;
        return args.command.empty() ? 1 : 0;
    }

    if (args.command == "init")      return cmdInit();
    if (args.command == "scan")      return cmdScan(args);
    if (args.command == "list")      return cmdList(args);
    if (args.command == "info")      return cmdInfo(args);
    if (args.command == "detect")    return cmdDetect(args);
    if (args.command == "launch")    return cmdLaunch(args);
    if (args.command == "platforms") return cmdPlatforms();
    if (args.command == "engines")   return cmdEngines();
    if (args.command == "apps")      return cmdApps();
    if (args.command == "verify")    return cmdVerify(args);

    std::cerr << "error: unknown command \"" << args.command << "\"\n\n" << kUsage;
    return 1;
}
