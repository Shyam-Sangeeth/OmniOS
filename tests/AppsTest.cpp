#include <cstdlib>

#include "Test.h"
#include "omnios/Apps.h"
#include "omnios/Paths.h"

using namespace omnios;

TEST("apps: the registry has the two a console cannot do without") {
    CHECK(findApp("video") != nullptr);
    CHECK(findApp("files") != nullptr);
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
    const App* video = findApp("video");
    CHECK(video != nullptr);
    if (video == nullptr) return;

    const std::vector<std::string> argv = appArgv(*video);
    CHECK(!argv.empty());
    CHECK_EQ(argv.front(), std::string("mpv"));
    // The window flags matter: launched bare from a tile, mpv would exit at
    // once having been given no file, which looks like a crash on a console.
    bool forcesWindow = false;
    for (const std::string& arg : argv) {
        if (arg.find("force-window") != std::string::npos) forcesWindow = true;
    }
    CHECK(forcesWindow);
}

TEST("apps: %GAMES% expands to the real games directory") {
#ifdef _WIN32
    _putenv_s("OMNIOS_GAMES_DIR", "C:/tmp/omni-games");
#else
    setenv("OMNIOS_GAMES_DIR", "/tmp/omni-games", 1);
#endif

    const App* files = findApp("files");
    CHECK(files != nullptr);
    if (files == nullptr) return;

    const std::vector<std::string> argv = appArgv(*files);
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
