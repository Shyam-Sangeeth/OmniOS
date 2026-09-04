// Compatibility engine — Phase 9 (OmniOS.md §11, §16, §20).
//
// Turns "the user pressed Play on this tile" into an exact command line. The
// engine table is data, not code, because the emulator landscape moves: Ryujinx
// was discontinued in 2024 and Citra was taken down the same year, so the
// engine behind a platform has to be swappable without touching the router.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "GameLibrary.h"
#include "Platform.h"

namespace omnios {

struct Engine {
    std::string_view id;
    std::string_view displayName;
    // Executable invoked, looked up on PATH.
    std::string_view command;
    // Arch package that provides it, shown in the "layer missing" prompt
    // (OmniOS.md §11, step 9.5).
    std::string_view package;
    Tier             tier;
    std::string_view notes;
};

// Every engine this build knows how to drive.
const std::vector<Engine>& allEngines();

// Returns nullptr for an unknown id.
const Engine* findEngine(std::string_view id);

// The engine that runs `platform` by default. Nullptr for Platform::Unknown.
const Engine* defaultEngine(Platform platform);

struct LaunchOptions {
    // Wrap in gamemoderun / mangohud (OmniOS.md §6.1, §6.2).
    bool gameMode = true;
    bool mangoHud = false;
    // Force an engine by id, overriding the game's own preference.
    std::string engine;
    // Report the plan without checking whether the engine is installed. Used by
    // the CLI's dry run and by tests, which run on machines with no emulators.
    bool skipAvailabilityCheck = false;
};

struct LaunchPlan {
    // Full command line, argv[0] first. Empty when `ok` is false.
    std::vector<std::string> argv;
    // Environment overrides to apply to the child process.
    std::map<std::string, std::string> environment;
    std::string engineId;
    std::string engineDisplayName;
    Tier        tier = Tier::Native;
    // Absolute path handed to the engine.
    std::string target;

    bool        ok = false;
    // Why the plan could not be built, in words meant for the launcher's error
    // screen rather than a log file.
    std::string error;
    // Set when the engine is known but not installed, so the UI can offer the
    // one-line pacman/yay command that fixes it.
    std::string installHint;

    // Single-line rendering of argv, for logs and the CLI.
    std::string commandLine() const;
};

// Builds the command line for a game. Never throws and never runs anything.
LaunchPlan planLaunch(const Game& game, const LaunchOptions& options = {});

// True when `command` resolves on PATH.
bool commandExists(std::string_view command);

}  // namespace omnios
