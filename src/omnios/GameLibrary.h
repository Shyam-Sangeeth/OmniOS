// Game library — Phase 7 (OmniOS.md §9).
//
// The in-memory model behind every tile in the launcher, plus its on-disk cache
// at ~/.omnios/library.json. The cache exists so the launcher can paint a full
// grid on boot without waiting for a filesystem walk; it is always
// reconstructible by rescanning, so a corrupt or stale cache is never fatal.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Detector.h"
#include "Json.h"
#include "Manifest.h"
#include "Platform.h"

namespace omnios {

struct Game {
    // Stable identity. Comes from the manifest when there is one, otherwise
    // derived as "<platform>.<slug>" so a rescan produces the same id and the
    // tile keeps its artwork and play history.
    std::string   id;
    std::string   title;
    Platform      platform = Platform::Unknown;

    // The file or directory to hand to the engine.
    std::filesystem::path path;
    // Relative executable inside `path` when it is a directory; empty for a
    // single-file title such as a ROM or a PKG.
    std::string   executable;

    std::string   version;
    std::string   developer;
    std::string   publisher;
    std::string   description;
    std::vector<std::string> tags;

    std::uint64_t sizeBytes = 0;
    // Cover art in ~/.omnios/library/<id>/; empty until bundled art is
    // extracted or SteamGridDB fills it in (Phase 7.4).
    std::filesystem::path coverPath;

    // How the platform was determined, kept so the launcher can explain a
    // failed launch instead of just reporting it.
    DetectionSource detectionSource = DetectionSource::None;
    std::string     format;
    // Engine id forced by the package or the user; empty means the router picks.
    std::string     engineOverride;

    bool fromManifest = false;

    Json toJson() const;
    static Game fromJson(const Json& value);

    std::string displaySize() const;
};

// Builds the library id for a title with no manifest of its own.
std::string makeGameId(Platform platform, std::string_view title);

class GameLibrary {
public:
    // Inserts, or replaces the existing entry with the same id. Returns true
    // when the entry was new.
    bool add(Game game);

    bool remove(std::string_view id);
    void clear();

    const Game* find(std::string_view id) const;

    const std::vector<Game>& games() const { return games_; }
    std::size_t              size() const { return games_.size(); }
    bool                     empty() const { return games_.empty(); }

    std::vector<const Game*> byPlatform(Platform platform) const;
    // Case-insensitive substring match over title, developer and publisher.
    std::vector<const Game*> search(std::string_view query) const;

    // Sorts by title, case-insensitively. The launcher's "All Games" order.
    void sortByTitle();

    Json toJson() const;
    static GameLibrary fromJson(const Json& value);

    // Cache round trip. Both return false and fill `error` on failure; a
    // missing cache file loads as an empty library and is not an error.
    bool save(const std::filesystem::path& file, std::string& error) const;
    bool load(const std::filesystem::path& file, std::string& error);

private:
    std::vector<Game> games_;
};

}  // namespace omnios
