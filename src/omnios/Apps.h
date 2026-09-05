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
    std::string_view description;
    // Tile badge colour, from the UI palette (OmniOS.md §12).
    std::string_view badgeColor;
};

const std::vector<App>& allApps();

// Nullptr when unknown.
const App* findApp(std::string_view id);

// True when the app's command resolves on PATH.
bool appAvailable(const App& app);

// Full argv for launching, with %GAMES% expanded. argv[0] is the command.
std::vector<std::string> appArgv(const App& app);

}  // namespace omnios
