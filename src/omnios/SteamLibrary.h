// Reading a Steam library.
//
// Steam does not put games where the rest of ~/Games is laid out and it does
// not name their folders after the game. What it does do is write an
// appmanifest_<appid>.acf beside every install, holding the app id, the real
// title and the size — so the library is read from Steam's own records rather
// than guessed from directory names, which is both more accurate and the only
// way to get the app id a launch needs.
//
// The file is Valve's KeyValues format. Only the flat "key" "value" pairs
// inside AppState matter here, so this reads those rather than pulling in a
// parser for a format nothing else in OmniOS uses.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GameLibrary.h"

namespace omnios {

// Every game with an appmanifest in `steamapps`. Directories that are not a
// Steam library simply produce nothing.
std::vector<Game> readSteamLibrary(const std::filesystem::path& steamapps);

// One manifest. False when the file is unreadable or has no app id, which is
// what a half-written manifest from an interrupted install looks like.
bool readSteamManifest(const std::filesystem::path& file, Game& game);

// Where Steam keeps its games by default, so a library that was installed
// before OmniOS pointed Steam at ~/Games is still found.
std::vector<std::filesystem::path> defaultSteamLibraries();

}  // namespace omnios
