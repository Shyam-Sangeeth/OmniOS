#include "Test.h"
#include "omnios/CoverArt.h"

using namespace omnios;

namespace {

Game gameAt(Platform platform, const char* path) {
    Game game;
    game.id = "test.game";
    game.platform = platform;
    game.path = path;
    return game;
}

}  // namespace

TEST("covers: each game is filed under its system") {
    CHECK_EQ(thumbnailSystem(gameAt(Platform::Retro, "/g/retro/a.nes")),
             std::string("Nintendo - Nintendo Entertainment System"));
    CHECK_EQ(thumbnailSystem(gameAt(Platform::Retro, "/g/retro/a.SFC")),
             std::string("Nintendo - Super Nintendo Entertainment System"));
    CHECK_EQ(thumbnailSystem(gameAt(Platform::Retro, "/g/retro/a.z64")), std::string("Nintendo - Nintendo 64"));
    CHECK_EQ(thumbnailSystem(gameAt(Platform::Retro, "/g/retro/a.gg")), std::string("Sega - Game Gear"));
    CHECK_EQ(thumbnailSystem(gameAt(Platform::PS1, "/g/ps1/a.cue")), std::string("Sony - PlayStation"));
    CHECK_EQ(thumbnailSystem(gameAt(Platform::GBA, "/g/gba/a.gba")), std::string("Nintendo - Game Boy Advance"));
    // None for what the collection has nothing of.
    CHECK(thumbnailSystem(gameAt(Platform::Switch, "/g/switch/a.nsp")).empty());
    CHECK(thumbnailSystem(gameAt(Platform::Windows, "/g/pc/a.exe")).empty());
    CHECK(thumbnailSystem(gameAt(Platform::Retro, "/g/retro/a.txt")).empty());
}

TEST("covers: the listing gives the names, decoded") {
    const std::string html =
        "<a href=\"?C=N;O=D\">Name</a><a href=\"/Nintendo%20-%20Nintendo%20Entertainment%20System/\">Parent</a>"
        "<a href=\"Nova%20the%20Squirrel%20(2019-04-30)(NovaSquirrel)%5bBuild%201405%5d.png\">x</a>"
        "<a href=\"Tom%20&amp;%20Jerry%20(USA).png\">y</a>"
        "<a href=\"Adventure%20Island%20%26%20Co%20(Japan).png\">z</a>";
    const std::vector<std::string> names = parseThumbnailIndex(html);
    CHECK_EQ(names.size(), std::size_t{3});
    CHECK_EQ(names.at(0), std::string("Nova the Squirrel (2019-04-30)(NovaSquirrel)[Build 1405]"));
    CHECK_EQ(names.at(1), std::string("Tom & Jerry (USA)"));
    CHECK_EQ(names.at(2), std::string("Adventure Island & Co (Japan)"));
}

TEST("covers: the same title, the best release of it, and nothing else") {
    const std::vector<std::string> names = {
        "Nova the Squirrel (2018-06-03)(NovaSquirrel)[Build 1074]",
        "Nova the Squirrel (2019-04-30)(NovaSquirrel)[Build 1405]",
        "Super Mario Bros. (World)",
        "Super Mario Bros. (Japan, USA) (Beta)",
        "Super Mario Bros. 3 (USA)",
        "Legend of Zelda, The (USA)",
        "Legend of Zelda, The (Europe)",
        "Tom & Jerry (USA)",
    };
    // Tags set aside; of several dated builds, the newest.
    CHECK_EQ(bestThumbnail("Nova the Squirrel", names),
             std::string("Nova the Squirrel (2019-04-30)(NovaSquirrel)[Build 1405]"));
    // A finished world release over a beta; and not the sequel.
    CHECK_EQ(bestThumbnail("super mario bros", names), std::string("Super Mario Bros. (World)"));
    CHECK_EQ(bestThumbnail("Super Mario Bros. 3", names), std::string("Super Mario Bros. 3 (USA)"));
    // "The" wherever it is put; the file's own region wins.
    CHECK_EQ(bestThumbnail("The Legend of Zelda (Europe)", names), std::string("Legend of Zelda, The (Europe)"));
    CHECK_EQ(bestThumbnail("Legend of Zelda, The", names), std::string("Legend of Zelda, The (USA)"));
    CHECK_EQ(bestThumbnail("Tom and Jerry", names), std::string("Tom & Jerry (USA)"));
    // A title not there gets nothing, not the nearest one.
    CHECK(bestThumbnail("Super Mario Bros. 2", names).empty());
    CHECK(bestThumbnail("", names).empty());
}

TEST("covers: where the listing and an image are") {
    CHECK_EQ(thumbnailIndexUrl("Nintendo - Game Boy"),
             std::string("https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Boxarts/"));
    CHECK_EQ(thumbnailUrl("Nintendo - Game Boy", "Tetris (World) (Rev 1)"),
             std::string("https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Boxarts/"
                         "Tetris%20%28World%29%20%28Rev%201%29.png"));
    CHECK_EQ(thumbnailLookupName(gameAt(Platform::Retro, "/g/retro/Nova the Squirrel.nes")),
             std::string("Nova the Squirrel"));
    CHECK(coverCacheFile(gameAt(Platform::Retro, "/g/a.nes")).filename() == "test.game.png");
}
