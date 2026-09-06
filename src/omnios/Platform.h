// Platform registry — the single source of truth for every platform OmniOS
// knows how to store, detect and launch. Adding a platform means adding one
// row to the table in Platform.cpp; nothing else in the codebase enumerates
// platforms by hand.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace omnios {

// Execution tier, per the layer architecture (OmniOS.md §16). Lower is faster:
// the router always prefers the lowest tier that can run a title.
enum class Tier : std::uint8_t {
    Native   = 0,  // direct exec on the host CPU
    ApiLayer = 1,  // syscall/graphics translation (Proton, shadPS4)
    Jit      = 2,  // runtime recompilation (Ryujinx, Waydroid)
    Emulator = 3,  // full machine emulation (RPCS3, PCSX2, Dolphin)
};

std::string_view tierName(Tier tier);

enum class Platform : std::uint8_t {
    Unknown = 0,
    Linux,
    Windows,
    PS5,
    PS4,
    PS3,
    PS2,
    PS1,
    Switch,
    GameCube,
    Wii,
    N3DS,
    GBA,
    Android,
    Retro,
    // Steam is not a machine, it is a library that knows what it installed. It
    // gets a platform row because everything downstream — the folder, the tile
    // badge, the engine — is keyed on one.
    Steam,
};

struct PlatformInfo {
    Platform    platform;
    // Stable machine-readable id — the "platform" value in an .opkg manifest.
    // Packages in the wild carry it, so it must never change.
    std::string_view id;
    // Folder under ~/Games/ holding titles for this platform. Usually the id,
    // but linux and windows share "pc" (OmniOS.md §7.1).
    std::string_view folder;
    std::string_view displayName;
    Tier             tier;
    // Badge colour from the UI spec (OmniOS.md §12).
    std::string_view badgeColor;
};

// Every known platform except Unknown, in launcher display order.
const std::vector<PlatformInfo>& allPlatforms();

const PlatformInfo& platformInfo(Platform platform);

// "ps4" -> Platform::PS4. Returns Unknown for anything unrecognised, so a
// manifest naming a platform this build does not support degrades instead of
// throwing.
Platform platformFromId(std::string_view id);

std::string_view platformId(Platform platform);
std::string_view platformDisplayName(Platform platform);
Tier             platformTier(Platform platform);

// Folder under ~/Games/ that titles for this platform live in.
std::string_view platformFolder(Platform platform);

}  // namespace omnios
