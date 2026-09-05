#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "Test.h"
#include "omnios/DesktopEntry.h"

using namespace omnios;
namespace fs = std::filesystem;

namespace {

// A desktop entry on disk, so the parser is exercised the way it runs.
fs::path writeEntry(const std::string& name, const std::string& body) {
    const fs::path dir = fs::temp_directory_path() / "omnios-desktop-test";
    fs::create_directories(dir);
    const fs::path file = dir / (name + ".desktop");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << body;
    return file;
}

}  // namespace

TEST("desktop: a normal entry parses") {
    // "sh" rather than a made-up name: the parser drops entries whose program
    // is missing, which is the point of the TryExec check below.
    const fs::path file = writeEntry("plain",
                                     "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Plain App\n"
                                     "Comment=Does a thing\n"
                                     "Icon=plain\n"
                                     "Exec=sh -c true\n");
    DesktopApp app;
    CHECK(parseDesktopEntry(file, app));
    CHECK_EQ(app.id, "plain");
    CHECK_EQ(app.name, "Plain App");
    CHECK_EQ(app.comment, "Does a thing");
    CHECK_EQ(app.icon, "plain");
    CHECK_EQ(app.argv.size(), std::size_t{3});
    CHECK_EQ(app.argv[0], "sh");
    CHECK(!app.flatpak);
}

TEST("desktop: field codes are dropped, not passed through") {
    // Handing "%U" to a program as a literal argument is how a launcher opens
    // an app with a file named %U and no window.
    const fs::path file = writeEntry("codes",
                                     "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Codes\n"
                                     "Exec=sh %U --flag %i %%literal\n");
    DesktopApp app;
    CHECK(parseDesktopEntry(file, app));
    CHECK_EQ(app.argv.size(), std::size_t{3});
    CHECK_EQ(app.argv[1], "--flag");
    // %% is an escaped percent and survives as one.
    CHECK_EQ(app.argv[2], "%literal");
}

TEST("desktop: quoted arguments stay whole") {
    const fs::path file = writeEntry("quoted",
                                     "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Quoted\n"
                                     "Exec=sh \"one two\" three\n");
    DesktopApp app;
    CHECK(parseDesktopEntry(file, app));
    CHECK_EQ(app.argv.size(), std::size_t{3});
    CHECK_EQ(app.argv[1], "one two");
}

TEST("desktop: hidden entries are not applications a user chose") {
    for (const char* key : {"NoDisplay=true\n", "Hidden=true\n", "Terminal=true\n"}) {
        const fs::path file = writeEntry("hidden",
                                         std::string("[Desktop Entry]\n"
                                                     "Type=Application\n"
                                                     "Name=Hidden\n"
                                                     "Exec=sh\n") + key);
        DesktopApp app;
        CHECK(!parseDesktopEntry(file, app));
    }
}

TEST("desktop: a link or directory entry is not an application") {
    const fs::path file = writeEntry("link",
                                     "[Desktop Entry]\n"
                                     "Type=Link\n"
                                     "Name=Somewhere\n"
                                     "URL=https://example.invalid\n");
    DesktopApp app;
    CHECK(!parseDesktopEntry(file, app));
}

TEST("desktop: only the main group is read") {
    // Actions carry their own Name and Exec. Reading them produced a tile that
    // launched the wrong thing, so the group header is tracked rather than the
    // file being scanned as a flat list of keys.
    const fs::path file = writeEntry("actions",
                                     "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Real Name\n"
                                     "Exec=sh\n"
                                     "\n"
                                     "[Desktop Action new]\n"
                                     "Name=New Window\n"
                                     "Exec=sh -c wrong\n");
    DesktopApp app;
    CHECK(parseDesktopEntry(file, app));
    CHECK_EQ(app.name, "Real Name");
    CHECK_EQ(app.argv.size(), std::size_t{1});
}

TEST("desktop: an entry for a program that is not installed is skipped") {
    // TryExec is the spec's own answer to this, and it is why a package can
    // ship an entry for a helper it did not install.
    const fs::path file = writeEntry("missing",
                                     "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Missing\n"
                                     "TryExec=omnios-no-such-program\n"
                                     "Exec=omnios-no-such-program\n");
    DesktopApp app;
    CHECK(!parseDesktopEntry(file, app));
}

TEST("desktop: the search path puts the user ahead of the system") {
    const std::vector<fs::path> dirs = applicationDirs();
    CHECK(!dirs.empty());
    // /usr/share is the fallback, so it must come last of the fixed entries —
    // otherwise a user's own override never wins.
    const auto system = std::find(dirs.begin(), dirs.end(), fs::path("/usr/share/applications"));
    const auto flatpak =
        std::find(dirs.begin(), dirs.end(), fs::path("/var/lib/flatpak/exports/share/applications"));
    CHECK(system != dirs.end());
    CHECK(flatpak != dirs.end());
    CHECK(flatpak < system);
}

TEST("desktop: a Flatpak's file-forwarding markers are removed") {
    // The real thing, copied from what flatpak exports for org.videolan.VLC.
    // Leaving @@u and @@ in place hands flatpak two arguments it believes are
    // files, and the app starts and immediately exits — which is exactly how
    // "apps installed from the store will not open" showed up.
    const fs::path file =
        writeEntry("org.videolan.VLC",
                   "[Desktop Entry]\n"
                   "Type=Application\n"
                   "Name=VLC media player\n"
                   "Icon=org.videolan.VLC\n"
                   "Exec=sh run --branch=stable --arch=x86_64 "
                   "--command=/app/bin/vlc --file-forwarding org.videolan.VLC "
                   "--started-from-file @@u %U @@\n");
    DesktopApp app;
    CHECK(parseDesktopEntry(file, app));

    for (const std::string& arg : app.argv) {
        CHECK(arg != "@@");
        CHECK(arg != "@@u");
        CHECK(arg != "%U");
    }
    // The application id has to survive: it is what flatpak runs, and it is
    // also how the app is uninstalled again.
    CHECK(std::find(app.argv.begin(), app.argv.end(), "org.videolan.VLC") != app.argv.end());
    CHECK_EQ(app.argv.back(), "--started-from-file");
}

