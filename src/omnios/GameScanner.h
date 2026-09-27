// Game scanner — Phase 7 (OmniOS.md §9).
//
// Walks ~/Games/, identifies every title it finds and produces the library.
// The scan is the authority: the cache is only ever a faster copy of what this
// produces, so a scan must be safe to run at any time and must never lose
// user-visible state that cannot be rederived.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GameLibrary.h"
#include "Platform.h"

namespace omnios {

struct ScanReport {
    int added     = 0;
    int updated   = 0;
    int removed   = 0;
    int skipped   = 0;
    // Files the scanner could not identify, by path. Surfaced in the launcher
    // so a user can see why a game they copied over has no tile.
    std::vector<std::string> unidentified;
    std::vector<std::string> warnings;

    int total() const { return added + updated; }
};

class GameScanner {
public:
    // Scans the default ~/Games/ tree.
    GameScanner();
    explicit GameScanner(std::filesystem::path gamesRoot);

    // Rebuilds `library` from disk. Entries already present keep their cached
    // artwork and metadata; entries whose files are gone are removed.
    ScanReport scan(GameLibrary& library) const;

    // Identifies one path as a title. Returns false when the path is not a game
    // (artwork, a readme, an unreadable file), with the reason in `reason`.
    bool identify(const std::filesystem::path& path, Platform folderHint,
                  Game& game, std::string& reason) const;

    const std::filesystem::path& root() const { return root_; }

    // Every Steam library the scan reads: ~/Games/steam, then Steam's own.
    // Also what steamLibraryStamp is given to notice a change.
    std::vector<std::filesystem::path> steamLibraries() const;

private:
    std::filesystem::path root_;
};

// Total size of a file, or of a directory tree. Returns 0 when unreadable.
std::uint64_t pathSize(const std::filesystem::path& path);

// "god-of-war_v1.2 [PS4]" -> "God of War". Best-effort cleanup of a dump's
// filename into something worth putting on a tile.
std::string titleFromFilename(std::string_view filename);

}  // namespace omnios
