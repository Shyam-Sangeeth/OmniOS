#include "Platform.h"

#include <algorithm>

namespace omnios {
namespace {

// The registry. Order is launcher display order (OmniOS.md §7.1).
const std::vector<PlatformInfo> kPlatforms = {
    {Platform::Linux,    "linux",    "pc",       "Linux",     Tier::Native,   "#3A8FFF"},
    {Platform::Windows,  "windows",  "pc",       "Windows",   Tier::ApiLayer, "#3A8FFF"},
    {Platform::PS5,      "ps5",      "ps5",      "PS5",       Tier::ApiLayer, "#0070D1"},
    {Platform::PS4,      "ps4",      "ps4",      "PS4",       Tier::ApiLayer, "#0070D1"},
    {Platform::PS3,      "ps3",      "ps3",      "PS3",       Tier::Emulator, "#0070D1"},
    {Platform::PS2,      "ps2",      "ps2",      "PS2",       Tier::Emulator, "#0070D1"},
    {Platform::PS1,      "ps1",      "ps1",      "PS1",       Tier::Emulator, "#0070D1"},
    {Platform::Switch,   "switch",   "switch",   "Switch",    Tier::Jit,      "#E4000F"},
    {Platform::GameCube, "gamecube", "gamecube", "GameCube",  Tier::Emulator, "#E4000F"},
    {Platform::Wii,      "wii",      "wii",      "Wii",       Tier::Emulator, "#E4000F"},
    {Platform::N3DS,     "3ds",      "3ds",      "3DS",       Tier::Emulator, "#E4000F"},
    {Platform::GBA,      "gba",      "gba",      "GBA",       Tier::Emulator, "#E4000F"},
    {Platform::Android,  "android",  "android",  "Android",   Tier::Jit,      "#888888"},
    {Platform::Retro,    "retro",    "retro",    "Retro",     Tier::Emulator, "#888888"},
    // Native because Steam ships the runtime a game needs; OmniOS only asks it
    // to start one. Its folder is ~/Games/Steam, capitalised as Steam spells
    // itself: it is the one folder named after a store rather than a machine.
    {Platform::Steam,    "steam",    "Steam",    "Steam",     Tier::Native,   "#1B2838"},
};

const PlatformInfo kUnknown = {
    Platform::Unknown, "unknown", "unsorted", "Unknown", Tier::Emulator, "#888888"};

}  // namespace

std::string_view tierName(Tier tier) {
    switch (tier) {
        case Tier::Native:   return "native";
        case Tier::ApiLayer: return "api_layer";
        case Tier::Jit:      return "jit";
        case Tier::Emulator: return "emulator";
    }
    return "unknown";
}

const std::vector<PlatformInfo>& allPlatforms() { return kPlatforms; }

const PlatformInfo& platformInfo(Platform platform) {
    const auto it = std::find_if(kPlatforms.begin(), kPlatforms.end(),
                                 [platform](const PlatformInfo& info) {
                                     return info.platform == platform;
                                 });
    return it == kPlatforms.end() ? kUnknown : *it;
}

Platform platformFromId(std::string_view id) {
    const auto it = std::find_if(
        kPlatforms.begin(), kPlatforms.end(),
        [id](const PlatformInfo& info) { return info.id == id; });
    return it == kPlatforms.end() ? Platform::Unknown : it->platform;
}

std::string_view platformId(Platform platform) { return platformInfo(platform).id; }
std::string_view platformDisplayName(Platform platform) { return platformInfo(platform).displayName; }
Tier             platformTier(Platform platform) { return platformInfo(platform).tier; }
std::string_view platformFolder(Platform platform) { return platformInfo(platform).folder; }

}  // namespace omnios
