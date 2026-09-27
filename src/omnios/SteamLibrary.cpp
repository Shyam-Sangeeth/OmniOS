#include "SteamLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace omnios {
namespace {

namespace fs = std::filesystem;

// One line of KeyValues: whitespace, "key", whitespace, "value". Returns false
// for a brace, a comment or a nested block header, all of which are ignored.
bool keyValue(const std::string& line, std::string& key, std::string& value) {
    std::size_t i = 0;
    const auto skipSpace = [&] {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    };
    const auto quoted = [&](std::string& out) {
        if (i >= line.size() || line[i] != '"') return false;
        ++i;
        out.clear();
        while (i < line.size() && line[i] != '"') {
            // Valve escapes backslashes and quotes; nothing else matters here.
            if (line[i] == '\\' && i + 1 < line.size()) ++i;
            out += line[i++];
        }
        if (i >= line.size()) return false;
        ++i;
        return true;
    };

    skipSpace();
    if (!quoted(key)) return false;
    skipSpace();
    return quoted(value);
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Steam's app ids are numbers. Anything else did not come from Steam, and is
// about to become part of a path and a URL.
bool isAppId(const std::string& text) {
    return !text.empty() && text.size() <= 12 &&
           std::all_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// The Steam client's own directory, in both of its homes (see
// defaultSteamLibraries for why both).
std::vector<fs::path> steamClients() {
    std::vector<fs::path> clients;
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return clients;
    clients.emplace_back(fs::path(home) / ".local/share/Steam");
    clients.emplace_back(fs::path(home) / ".steam/steam");
    return clients;
}

// StateFlags bit 2 (value 4) is "fully installed". A first download in
// progress lacks it; an update in progress keeps it, since the game still
// runs until Steam says otherwise.
constexpr unsigned long kFullyInstalled = 4;

}  // namespace

bool isSteamTool(const std::string& appId, const std::string& name) {
    // By id where the id is certain; by name for the rest, since Valve adds a
    // Proton and a runtime every year or so under a new id.
    static const char* const kToolIds[] = {
        "228980",   // Steamworks Common Redistributables
        "1070560",  // Steam Linux Runtime 1.0 (scout)
        "1391110",  // Steam Linux Runtime 2.0 (soldier)
        "1628350",  // Steam Linux Runtime 3.0 (sniper)
        "1493710",  // Proton Experimental
        "2180100",  // Proton Hotfix
        "1161040",  // Proton BattlEye Runtime
        "1826330",  // Proton EasyAntiCheat Runtime
    };
    for (const char* id : kToolIds)
        if (appId == id) return true;

    static const char* const kToolPrefixes[] = {
        "Proton ",
        "Steam Linux Runtime",
        "Steamworks Common Redistributables",
    };
    for (const char* prefix : kToolPrefixes)
        if (name.rfind(prefix, 0) == 0) return true;
    return false;
}

bool readSteamManifest(const fs::path& file, Game& game) {
    std::ifstream in(file);
    if (!in) return false;

    std::string appId, name, installDir, sizeOnDisk, stateFlags, lastPlayed;
    std::string line, key, value;
    while (std::getline(in, line)) {
        if (!keyValue(line, key, value)) continue;
        const std::string lower = lowered(key);
        if (lower == "appid") appId = value;
        else if (lower == "name") name = value;
        else if (lower == "installdir") installDir = value;
        else if (lower == "sizeondisk") sizeOnDisk = value;
        else if (lower == "stateflags") stateFlags = value;
        else if (lower == "lastplayed") lastPlayed = value;
    }

    // The app id is the only part a launch cannot do without: everything else
    // has a reasonable fallback.
    if (!isAppId(appId)) return false;
    if (isSteamTool(appId, name)) return false;

    // Still downloading for the first time: a tile now would start Steam's
    // download page, not the game. It appears once the download finishes. A
    // manifest without the field is taken as installed, as older ones were.
    if (!stateFlags.empty()) {
        try {
            if ((std::stoul(stateFlags) & kFullyInstalled) == 0) return false;
        } catch (const std::exception&) {
            // Unparseable: better a tile that Steam then explains than no tile.
        }
    }

    game = Game{};
    game.id       = "steam." + appId;
    game.title    = name.empty() ? ("Steam app " + appId) : name;
    game.platform = Platform::Steam;
    game.launchId = appId;
    game.format   = "steam";
    game.detectionSource = DetectionSource::Manifest;

    // The install directory is what a user would call "the game", and having it
    // means the tile can report a real size and find bundled artwork later.
    if (!installDir.empty()) {
        const fs::path common = file.parent_path() / "common" / installDir;
        std::error_code ec;
        if (fs::exists(common, ec)) game.path = common;
    }
    if (game.path.empty()) game.path = file;

    // Steam writes 0 until the game has been started once.
    if (!lastPlayed.empty()) {
        try {
            game.lastPlayed = std::stoll(lastPlayed);
        } catch (const std::exception&) {
            // Unknown, which is what 0 already says.
        }
    }

    if (!sizeOnDisk.empty()) {
        try {
            game.sizeBytes = static_cast<std::uint64_t>(std::stoull(sizeOnDisk));
        } catch (const std::exception&) {
            // A manifest with a size we cannot parse is still a game.
        }
    }
    return true;
}

std::vector<Game> readSteamLibrary(const fs::path& steamapps) {
    std::vector<Game> games;
    std::error_code ec;
    if (!fs::is_directory(steamapps, ec)) return games;

    for (const fs::directory_entry& entry :
         fs::directory_iterator(steamapps, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const std::string name = entry.path().filename().string();
        if (name.rfind("appmanifest_", 0) != 0) continue;
        if (entry.path().extension() != ".acf") continue;

        Game game;
        if (readSteamManifest(entry.path(), game)) games.push_back(std::move(game));
    }
    return games;
}

std::vector<fs::path> defaultSteamLibraries() {
    // Both spellings: Steam moved from ~/.steam to ~/.local/share/Steam, and
    // installs made before either path is still on plenty of machines.
    std::vector<fs::path> roots;
    for (const fs::path& client : steamClients()) roots.push_back(client / "steamapps");

    // Then every other library the client knows. The file has lived in both
    // places over the years. Reached through two clients, or through the
    // ~/Games/Steam symlink, one library can be listed twice; the scanner
    // keeps one tile per game, so the duplicates only cost a directory read.
    const std::size_t clientLibraries = roots.size();
    for (std::size_t i = 0; i < clientLibraries; ++i) {
        for (const fs::path& vdf : {roots[i] / "libraryfolders.vdf",
                                    roots[i].parent_path() / "config" / "libraryfolders.vdf"}) {
            for (fs::path& library : steamLibraryFolders(vdf)) {
                if (std::find(roots.begin(), roots.end(), library) == roots.end())
                    roots.push_back(std::move(library));
            }
        }
    }
    return roots;
}

std::vector<fs::path> steamLibraryFolders(const fs::path& vdf) {
    // "libraryfolders" { "0" { "path" "/home/me/.local/share/Steam" ... } ... }
    // Only the "path" lines matter; the nesting says nothing needed here.
    std::vector<fs::path> libraries;
    std::ifstream in(vdf);
    if (!in) return libraries;

    std::string line, key, value;
    while (std::getline(in, line)) {
        if (!keyValue(line, key, value)) continue;
        if (lowered(key) != "path" || value.empty()) continue;
        libraries.push_back(fs::path(value) / "steamapps");
    }
    return libraries;
}

fs::path steamCover(const fs::path& client, const std::string& appId) {
    if (!isAppId(appId)) return {};
    const fs::path cache = client / "appcache" / "librarycache";
    std::error_code ec;

    // Three layouts, newest first: a folder per app, sometimes with the art
    // one level down in a folder named by a hash; and, before 2024, flat files
    // prefixed with the app id.
    const auto find = [&](const std::string& image) -> fs::path {
        const fs::path appDir = cache / appId;
        if (fs::is_regular_file(appDir / image, ec)) return appDir / image;
        if (fs::is_directory(appDir, ec)) {
            for (const fs::directory_entry& sub : fs::directory_iterator(appDir, ec)) {
                if (sub.is_directory(ec) && fs::is_regular_file(sub.path() / image, ec))
                    return sub.path() / image;
            }
        }
        const fs::path flat = cache / (appId + "_" + image);
        if (fs::is_regular_file(flat, ec)) return flat;
        return {};
    };

    // The tall capsule is the shape of a tile. The wide header is cropped to
    // fit, which still beats a plain colour.
    if (fs::path cover = find("library_600x900.jpg"); !cover.empty()) return cover;
    return find("header.jpg");
}

fs::path steamCover(const std::string& appId) {
    for (const fs::path& client : steamClients()) {
        if (fs::path cover = steamCover(client, appId); !cover.empty()) return cover;
    }
    return {};
}

std::vector<SteamGameProcess> runningSteamGames(const fs::path& proc) {
    std::vector<SteamGameProcess> games;
    std::error_code ec;
    if (!fs::is_directory(proc, ec)) return games;

    for (const fs::directory_entry& entry :
         fs::directory_iterator(proc, fs::directory_options::skip_permission_denied, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;

        // One argument per NUL. A process can vanish between the listing and
        // the read; it then reads as empty and is skipped.
        std::ifstream in(entry.path() / "cmdline", std::ios::binary);
        if (!in) continue;
        std::vector<std::string> args;
        std::string arg;
        while (std::getline(in, arg, '\0')) args.push_back(arg);

        // "SteamLaunch" then "AppId=<n>", both before the "--" that starts
        // the game's own command, which could contain anything.
        bool launch = false;
        for (const std::string& a : args) {
            if (a == "--") break;
            if (a == "SteamLaunch") launch = true;
            else if (launch && a.rfind("AppId=", 0) == 0 && isAppId(a.substr(6))) {
                SteamGameProcess game;
                try {
                    game.pid = std::stoi(name);
                } catch (const std::exception&) {
                    break;
                }
                game.appId = a.substr(6);
                games.push_back(std::move(game));
                break;
            }
        }
    }
    return games;
}

std::vector<int> descendantsOf(int pid, const fs::path& proc) {
    // Parent of every process, from /proc/<pid>/stat: "pid (comm) state ppid".
    // comm can hold spaces and parentheses, so the fields are read after the
    // last ')'.
    std::vector<std::pair<int, int>> parents;  // {pid, ppid}
    std::error_code ec;
    if (!fs::is_directory(proc, ec)) return {};
    for (const fs::directory_entry& entry :
         fs::directory_iterator(proc, fs::directory_options::skip_permission_denied, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;
        std::ifstream in(entry.path() / "stat");
        std::string stat;
        if (!in || !std::getline(in, stat)) continue;
        const std::size_t close = stat.rfind(')');
        if (close == std::string::npos) continue;
        std::istringstream rest(stat.substr(close + 1));
        std::string state;
        int ppid = 0;
        if (!(rest >> state >> ppid)) continue;
        try {
            parents.emplace_back(std::stoi(name), ppid);
        } catch (const std::exception&) {
        }
    }

    std::vector<int> found;
    std::vector<int> frontier{pid};
    while (!frontier.empty()) {
        const int parent = frontier.back();
        frontier.pop_back();
        for (const auto& [child, ppid] : parents) {
            if (ppid != parent) continue;
            if (std::find(found.begin(), found.end(), child) != found.end()) continue;
            found.push_back(child);
            frontier.push_back(child);
        }
    }
    return found;
}

std::uint64_t processStartTime(int pid, const fs::path& proc) {
    std::ifstream in(proc / std::to_string(pid) / "stat");
    std::string stat;
    if (!in || !std::getline(in, stat)) return 0;
    const std::size_t close = stat.rfind(')');
    if (close == std::string::npos) return 0;
    // After "(comm)": state is field 3, so start time (22) is the 20th here.
    std::istringstream rest(stat.substr(close + 1));
    std::string field;
    for (int i = 3; i <= 22; ++i) {
        if (!(rest >> field)) return 0;
    }
    try {
        return std::stoull(field);
    } catch (const std::exception&) {
        return 0;
    }
}

std::string steamLibraryStamp(const std::vector<fs::path>& libraries) {
    // What a tile shows, not when a file was touched: Steam rewrites a
    // manifest every few seconds while it downloads, and a rescan resets the
    // grid, so the Games tab must not follow every write.
    std::vector<std::string> entries;
    for (const fs::path& steamapps : libraries) {
        for (const Game& game : readSteamLibrary(steamapps))
            entries.push_back(game.id + '\t' + game.title + '\t' + steamCover(game.launchId).string());
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    std::string stamp;
    for (const std::string& entry : entries) stamp += entry + '\n';
    return stamp;
}

}  // namespace omnios
