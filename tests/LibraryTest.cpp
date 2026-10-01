#include <filesystem>
#include <fstream>

#include "Test.h"
#include "omnios/GameLibrary.h"
#include "omnios/Paths.h"

using namespace omnios;
namespace fs = std::filesystem;

namespace {

Game make(std::string title, Platform platform) {
    Game game;
    game.title    = title;
    game.platform = platform;
    game.id       = makeGameId(platform, title);
    game.path     = fs::path("/home/omni/Games") / std::string(platformFolder(platform)) /
                (title + ".bin");
    return game;
}

// A directory that cleans itself up, so a failing test does not leave debris
// behind in the temp tree.
struct TempDir {
    fs::path path;

    TempDir() {
        path = fs::temp_directory_path() /
               ("omnios-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST("library: add inserts, then replaces by id") {
    GameLibrary library;
    CHECK(library.add(make("Bloodborne", Platform::PS4)));
    CHECK(!library.add(make("Bloodborne", Platform::PS4)));
    CHECK_EQ(library.size(), std::size_t(1));
}

TEST("library: the same title on two platforms is two entries") {
    GameLibrary library;
    library.add(make("Hades", Platform::Linux));
    library.add(make("Hades", Platform::Windows));
    CHECK_EQ(library.size(), std::size_t(2));
}

TEST("library: find and remove work by id") {
    GameLibrary library;
    library.add(make("Celeste", Platform::Linux));

    const Game* found = library.find("linux.celeste");
    CHECK(found != nullptr);
    if (found != nullptr) CHECK_EQ(found->title, std::string("Celeste"));

    CHECK(library.remove("linux.celeste"));
    CHECK(!library.remove("linux.celeste"));
    CHECK(library.empty());
}

TEST("library: search matches title, developer and publisher, case-insensitively") {
    GameLibrary library;
    Game god = make("God of War", Platform::PS4);
    god.developer = "Santa Monica Studio";
    god.publisher = "Sony";
    library.add(god);
    library.add(make("Hollow Knight", Platform::Linux));

    CHECK_EQ(library.search("god").size(), std::size_t(1));
    CHECK_EQ(library.search("SANTA").size(), std::size_t(1));
    CHECK_EQ(library.search("sony").size(), std::size_t(1));
    CHECK_EQ(library.search("o").size(), std::size_t(2));
    CHECK_EQ(library.search("").size(), std::size_t(2));
    CHECK(library.search("nothing here").empty());
}

TEST("library: a search needs every word, each anywhere") {
    Game kart = make("Mario Kart 8 Deluxe", Platform::Switch);
    kart.publisher = "Nintendo";
    CHECK(matchesSearch(kart, "mario kart"));
    CHECK(matchesSearch(kart, "kart  mario"));
    CHECK(matchesSearch(kart, "deluxe nintendo"));
    CHECK(matchesSearch(kart, "  "));
    CHECK(!matchesSearch(kart, "mario party"));
}

TEST("library: byPlatform filters") {
    GameLibrary library;
    library.add(make("A", Platform::PS4));
    library.add(make("B", Platform::PS4));
    library.add(make("C", Platform::Switch));

    CHECK_EQ(library.byPlatform(Platform::PS4).size(), std::size_t(2));
    CHECK_EQ(library.byPlatform(Platform::Switch).size(), std::size_t(1));
    CHECK(library.byPlatform(Platform::PS3).empty());
}

TEST("library: sorting is case-insensitive and stable") {
    GameLibrary library;
    library.add(make("zelda", Platform::Switch));
    library.add(make("Alan Wake", Platform::Windows));
    library.add(make("bloodborne", Platform::PS4));
    library.sortByTitle();

    CHECK_EQ(library.games()[0].title, std::string("Alan Wake"));
    CHECK_EQ(library.games()[1].title, std::string("bloodborne"));
    CHECK_EQ(library.games()[2].title, std::string("zelda"));
}

TEST("library: saves and reloads through the cache file") {
    TempDir temp;
    const fs::path cache = temp.path / "library.json";

    GameLibrary original;
    Game game = make("God of War", Platform::PS4);
    game.developer  = "Santa Monica Studio";
    game.sizeBytes  = 45ULL * 1024 * 1024 * 1024;
    game.tags       = {"action", "adventure"};
    game.executable = "game/eboot.bin";
    game.coverPath  = temp.path / "cover.jpg";
    original.add(game);

    std::string error;
    CHECK(original.save(cache, error));
    CHECK_EQ(error, std::string());

    GameLibrary loaded;
    CHECK(loaded.load(cache, error));
    CHECK_EQ(error, std::string());
    CHECK_EQ(loaded.size(), std::size_t(1));

    const Game* restored = loaded.find("ps4.god-of-war");
    CHECK(restored != nullptr);
    if (restored != nullptr) {
        CHECK_EQ(restored->title, std::string("God of War"));
        CHECK_EQ(restored->developer, std::string("Santa Monica Studio"));
        CHECK_EQ(restored->sizeBytes, game.sizeBytes);
        CHECK_EQ(restored->executable, std::string("game/eboot.bin"));
        CHECK_EQ(restored->tags.size(), std::size_t(2));
        CHECK_EQ(std::string(platformId(restored->platform)), std::string("ps4"));
    }
}

TEST("library: a missing cache file loads as empty and is not an error") {
    TempDir temp;
    GameLibrary library;
    std::string error;

    CHECK(library.load(temp.path / "absent.json", error));
    CHECK_EQ(error, std::string());
    CHECK(library.empty());
}

TEST("library: a corrupt cache reports rather than half-loading") {
    TempDir temp;
    const fs::path cache = temp.path / "library.json";
    { std::ofstream(cache) << "{ this is not json"; }

    GameLibrary library;
    std::string error;
    CHECK(!library.load(cache, error));
    CHECK(error.find("corrupt") != std::string::npos);
    CHECK(library.empty());
}

TEST("library: saving does not destroy the previous cache on a bad path") {
    TempDir temp;
    const fs::path cache = temp.path / "nested" / "deeper" / "library.json";

    GameLibrary library;
    library.add(make("Celeste", Platform::Linux));

    std::string error;
    CHECK(library.save(cache, error));  // parent directories are created
    CHECK(fs::exists(cache));
    CHECK(!fs::exists(fs::path(cache.string() + ".tmp")));
}

TEST("library: sizes render in human units") {
    Game game = make("Big", Platform::PS4);
    game.sizeBytes = 0;
    CHECK_EQ(game.displaySize(), std::string("unknown"));

    game.sizeBytes = 512;
    CHECK_EQ(game.displaySize(), std::string("512 B"));

    game.sizeBytes = 45ULL * 1024 * 1024 * 1024;
    CHECK_EQ(game.displaySize(), std::string("45 GB"));

    game.sizeBytes = 1536ULL * 1024 * 1024;  // 1.5 GB
    CHECK_EQ(game.displaySize(), std::string("1.5 GB"));
}

TEST("paths: slugify produces stable filesystem-safe ids") {
    CHECK_EQ(slugify("God of War"), std::string("god-of-war"));
    CHECK_EQ(slugify("Zelda: Tears of the Kingdom"), std::string("zelda-tears-of-the-kingdom"));
    CHECK_EQ(slugify("  Ratchet & Clank!  "), std::string("ratchet-clank"));
    CHECK_EQ(slugify("---"), std::string("untitled"));
    CHECK_EQ(slugify("FINAL FANTASY VII"), std::string("final-fantasy-vii"));
}
