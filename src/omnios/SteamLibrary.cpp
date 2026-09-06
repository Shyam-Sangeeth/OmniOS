#include "SteamLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

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

}  // namespace

bool readSteamManifest(const fs::path& file, Game& game) {
    std::ifstream in(file);
    if (!in) return false;

    std::string appId, name, installDir, sizeOnDisk;
    std::string line, key, value;
    while (std::getline(in, line)) {
        if (!keyValue(line, key, value)) continue;
        const std::string lower = lowered(key);
        if (lower == "appid") appId = value;
        else if (lower == "name") name = value;
        else if (lower == "installdir") installDir = value;
        else if (lower == "sizeondisk") sizeOnDisk = value;
    }

    // The app id is the only part a launch cannot do without: everything else
    // has a reasonable fallback.
    if (appId.empty()) return false;

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
    std::vector<fs::path> roots;
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return roots;

    // Both spellings: Steam moved from ~/.steam to ~/.local/share/Steam, and
    // installs made before either path is still on plenty of machines.
    roots.emplace_back(fs::path(home) / ".local/share/Steam/steamapps");
    roots.emplace_back(fs::path(home) / ".steam/steam/steamapps");
    return roots;
}

}  // namespace omnios
