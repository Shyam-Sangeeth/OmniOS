#include "GameLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "Paths.h"

namespace omnios {
namespace {

namespace fs = std::filesystem;

std::string toLower(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

bool containsFold(std::string_view haystack, std::string_view needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

DetectionSource detectionSourceFromName(std::string_view name) {
    if (name == "magic")     return DetectionSource::Magic;
    if (name == "extension") return DetectionSource::Extension;
    if (name == "folder")    return DetectionSource::Folder;
    if (name == "manifest")  return DetectionSource::Manifest;
    return DetectionSource::None;
}

// The cache format version. Bumped when a field's meaning changes; a cache
// written by a different version is discarded and rebuilt by the scanner.
constexpr int kCacheVersion = 1;

}  // namespace

std::string makeGameId(Platform platform, std::string_view title) {
    return std::string(platformId(platform)) + "." + slugify(title);
}

std::string Game::displaySize() const {
    if (sizeBytes == 0) return "unknown";

    constexpr double kUnit = 1024.0;
    const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(sizeBytes);
    int    unit  = 0;
    while (value >= kUnit && unit < 4) {
        value /= kUnit;
        ++unit;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), value < 10.0 && unit > 0 ? "%.1f %s" : "%.0f %s",
                  value, units[unit]);
    return buffer;
}

Json Game::toJson() const {
    Json out;
    out.set("id", Json(id));
    out.set("title", Json(title));
    out.set("platform", Json(std::string(platformId(platform))));
    out.set("path", Json(path.generic_string()));
    if (!executable.empty())  out.set("executable", Json(executable));
    if (!launchId.empty())    out.set("launchId", Json(launchId));
    if (!version.empty())     out.set("version", Json(version));
    if (!developer.empty())   out.set("developer", Json(developer));
    if (!publisher.empty())   out.set("publisher", Json(publisher));
    if (!description.empty()) out.set("description", Json(description));
    out.set("size_bytes", Json(static_cast<double>(sizeBytes)));
    if (lastPlayed > 0) out.set("last_played", Json(static_cast<double>(lastPlayed)));
    if (!coverPath.empty()) out.set("cover", Json(coverPath.generic_string()));
    out.set("detection_source", Json(std::string(detectionSourceName(detectionSource))));
    if (!format.empty())         out.set("format", Json(format));
    if (!engineOverride.empty()) out.set("engine", Json(engineOverride));
    out.set("from_manifest", Json(fromManifest));
    if (!tags.empty()) {
        Json list;
        for (const std::string& tag : tags) list.push(Json(tag));
        out.set("tags", std::move(list));
    }
    return out;
}

Game Game::fromJson(const Json& value) {
    Game game;
    game.id          = value["id"].asString();
    game.title       = value["title"].asString();
    game.platform    = platformFromId(value["platform"].asString());
    game.path        = fs::path(value["path"].asString());
    game.executable  = value["executable"].asString();
    game.launchId    = value["launchId"].asString();
    game.version     = value["version"].asString();
    game.developer   = value["developer"].asString();
    game.publisher   = value["publisher"].asString();
    game.description = value["description"].asString();
    game.sizeBytes   = static_cast<std::uint64_t>(value["size_bytes"].asNumber(0.0));
    game.lastPlayed  = static_cast<std::int64_t>(value["last_played"].asNumber(0.0));
    if (value.contains("cover")) game.coverPath = fs::path(value["cover"].asString());
    game.detectionSource =
        detectionSourceFromName(value["detection_source"].asString());
    game.format         = value["format"].asString();
    game.engineOverride = value["engine"].asString();
    game.fromManifest   = value["from_manifest"].asBool();
    for (const Json& tag : value["tags"].items()) {
        if (tag.isString()) game.tags.push_back(tag.asString());
    }
    return game;
}

bool GameLibrary::add(Game game) {
    const auto it = std::find_if(games_.begin(), games_.end(),
                                 [&](const Game& existing) { return existing.id == game.id; });
    if (it != games_.end()) {
        *it = std::move(game);
        return false;
    }
    games_.push_back(std::move(game));
    return true;
}

bool GameLibrary::remove(std::string_view id) {
    const auto it = std::find_if(games_.begin(), games_.end(),
                                 [id](const Game& game) { return game.id == id; });
    if (it == games_.end()) return false;
    games_.erase(it);
    return true;
}

void GameLibrary::clear() { games_.clear(); }

const Game* GameLibrary::find(std::string_view id) const {
    const auto it = std::find_if(games_.begin(), games_.end(),
                                 [id](const Game& game) { return game.id == id; });
    return it == games_.end() ? nullptr : &*it;
}

bool GameLibrary::setCover(std::string_view id, const std::filesystem::path& cover) {
    const auto it = std::find_if(games_.begin(), games_.end(),
                                 [id](const Game& game) { return game.id == id; });
    if (it == games_.end()) return false;
    it->coverPath = cover;
    return true;
}

std::vector<const Game*> GameLibrary::byPlatform(Platform platform) const {
    std::vector<const Game*> matches;
    for (const Game& game : games_) {
        if (game.platform == platform) matches.push_back(&game);
    }
    return matches;
}

std::vector<const Game*> GameLibrary::search(std::string_view query) const {
    std::vector<const Game*> matches;
    if (query.empty()) {
        for (const Game& game : games_) matches.push_back(&game);
        return matches;
    }
    for (const Game& game : games_) {
        if (containsFold(game.title, query) || containsFold(game.developer, query) ||
            containsFold(game.publisher, query)) {
            matches.push_back(&game);
        }
    }
    return matches;
}

void GameLibrary::sortByTitle() {
    std::sort(games_.begin(), games_.end(), [](const Game& a, const Game& b) {
        const std::string left  = toLower(a.title);
        const std::string right = toLower(b.title);
        // Ties fall back to id so the order is stable across rescans.
        return left != right ? left < right : a.id < b.id;
    });
}

Json GameLibrary::toJson() const {
    Json games;
    for (const Game& game : games_) games.push(game.toJson());
    if (games_.empty()) games = Json::array({});

    Json out;
    out.set("cache_version", Json(static_cast<double>(kCacheVersion)));
    out.set("games", std::move(games));
    return out;
}

GameLibrary GameLibrary::fromJson(const Json& value) {
    GameLibrary library;
    for (const Json& entry : value["games"].items()) {
        Game game = Game::fromJson(entry);
        if (!game.id.empty()) library.add(std::move(game));
    }
    return library;
}

bool GameLibrary::save(const fs::path& file, std::string& error) const {
    error.clear();

    std::error_code ec;
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
        if (ec) {
            error = "could not create " + file.parent_path().string() + ": " + ec.message();
            return false;
        }
    }

    // Write to a sibling temp file and rename, so a crash mid-write leaves the
    // previous cache intact rather than a truncated one.
    const fs::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not open " + temp.string() + " for writing";
            return false;
        }
        out << toJson().dump(2) << '\n';
        if (!out) {
            error = "could not write " + temp.string();
            return false;
        }
    }

    fs::rename(temp, file, ec);
    if (ec) {
        // Windows refuses a rename onto an existing file; fall back to replace.
        fs::remove(file, ec);
        fs::rename(temp, file, ec);
    }
    if (ec) {
        error = "could not replace " + file.string() + ": " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool GameLibrary::load(const fs::path& file, std::string& error) {
    error.clear();
    clear();

    std::error_code ec;
    if (!fs::exists(file, ec)) return true;  // no cache yet is normal

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "could not open " + file.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    std::string parseError;
    const Json root = Json::parse(buffer.str(), parseError);
    if (!parseError.empty()) {
        error = "library cache is corrupt (" + parseError + "); rescan to rebuild it";
        return false;
    }

    const int version = static_cast<int>(root["cache_version"].asNumber(0.0));
    if (version != kCacheVersion) {
        error = "library cache version " + std::to_string(version) +
                " was written by a different build; rescan to rebuild it";
        return false;
    }

    *this = fromJson(root);
    return true;
}

}  // namespace omnios
