// Box art for emulator games, from libretro's thumbnail collection.
//
// thumbnails.libretro.com files box art per system under the names of the
// No-Intro and Redump sets — "Super Mario Bros. (World).png" — and publishes a
// plain directory listing for each system. So a game is matched by its file
// name against that listing, which the launcher downloads once per system and
// keeps (CoverFetcher), and only the one image it needs is fetched after that.
//
// A match has to be the same title: region, version and dump tags — the parts
// in brackets — are set aside, and among the files left the one the game's own
// tags point to wins, then a world or US release, never a beta or a demo when
// there is a finished one. A title that is not there gets no cover, rather
// than the nearest one: a wrong cover is worse than none.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "GameLibrary.h"

namespace omnios {

// The system a game's box art is filed under ("Nintendo - Game Boy Advance");
// empty for one the collection has none for (a PC game, Switch, Steam).
std::string thumbnailSystem(const Game& game);

// What a game is looked up by: its file's name without the extension, or its
// folder's name for a game that is a folder ("Tetrade" for Tetrade/*.cue).
std::string thumbnailLookupName(const Game& game);

// The box art names in a system's Named_Boxarts listing, as the server's HTML
// index gives them: decoded, without ".png".
std::vector<std::string> parseThumbnailIndex(std::string_view html);

// The name among `names` that is `lookup`'s box art; empty when none is.
std::string bestThumbnail(std::string_view lookup, const std::vector<std::string>& names);

// Where a system's listing and one of its images are.
std::string thumbnailIndexUrl(std::string_view system);
std::string thumbnailUrl(std::string_view system, std::string_view name);

// Where a fetched cover is kept: ~/.omnios/library/covers/<game id>.png.
std::filesystem::path coverCacheFile(const Game& game);

}  // namespace omnios
