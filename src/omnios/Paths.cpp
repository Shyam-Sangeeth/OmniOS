#include "Paths.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace omnios {
namespace {

namespace fs = std::filesystem;

// Returns the value of `name`, or an empty string when unset or blank. Getenv
// is used directly rather than a cache: the installer changes these mid-run.
std::string env(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : std::string();
}

}  // namespace

fs::path homeDir() {
    if (const std::string home = env("HOME"); !home.empty()) return home;
    // Windows dev hosts, where the launcher's tooling is built and tested.
    if (const std::string profile = env("USERPROFILE"); !profile.empty()) return profile;
    return fs::current_path();
}

fs::path gamesDir() {
    if (const std::string override = env("OMNIOS_GAMES_DIR"); !override.empty())
        return override;
    return homeDir() / "Games";
}

fs::path dataDir() {
    if (const std::string override = env("OMNIOS_DATA_DIR"); !override.empty())
        return override;
    return homeDir() / ".omnios";
}

fs::path libraryDir() { return dataDir() / "library"; }

fs::path libraryCacheFile() { return dataDir() / "library.json"; }

fs::path platformDir(Platform platform) {
    return gamesDir() / std::string(platformFolder(platform));
}

bool ensureDirectories(std::string& error) {
    error.clear();
    std::error_code ec;

    const auto create = [&](const fs::path& path) {
        fs::create_directories(path, ec);
        if (ec && error.empty())
            error = "could not create " + path.string() + ": " + ec.message();
    };

    create(gamesDir());
    // PS1 and PS2 BIOS files, which the user supplies (Router.h, biosDir).
    create(gamesDir() / "bios");
    for (const PlatformInfo& info : allPlatforms()) {
        // linux and windows share the "pc" folder, so this creates it twice —
        // create_directories is happy either way.
        create(gamesDir() / std::string(info.folder));
    }
    create(libraryDir());

    return error.empty();
}

std::string slugify(std::string_view text) {
    std::string slug;
    slug.reserve(text.size());
    bool pendingSeparator = false;

    for (const unsigned char c : text) {
        if (std::isalnum(c) != 0) {
            if (pendingSeparator && !slug.empty()) slug.push_back('-');
            pendingSeparator = false;
            slug.push_back(static_cast<char>(std::tolower(c)));
        } else {
            // Collapse any run of punctuation or space into one hyphen, and
            // never emit a leading or trailing one.
            pendingSeparator = true;
        }
    }
    return slug.empty() ? std::string("untitled") : slug;
}

}  // namespace omnios
