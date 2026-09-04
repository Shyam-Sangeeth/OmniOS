// Where OmniOS keeps things (OmniOS.md §7.1).
//
// Every path the system uses resolves through here so tests — and the installer,
// which runs before a user account exists — can point the whole library
// elsewhere with OMNIOS_GAMES_DIR / OMNIOS_DATA_DIR.
#pragma once

#include <filesystem>

#include "Platform.h"

namespace omnios {

// The user's home directory. Falls back to the current directory if the
// environment says nothing, so nothing ever resolves to an empty path.
std::filesystem::path homeDir();

// ~/Games — override with OMNIOS_GAMES_DIR.
std::filesystem::path gamesDir();

// ~/.omnios — cache, cover art, settings. Override with OMNIOS_DATA_DIR.
std::filesystem::path dataDir();

// ~/.omnios/library — per-title artwork and metadata cache.
std::filesystem::path libraryDir();

// ~/.omnios/library.json — the scanned library cache.
std::filesystem::path libraryCacheFile();

// ~/Games/<folder> for a platform.
std::filesystem::path platformDir(Platform platform);

// Creates ~/Games/* and ~/.omnios/*. Returns false and fills `error` if the
// tree could not be created.
bool ensureDirectories(std::string& error);

// Turns a title into a filesystem-safe folder name: lowercase, spaces and
// punctuation collapsed to single hyphens. "God of War" -> "god-of-war".
std::string slugify(std::string_view text);

}  // namespace omnios
