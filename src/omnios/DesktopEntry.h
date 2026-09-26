// Installed applications, discovered from freedesktop desktop entries.
//
// OmniOS does not keep a catalogue of what you can install — GNOME Software
// does that, against Flathub. What OmniOS needs is the other half: knowing what
// is on the machine now, so that installing something from the store puts a
// tile on the Apps tab without anyone having to add a row to a table here.
//
// Desktop entries are how every launcher on Linux answers that question, and
// Flatpak exports one per app into a directory that is already on the search
// path, so a Flathub install is picked up with no special case at all.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace omnios {

struct DesktopApp {
    // Desktop file id — the basename without ".desktop". For a Flatpak this is
    // also the application id, which is what uninstalling it needs.
    std::string id;
    std::string name;
    std::string comment;
    // Exec with the field codes (%U, %f, %i …) removed. Quoted arguments are
    // honoured, so an Exec line with a path containing spaces survives.
    std::vector<std::string> argv;
    std::string icon;
    std::filesystem::path path;
    // Installed through Flatpak rather than pacman. Decides how it is removed,
    // and there is no reliable way to tell after the fact except where the
    // entry came from.
    bool flatpak = false;
};

// Every visible application on the machine, sorted by name and de-duplicated by
// id (earlier directories win, matching XDG precedence).
//
// Excluded: entries marked NoDisplay or Hidden, anything that is not
// Type=Application, terminal programs, entries whose executable is missing, and
// anything listed in the baseline file below.
std::vector<DesktopApp> installedApps();

// Directories searched, in precedence order. Exposed for the tests, which need
// to know what to populate.
std::vector<std::filesystem::path> applicationDirs();

// Desktop ids that shipped with the image and should not appear as tiles.
//
// Without this the Apps tab is a junk drawer: a console image drags in several
// dozen entries nobody chose — settings dialogs from the file manager's
// dependencies, Vulkan and Avahi tools, an Xwayland launcher. The list is
// written at build time by scripts/build-iso.sh, straight after mkarchiso has
// assembled the root filesystem, so it is exactly "what came with the image"
// rather than a guess maintained by hand.
//
// A missing file means no filtering, which is the safe direction: a developer
// build shows everything rather than nothing.
const std::vector<std::string>& baselineApps();
std::filesystem::path baselineAppsFile();

// The desktops this session calls itself, from XDG_CURRENT_DESKTOP — a
// colon-separated list such as "KDE" or "Hyprland". Empty when unset.
std::vector<std::string> currentDesktops();

// Parses one desktop entry. Returns false when the file is not a visible
// application. Exposed for testing.
//
// OnlyShowIn and NotShowIn are judged against currentDesktops(). That matters
// now that the image has two sessions: an entry written for one of them — the
// Plasma desktop's "Game Mode" shortcut, the dozens of KDE panels marked
// OnlyShowIn=KDE — must not turn up as a tile in the other.
bool parseDesktopEntry(const std::filesystem::path& file, DesktopApp& out);

// Same, judged against the given desktops instead of the environment's, so the
// tests can ask "would this show under KDE?" without setting variables.
bool parseDesktopEntry(const std::filesystem::path& file, DesktopApp& out,
                       const std::vector<std::string>& desktops);

}  // namespace omnios
