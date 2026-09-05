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
    // Part of OmniOS itself. A console without a file manager or a video
    // player is broken, so these cannot be uninstalled from the UI — the
    // option is shown disabled rather than hidden, so it is clear the tile
    // has a menu like any other and why this entry is unavailable.
    bool system;
};

// An app that can be installed from the official repositories. Kept separate
// from App: these are optional extras, and until one is installed it has no
// command to run and no icon on disk to show.
struct StoreApp {
    std::string_view id;
    std::string_view name;
    std::string_view package;
    // Binary the package provides, used to tell installed from not.
    std::string_view command;
    std::string_view icon;
    std::string_view description;
    std::string_view category;
    std::string_view badgeColor;
};

const std::vector<StoreApp>& appCatalog();
const StoreApp* findStoreApp(std::string_view id);

// True when the package's binary is on PATH. Cheaper than querying pacman and
// answers the question the UI actually asks: can this be launched?
bool storeAppInstalled(const StoreApp& app);

// True when removing this package would break OmniOS itself, because a system
// app depends on it. Enforced here rather than in the UI: the browser and the
// YouTube tile are two rows sharing one package, so "is this tile a system
// app?" is the wrong question — "does any system app need this package?" is
// the right one, and only this table can answer it.
bool packageIsProtected(std::string_view package);

const std::vector<App>& allApps();

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
std::string storeIconPath(const StoreApp& app);

// Shared lookup behind both, exposed so a caller with only an icon name (a
// pacman query result, say) can use the same search order.
std::string iconPathFor(std::string_view icon);

}  // namespace omnios
