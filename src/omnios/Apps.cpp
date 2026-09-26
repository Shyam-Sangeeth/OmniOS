#include "Apps.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "Paths.h"
#include "Router.h"

namespace omnios {
namespace {

// The app registry: the tiles on Game Mode's Apps tab that are not found from
// desktop entries.
//
// Only Steam. Game Mode runs inside the Plasma session, so a browser, a file
// manager, a video player, a store and settings are all a switch to the
// desktop away, where they are Plasma's own; on the library they were tiles
// that opened desktop apps full screen. Steam stays because it is a library in
// its own right, and one you open to play.
//
// Apps someone installs still turn up on the tab, found from their desktop
// entries (see DesktopEntry.cpp).
const std::vector<App> kApps = {
    // Steam ships with the image, so the baseline filter hides its desktop
    // entry from the Apps tab — which is right for RetroArch and Dolphin, whose
    // job is to be launched by a game tile, and wrong for this one. Steam is a
    // library you open and browse, so it needs a tile of its own.
    //
    // Not a system app: nothing about the console stops working without it.
    {"steam", "Steam", "steam", "", "steam", "steam",
     "Your Steam library, and the store",
     "#1B2838", false},
};

}  // namespace

const std::vector<App>& allApps() { return kApps; }

bool packageIsProtected(std::string_view package) {
    if (package.empty()) return true;  // unknown provenance: refuse
    return std::any_of(kApps.begin(), kApps.end(), [package](const App& app) {
        return app.system && app.package == package;
    });
}

const App* findApp(std::string_view id) {
    const auto it = std::find_if(kApps.begin(), kApps.end(),
                                 [id](const App& app) { return app.id == id; });
    return it == kApps.end() ? nullptr : &*it;
}

bool appAvailable(const App& app) { return commandExists(app.command); }

std::string iconPathFor(std::string_view icon) {
    if (icon.empty()) return {};

    namespace fs = std::filesystem;
    const std::string name(icon);
    std::error_code ec;

    // A desktop entry is allowed to give an absolute path instead of a themed
    // name, and several do. Nothing else here would find those.
    if (!name.empty() && name.front() == '/') {
        return fs::exists(fs::path(name), ec) ? name : std::string();
    }

    // Icon theme roots, in the order a lookup should prefer them.
    //
    // The Flatpak exports come first and are the reason this list exists at
    // all: a Flathub app installs its icon under /var/lib/flatpak/exports, and
    // a search that only knew about /usr/share found nothing, so every app
    // installed from the store drew as a plain colour wash while the built-in
    // tiles had real logos.
    std::vector<fs::path> roots;
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        roots.emplace_back(fs::path(home) / ".local/share/flatpak/exports/share/icons");
        roots.emplace_back(fs::path(home) / ".local/share/icons");
    }
    roots.emplace_back("/var/lib/flatpak/exports/share/icons");
    roots.emplace_back("/usr/local/share/icons");
    roots.emplace_back("/usr/share/icons");

    // Largest first: these are drawn at tile size, and upscaling a 48px icon
    // looks worse than downscaling a 256px one.
    static const char* const kSizes[] = {
        "512x512", "256x256", "192x192", "128x128", "96x96", "64x64", "48x48",
    };

    for (const fs::path& root : roots) {
        for (const char* size : kSizes) {
            const fs::path png = root / "hicolor" / size / "apps" / (name + ".png");
            if (fs::exists(png, ec)) return png.generic_string();
        }
    }

    // Loose icons, which is where a package with no theme integration puts one.
    for (const char* dir : {"/usr/share/pixmaps/", "/usr/local/share/pixmaps/"}) {
        const fs::path png = fs::path(dir) / (name + ".png");
        if (fs::exists(png, ec)) return png.generic_string();
    }

    // SVG last: qt6-svg can render it, but a themed PNG is cheaper to draw
    // under software rendering.
    for (const fs::path& root : roots) {
        const fs::path svg = root / "hicolor/scalable/apps" / (name + ".svg");
        if (fs::exists(svg, ec)) return svg.generic_string();
    }
    for (const char* dir : {"/usr/share/pixmaps/", "/usr/local/share/pixmaps/"}) {
        const fs::path svg = fs::path(dir) / (name + ".svg");
        if (fs::exists(svg, ec)) return svg.generic_string();
    }
    return {};
}

std::string appIconPath(const App& app) { return iconPathFor(app.icon); }

std::vector<std::string> appArgv(const App& app) {
    std::vector<std::string> argv{std::string(app.command)};

    const std::string args(app.args);
    std::size_t start = 0;
    while (start < args.size()) {
        const std::size_t end = args.find(' ', start);
        const std::string token =
            args.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty()) {
            argv.push_back(token == "%GAMES%" ? gamesDir().generic_string() : token);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return argv;
}

}  // namespace omnios
