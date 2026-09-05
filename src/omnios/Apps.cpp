#include "Apps.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "Paths.h"
#include "Router.h"

namespace omnios {
namespace {

// The app registry.
//
// mpv is started with a window forced open: launched bare from a tile it would
// otherwise exit immediately having been given no file, which on a console
// looks exactly like a crash.
//
// Thunar opens on the games directory rather than $HOME. On a console the
// reason to open a file manager is almost always to move a game or read a USB
// stick, and §18's install flow starts from exactly there.
const std::vector<App> kApps = {
    // First on the tab on purpose. On a console the store is how the machine
    // grows, so it should be the thing under the cursor when the tab opens
    // rather than something to hunt for at the end of a row.
    //
    // GNOME Software is Ubuntu's store — the same program, before Canonical
    // renamed it — and Arch builds it with the Flatpak backend, so it offers
    // Flathub rather than the repositories. That is the right catalogue for a
    // console: a Flatpak carries its own libraries, so installing one cannot
    // drag the base system into a partial upgrade the way "pacman -Sy vlc"
    // could.
    {"store", "Store", "gnome-software", "", "gnome-software",
     "org.gnome.Software",
     "Install apps from Flathub",
     "#8B5CF6", true},

    // Both --vo and --gpu-context are pinned, and the second one is the one
    // that matters. Without hardware GL, mpv crashes here:
    //
    //   MESA-EGL: warning: egl: failed to create dri2 screen
    //   mpv: video/out/x11_common.c:679: vo_x11_init: Assertion !vo->x11 failed
    //
    // vo_x11_init is called by the *gpu* output's X11 context, not by the x11
    // output, so restricting --vo alone changes nothing — it was tried and the
    // assertion came back unchanged. --gpu-context=wayland is what keeps mpv
    // off X11; OmniOS is a Wayland system, so nothing is given up.
    //
    // --vo=gpu,wlshm then means: use the GPU on a real machine, and fall back
    // to Wayland shared memory (software scaling) where there is none.
    {"video", "Video Player", "mpv",
     "--player-operation-mode=pseudo-gui --force-window=yes --idle=yes "
     "--vo=gpu,wlshm --gpu-context=wayland",
     "mpv", "mpv",
     "Play video and music from a drive or USB stick",
     "#6C63FF", true},

    {"files", "Files", "thunar", "%GAMES%", "thunar", "org.xfce.thunar",
     "Browse drives, copy games, open a USB stick",
     "#3A8FFF", true},

    // Chromium, not Chrome: Chrome is AUR-only, chromium is the same engine in
    // the official repos and needs no build step on first boot.
    //
    // --ozone-platform=wayland for the same reason mpv pins its context —
    // letting a browser fall back to XWayland on a machine with no GPU is how
    // the mpv crash happened.
    // --no-first-run matters more than it looks: without it chromium opens on
    // a terms-of-service dialog with Cancel/Accept, which is not something a
    // console should ever put in front of someone. --no-default-browser-check
    // suppresses the other startup prompt for the same reason.
    {"browser", "Browser", "chromium",
     "--ozone-platform=wayland --no-first-run --no-default-browser-check "
     "--start-maximized",
     "chromium", "chromium",
     "Browse the web",
     "#3A8FFF", true},

    // YouTube is a web app, so it is Chromium in app mode: no tabs, no
    // address bar, just the site. The icon is Chromium's own, which is honest
    // about what is actually running.
    {"youtube", "YouTube", "chromium",
     "--ozone-platform=wayland --no-first-run --no-default-browser-check "
     "--app=https://www.youtube.com --start-fullscreen",
     "chromium", "chromium",
     "Watch YouTube",
     "#E4000F", true},
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
