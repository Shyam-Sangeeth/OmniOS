#include <cstdlib>

#include "Test.h"
#include "omnios/Apps.h"
#include "omnios/Paths.h"

using namespace omnios;

TEST("apps: the registry has Steam and nothing it does not know") {
    CHECK(findApp("steam") != nullptr);
    CHECK(findApp("nope") == nullptr);
}

TEST("apps: every row is complete") {
    for (const App& app : allApps()) {
        CHECK(!app.id.empty());
        CHECK(!app.name.empty());
        CHECK(!app.command.empty());
        // A missing app has to name the package that provides it, the same way
        // a missing emulator does.
        CHECK(!app.package.empty());
        CHECK(!app.description.empty());
        CHECK(!app.badgeColor.empty());
        CHECK(app.badgeColor.front() == '#');
        CHECK(findApp(app.id) != nullptr);
    }
}

TEST("apps: argv starts with the command") {
    const App* steam = findApp("steam");
    CHECK(steam != nullptr);
    if (steam == nullptr) return;

    const std::vector<std::string> argv = appArgv(*steam);
    CHECK_EQ(argv.size(), std::size_t(1));
    CHECK_EQ(argv.front(), std::string("steam"));
}

TEST("apps: every app names an icon") {
    for (const App& app : allApps()) {
        // A wrong or missing icon name shows as a blank tile with no error,
        // so the name being present is worth asserting even though the file
        // only exists on the installed system.
        CHECK(!app.icon.empty());
    }
}

TEST("apps: %GAMES% expands to the real games directory") {
#ifdef _WIN32
    _putenv_s("OMNIOS_GAMES_DIR", "C:/tmp/omni-games");
#else
    setenv("OMNIOS_GAMES_DIR", "/tmp/omni-games", 1);
#endif

    // No row uses the token today, so a row is made for the test: the
    // substitution is the registry's contract, not any one app's.
    const App opener{"opener", "Opener", "xdg-open", "%GAMES%", "xdg-utils", "folder",
                     "Open the games folder", "#3A8FFF", false};
    const std::vector<std::string> argv = appArgv(opener);
    CHECK_EQ(argv.size(), std::size_t(2));

    // The token must be gone and the real path in its place — a file manager
    // opening a literal "%GAMES%" directory would be worse than opening $HOME.
    CHECK(argv[1].find("%GAMES%") == std::string::npos);
    CHECK_EQ(argv[1], gamesDir().generic_string());

#ifdef _WIN32
    _putenv_s("OMNIOS_GAMES_DIR", "");
#else
    unsetenv("OMNIOS_GAMES_DIR");
#endif
}

TEST("apps: an app with no arguments yields just its command") {
    // Guards the splitter against emitting empty strings, which would become
    // stray empty argv entries.
    for (const App& app : allApps()) {
        for (const std::string& arg : appArgv(app)) CHECK(!arg.empty());
    }
}

// ---- system apps ----------------------------------------------------------

TEST("apps: Steam can be uninstalled from its tile") {
    // Nothing about the console stops working without it.
    CHECK(!findApp("steam")->system);
    CHECK(!packageIsProtected("steam"));
}

TEST("apps: a system app's package cannot be removed") {
    for (const App& app : allApps()) {
        if (app.system) CHECK(packageIsProtected(app.package));
    }
}

TEST("apps: an empty package name is refused rather than passed to pacman") {
    // Every caller gets the name from a registry row, so an empty one means a
    // lookup failed. Refusing is the safe reading: "pacman -Rns" with no
    // argument is not a no-op worth finding out about the hard way.
    CHECK(packageIsProtected(""));
}

TEST("apps: nothing a user installs is protected") {
    // Protection exists to keep the console working, not to make packages
    // permanent. Anything not backing a system tile has to be removable.
    CHECK(!packageIsProtected("vlc"));
    CHECK(!packageIsProtected("org.videolan.VLC"));
}
