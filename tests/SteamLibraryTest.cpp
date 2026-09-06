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
