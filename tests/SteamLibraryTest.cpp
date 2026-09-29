#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Test.h"
#include "omnios/Router.h"
#include "omnios/SteamLibrary.h"

using namespace omnios;
namespace fs = std::filesystem;

namespace {

fs::path steamappsDir() {
    const fs::path dir = fs::temp_directory_path() / "omnios-steam-test" / "steamapps";
    fs::create_directories(dir);
    return dir;
}

fs::path writeManifest(const std::string& name, const std::string& body) {
    const fs::path file = steamappsDir() / name;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << body;
    return file;
}

}  // namespace

TEST("steam: a manifest yields the title, the app id and the size") {
    // The real shape, tabs and all — this is what Steam writes.
    const fs::path file = writeManifest("appmanifest_440.acf",
                                        "\"AppState\"\n"
                                        "{\n"
                                        "\t\"appid\"\t\t\"440\"\n"
                                        "\t\"name\"\t\t\"Team Fortress 2\"\n"
                                        "\t\"installdir\"\t\t\"Team Fortress 2\"\n"
                                        "\t\"SizeOnDisk\"\t\t\"22000000000\"\n"
                                        "}\n");
    Game game;
    CHECK(readSteamManifest(file, game));
    CHECK_EQ(game.title, "Team Fortress 2");
    CHECK_EQ(game.launchId, "440");
    CHECK_EQ(game.id, "steam.440");
    CHECK(game.platform == Platform::Steam);
    CHECK_EQ(game.sizeBytes, std::uint64_t{22000000000ULL});
    // Steam told us; nothing was guessed from a filename or a folder.
    CHECK(game.detectionSource == DetectionSource::Manifest);
}

TEST("steam: a manifest with no app id is not a game") {
    // What an interrupted install leaves behind. Without an id there is
    // nothing to launch, so a tile would be a button that cannot work.
    const fs::path file = writeManifest("appmanifest_broken.acf",
                                        "\"AppState\"\n{\n\t\"name\"\t\t\"Half a game\"\n}\n");
    Game game;
    CHECK(!readSteamManifest(file, game));
}

TEST("steam: a game with no name still gets a usable tile") {
    const fs::path file = writeManifest("appmanifest_620.acf",
                                        "\"AppState\"\n{\n\t\"appid\"\t\t\"620\"\n}\n");
    Game game;
    CHECK(readSteamManifest(file, game));
    CHECK_EQ(game.launchId, "620");
    CHECK(!game.title.empty());
}

TEST("steam: the library is every manifest in the folder") {
    writeManifest("appmanifest_10.acf",
                  "\"AppState\"\n{\n\t\"appid\"\t\t\"10\"\n\t\"name\"\t\t\"Counter-Strike\"\n}\n");
    // Not a manifest, and not a game: the folder holds Steam's own files too.
    writeManifest("libraryfolders.vdf", "\"libraryfolders\"\n{\n}\n");

    const std::vector<Game> games = readSteamLibrary(steamappsDir());
    CHECK(games.size() >= std::size_t{2});
    for (const Game& game : games) {
        CHECK(!game.launchId.empty());
        CHECK(game.platform == Platform::Steam);
    }
}

TEST("steam: a directory that is not a Steam library yields nothing") {
    CHECK(readSteamLibrary(fs::temp_directory_path() / "omnios-no-such-steam").empty());
}

TEST("steam: a game is launched by app id, not by a path") {
    // Handing anyone the executable does not work: the client has to be running
    // for DRM, the overlay and cloud saves, so the route is through Steam.
    Game game;
    game.id       = "steam.440";
    game.title    = "Team Fortress 2";
    game.platform = Platform::Steam;
    game.launchId = "440";

    LaunchOptions options;
    options.skipAvailabilityCheck = true;
    const LaunchPlan plan = planLaunch(game, options);

    CHECK(plan.ok);
    CHECK_EQ(plan.argv.size(), std::size_t{2});
    CHECK_EQ(plan.argv[0], "steam");
    CHECK_EQ(plan.argv[1], "steam://rungameid/440");
}

namespace {

// A fresh, empty directory of its own for a test that counts what it finds.
fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "omnios-steam-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& file, const std::string& body) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << body;
}

std::string manifest(const std::string& appId, const std::string& name,
                     const std::string& stateFlags = "4") {
    return "\"AppState\"\n{\n\t\"appid\"\t\t\"" + appId + "\"\n\t\"name\"\t\t\"" + name +
           "\"\n\t\"StateFlags\"\t\t\"" + stateFlags + "\"\n}\n";
}

}  // namespace

TEST("steam: Proton, the runtimes and the redistributables are not games") {
    // Each of these lands in steamapps with a manifest like any game the first
    // time a Windows game is installed. Tiles for them would start nothing.
    const fs::path dir = freshDir("tools");
    writeFile(dir / "appmanifest_1493710.acf", manifest("1493710", "Proton Experimental"));
    writeFile(dir / "appmanifest_2805730.acf", manifest("2805730", "Proton 9.0"));
    writeFile(dir / "appmanifest_1628350.acf", manifest("1628350", "Steam Linux Runtime 3.0 (sniper)"));
    writeFile(dir / "appmanifest_228980.acf", manifest("228980", "Steamworks Common Redistributables"));
    writeFile(dir / "appmanifest_570.acf", manifest("570", "Dota 2"));

    const std::vector<Game> games = readSteamLibrary(dir);
    CHECK_EQ(games.size(), std::size_t{1});
    CHECK_EQ(games.at(0).title, "Dota 2");
}

TEST("steam: a game still downloading has no tile until it finishes") {
    // 1026 is what Steam writes while a first download runs; 4 is installed;
    // 6 is installed with an update waiting, which still plays.
    const fs::path dir = freshDir("state");
    Game game;
    writeFile(dir / "appmanifest_1.acf", manifest("1", "Downloading", "1026"));
    CHECK(!readSteamManifest(dir / "appmanifest_1.acf", game));
    writeFile(dir / "appmanifest_2.acf", manifest("2", "Installed", "4"));
    CHECK(readSteamManifest(dir / "appmanifest_2.acf", game));
    writeFile(dir / "appmanifest_3.acf", manifest("3", "Update waiting", "6"));
    CHECK(readSteamManifest(dir / "appmanifest_3.acf", game));
}

TEST("steam: an app id that is not a number is not Steam's") {
    // It would otherwise become part of a cover path and a steam:// URL.
    const fs::path dir = freshDir("badid");
    writeFile(dir / "appmanifest_x.acf", manifest("../../etc", "Odd"));
    Game game;
    CHECK(!readSteamManifest(dir / "appmanifest_x.acf", game));
}

TEST("steam: libraryfolders.vdf lists the other libraries") {
    // A second drive is the usual reason for one: the games there are no less
    // installed for not being under ~/Games.
    const fs::path dir = freshDir("folders");
    writeFile(dir / "libraryfolders.vdf",
              "\"libraryfolders\"\n{\n"
              "\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/me/.local/share/Steam\"\n"
              "\t\t\"label\"\t\t\"\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"570\"\t\t\"123\"\n\t\t}\n\t}\n"
              "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"/mnt/games/SteamLibrary\"\n\t}\n"
              "}\n");
    const std::vector<fs::path> libraries = steamLibraryFolders(dir / "libraryfolders.vdf");
    CHECK_EQ(libraries.size(), std::size_t{2});
    CHECK(libraries.at(0) == fs::path("/home/me/.local/share/Steam") / "steamapps");
    CHECK(libraries.at(1) == fs::path("/mnt/games/SteamLibrary") / "steamapps");
    CHECK(steamLibraryFolders(dir / "missing.vdf").empty());
}

TEST("steam: the cover comes from Steam's library cache, in any of its layouts") {
    const fs::path client = freshDir("client");
    const fs::path cache = client / "appcache" / "librarycache";

    writeFile(cache / "10" / "library_600x900.jpg", "jpg");            // today's
    writeFile(cache / "20" / "0a1b2c" / "library_600x900.jpg", "jpg"); // hashed
    writeFile(cache / "30_library_600x900.jpg", "jpg");                // before 2024
    writeFile(cache / "40" / "header.jpg", "jpg");                     // wide only
    writeFile(cache / "50" / "header.jpg", "jpg");
    writeFile(cache / "50" / "library_600x900.jpg", "jpg");

    CHECK(steamCover(client, "10") == cache / "10" / "library_600x900.jpg");
    CHECK(steamCover(client, "20") == cache / "20" / "0a1b2c" / "library_600x900.jpg");
    CHECK(steamCover(client, "30") == cache / "30_library_600x900.jpg");
    CHECK(steamCover(client, "40") == cache / "40" / "header.jpg");
    // The tall capsule is the tile's shape, so it wins over the header.
    CHECK(steamCover(client, "50") == cache / "50" / "library_600x900.jpg");
    CHECK(steamCover(client, "60").empty());
    CHECK(steamCover(client, "..").empty());
}

TEST("steam: the library stamp follows the tiles, not every write") {
    // Steam rewrites a manifest every few seconds during a download. Following
    // those would rescan, and reset the grid, the whole time.
    const fs::path dir = freshDir("stamp");
    writeFile(dir / "appmanifest_570.acf", manifest("570", "Dota 2"));
    const std::string before = steamLibraryStamp({dir});

    writeFile(dir / "appmanifest_730.acf", manifest("730", "Counter-Strike 2", "1026"));
    CHECK_EQ(steamLibraryStamp({dir}), before);

    writeFile(dir / "appmanifest_730.acf", manifest("730", "Counter-Strike 2", "4"));
    const std::string installed = steamLibraryStamp({dir});
    CHECK(installed != before);

    fs::remove(dir / "appmanifest_730.acf");
    CHECK_EQ(steamLibraryStamp({dir}), before);
}

TEST("steam: when a game was last played comes from its manifest, and survives the cache") {
    // Steam writes LastPlayed as seconds since 1970, and 0 until the first run.
    const fs::path dir = freshDir("played");
    writeFile(dir / "appmanifest_570.acf",
              "\"AppState\"\n{\n\t\"appid\"\t\t\"570\"\n\t\"name\"\t\t\"Dota 2\"\n"
              "\t\"StateFlags\"\t\t\"4\"\n\t\"LastPlayed\"\t\t\"1790000000\"\n}\n");
    writeFile(dir / "appmanifest_730.acf", manifest("730", "Counter-Strike 2"));

    Game played, never;
    CHECK(readSteamManifest(dir / "appmanifest_570.acf", played));
    CHECK(readSteamManifest(dir / "appmanifest_730.acf", never));
    CHECK_EQ(played.lastPlayed, std::int64_t{1790000000});
    CHECK_EQ(never.lastPlayed, std::int64_t{0});

    CHECK_EQ(Game::fromJson(played.toJson()).lastPlayed, std::int64_t{1790000000});
    CHECK_EQ(Game::fromJson(never.toJson()).lastPlayed, std::int64_t{0});
}

TEST("steam: a running game is found by its reaper, and nothing else is") {
    // A fake /proc: the reaper Steam starts a game under, the Steam client
    // itself, a process whose game command mentions AppId, and a non-process.
    const fs::path proc = freshDir("proc");
    const auto cmdline = [&](const std::string& pid, const std::vector<std::string>& args) {
        std::string body;
        for (const std::string& a : args) body += a + '\0';
        writeFile(proc / pid / "cmdline", body);
    };
    cmdline("4242", {"/home/me/.local/share/Steam/ubuntu12_32/reaper", "SteamLaunch",
                     "AppId=570", "--", "/home/me/Games/Steam/common/dota 2 beta/game/dota.sh"});
    cmdline("100", {"/home/me/.local/share/Steam/ubuntu12_32/steam", "steam://rungameid/570"});
    cmdline("200", {"/usr/bin/python", "--", "SteamLaunch", "AppId=999"});
    writeFile(proc / "self" / "cmdline", std::string("reaper\0SteamLaunch\0AppId=1\0", 26));

    const std::vector<SteamGameProcess> games = runningSteamGames(proc);
    CHECK_EQ(games.size(), std::size_t{1});
    CHECK_EQ(games.at(0).pid, 4242);
    CHECK_EQ(games.at(0).appId, "570");

    CHECK(runningSteamGames(proc / "missing").empty());
}

TEST("steam: the client is found by its process name") {
    // steamwebhelper and a game's reaper are not the client.
    const fs::path proc = freshDir("client");
    writeFile(proc / "30" / "comm", "steamwebhelper\n");
    writeFile(proc / "31" / "comm", "reaper\n");
    CHECK(!steamClientRunning(proc));
    writeFile(proc / "32" / "comm", "steam\n");
    CHECK(steamClientRunning(proc));
    CHECK(!steamClientRunning(proc / "missing"));
}

TEST("steam: stopping a game reaches everything under its reaper") {
    // reaper 10 -> wrapper 11 -> game 12 -> helper 13; 20 is unrelated. The
    // game's name has a space and a parenthesis, as comm can.
    const fs::path proc = freshDir("tree");
    const auto stat = [&](int pid, const std::string& comm, int ppid) {
        writeFile(proc / std::to_string(pid) / "stat",
                  std::to_string(pid) + " (" + comm + ") S " + std::to_string(ppid) + " 1 1 0\n");
    };
    stat(10, "reaper", 1);
    stat(11, "steam-launch-wr", 10);
    stat(12, "Game (x64) main", 11);
    stat(13, "helper", 12);
    stat(20, "kwin_wayland", 1);

    std::vector<int> below = descendantsOf(10, proc);
    std::sort(below.begin(), below.end());
    CHECK_EQ(below.size(), std::size_t{3});
    CHECK_EQ(below.at(0), 11);
    CHECK_EQ(below.at(2), 13);
    CHECK(descendantsOf(20, proc).empty());
}

TEST("steam: a process is recognised by its start time, not its number alone") {
    // Field 22 of stat. A game that ignored being asked to stop is finished
    // off only if the same process still holds the number.
    const fs::path proc = freshDir("start");
    writeFile(proc / "77" / "stat",
              "77 (Game (x64) main) S 1 77 77 0 -1 4194304 100 0 0 0 5 3 0 0 20 0 4 0 "
              "123456 1000000 500 18446744073709551615\n");
    CHECK_EQ(processStartTime(77, proc), std::uint64_t{123456});
    CHECK_EQ(processStartTime(78, proc), std::uint64_t{0});
}
