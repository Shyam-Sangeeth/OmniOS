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

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GameLibrary.h"

namespace omnios {

// Every game with an appmanifest in `steamapps`. Directories that are not a
// Steam library simply produce nothing.
std::vector<Game> readSteamLibrary(const std::filesystem::path& steamapps);

// One manifest. False when the file is unreadable or has no app id, which is
// what a half-written manifest from an interrupted install looks like; and
// false for anything that is not a game to play yet — see below.
bool readSteamManifest(const std::filesystem::path& file, Game& game);

// Steam installs its own tools beside the games, each with a manifest of its
// own: Proton, the Steam Linux Runtimes, the Windows redistributables. None of
// them is something to start from a tile.
bool isSteamTool(const std::string& appId, const std::string& name);

// Where Steam keeps its games by default, so a library that was installed
// before OmniOS pointed Steam at ~/Games is still found — and every further
// library those list in libraryfolders.vdf, such as one on a second drive.
std::vector<std::filesystem::path> defaultSteamLibraries();

// The steamapps folder of every library a libraryfolders.vdf lists. Missing
// or unreadable files list nothing.
std::vector<std::filesystem::path> steamLibraryFolders(const std::filesystem::path& vdf);

// Steam's own cover for an app, from the artwork the client caches for its
// library view under <client>/appcache/librarycache. The tall capsule when
// there is one, the wide header otherwise; empty when Steam has none yet.
std::filesystem::path steamCover(const std::filesystem::path& client, const std::string& appId);

// The same, looked up in the Steam clients in their default places.
std::filesystem::path steamCover(const std::string& appId);

// A game Steam is running now. Steam starts every game under its reaper,
// whose command line names it — "reaper SteamLaunch AppId=570 -- <game>" —
// and which ends when the game and everything it started has ended. So the
// reaper is both how to tell the game is running and how to stop it.
struct SteamGameProcess {
    int         pid = 0;   // the reaper's
    std::string appId;
};

// Every game Steam is running, from the command lines under `proc`. Empty
// where there is no /proc, as on Windows.
std::vector<SteamGameProcess> runningSteamGames(
    const std::filesystem::path& proc = std::filesystem::path("/proc"));

// Whether the Steam client is running: a process named "steam" under `proc`.
// While it is, Steam answers the controller's Guide button as well, opening
// Big Picture over whatever is in front.
bool steamClientRunning(const std::filesystem::path& proc = std::filesystem::path("/proc"));

// Every process below `pid` — children, their children, and on — from the
// parent ids under `proc`. What has to be stopped to stop a game: the reaper
// cleans up after a game that exits, and nothing says it does the same when it
// is the one told to stop.
std::vector<int> descendantsOf(int pid,
                               const std::filesystem::path& proc = std::filesystem::path("/proc"));

// When a process started, in clock ticks since boot (field 22 of its stat); 0
// when there is no such process. A process id can be reused once its owner
// has gone; the pair (pid, start time) cannot, so it is how a process noted
// earlier is recognised later — to finish off a game that ignored being asked
// to stop, and nothing that has taken its number since.
std::uint64_t processStartTime(int pid,
                               const std::filesystem::path& proc = std::filesystem::path("/proc"));

// The Steam games in `libraries` as the Games tab would show them: ids,
// titles, covers. It changes when a game finishes installing, is uninstalled,
// is renamed or gets its art — not while a download merely progresses. Cheap
// enough to poll, so the Games tab can notice a finished install by itself.
std::string steamLibraryStamp(const std::vector<std::filesystem::path>& libraries);

}  // namespace omnios
