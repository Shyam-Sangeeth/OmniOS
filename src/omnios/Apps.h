// Built-in apps — the non-game tiles.
//
// A console still has to play a video and browse a USB stick (OmniOS.md §17,
// §18). Rather than write either, OmniOS ships proven ones and presents them
// as tiles, so "Play everything" covers the media on the drive as well as the
// games.
//
// Structured exactly like the platform and engine registries: one table, and
// adding an app means adding a row.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace omnios {

struct App {
    std::string_view id;
    std::string_view name;
    // Executable, looked up on PATH the same way engines are.
    std::string_view command;
    // Space-separated arguments. The token %GAMES% is replaced with the real
    // games directory at launch, so a file manager opens somewhere useful
    // instead of a home directory the user never populated.
    std::string_view args;
    // Arch package providing it, for the "not installed" prompt.
    std::string_view package;
    // Freedesktop icon name, resolved against the installed icon theme. Empty
    // means the tile falls back to its colour wash.
    std::string_view icon;
    std::string_view description;
    // Tile badge colour, from the UI palette (OmniOS.md §12).
    std::string_view badgeColor;
    // Part of OmniOS itself. A console without a file manager, a video player
    // or a way to install things is broken, so these cannot be uninstalled.
    //
    // The menu still shows the entry, greyed with the reason. Everything else
    // it cannot do it simply omits — but this one is a rule rather than a
    // state, and silently dropping it would leave someone wondering whether
    // the tile was special or the menu was broken.
    bool system;
};

const std::vector<App>& allApps();

// True when removing this package would break OmniOS itself, because a system
// app depends on it. Asked of the package rather than the tile: the browser and
// the YouTube tile are two rows sharing one chromium package, so "is this tile
// a system app?" is the wrong question — removing chromium from either would
// break both.
bool packageIsProtected(std::string_view package);

// Nullptr when unknown.
const App* findApp(std::string_view id);

// True when the app's command resolves on PATH.
bool appAvailable(const App& app);

// Full argv for launching, with %GAMES% expanded. argv[0] is the command.
std::vector<std::string> appArgv(const App& app);

// Absolute path to the app's icon, or empty when the theme has none. Looked up
// at call time rather than cached: the icon appears when the package is
// installed, which on a live image can happen after the launcher has started.
std::string appIconPath(const App& app);

// Same lookup for a caller that has only an icon name, such as a desktop entry.
std::string iconPathFor(std::string_view icon);

}  // namespace omnios
