// TV channels from iptv-org (https://github.com/iptv-org/iptv).
//
// iptv-org keeps a list of free-to-air and publicly available streams — news
// channels, public broadcasters, the rest — and publishes it as M3U
// playlists: one per country, one per category. Nothing is hosted there, and
// nothing here either: a channel is a name, a logo and a stream URL that mpv
// plays. What is and is not available is iptv-org's list, and any one stream
// can be offline or blocked outside its country on a given day.
//
// This is the part that needs no network: where the playlists are, and
// reading one.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace omnios {

struct TvChannel {
    std::string name;       // "BBC News (720p)"
    std::string id;         // tvg-id, e.g. "BBCNews.uk@SD"; may be empty
    std::string logo;       // tvg-logo URL; may be empty
    std::string group;      // group-title: the category, e.g. "News"
    std::string url;        // the stream
    // Some streams answer only a particular player or page. The playlist says
    // so in #EXTVLCOPT lines, which are passed to mpv as they are.
    std::string userAgent;
    std::string referrer;
};

// A channel with everything it can be filtered by. iptv-org publishes the
// whole list three times over, grouped by country, by category and by
// language; each stream appears once under every group it belongs to, so
// merging the three by stream gives each channel all three at once.
struct TvListing {
    TvChannel channel;
    std::vector<std::string> countries;   // "India", "International"
    std::vector<std::string> categories;  // "News", "Movies"
    std::vector<std::string> languages;   // "Hindi", "English"
};

enum class TvFacet { Country, Category, Language };

// The merged list. add() one grouped playlist per facet, in any order; the
// same stream from different playlists becomes one listing.
class TvCatalog {
public:
    void add(std::string_view groupedM3u, TvFacet facet);
    const std::vector<TvListing>& listings() const { return listings_; }

private:
    std::vector<TvListing> listings_;
};

// Where iptv-org publishes the whole list grouped by a facet.
std::string iptvGroupedPlaylistUrl(TvFacet facet);

// Reads an extended M3U playlist: "#EXTINF:-1 key="value" ...,Name", then any
// #EXTVLCOPT lines, then the URL. Entries without a URL, or whose URL is not
// http(s), are left out — the player is only ever handed a web address.
std::vector<TvChannel> parseM3u(std::string_view text);

// The one stream to play from an HLS master playlist — the tallest picture no
// taller than `maxHeight` (the best-paid-for among equals), or the smallest if
// every one is taller — as its URI is written there, possibly relative.
//
// Why pick at all: handed a master playlist, FFmpeg opens every variant in it
// and downloads all of them at once, and the player then shows whichever it
// meets first — often the smallest. A channel offered in eight qualities cost
// eight downloads, lists every copy of its sound as a separate audio track,
// and on a slow machine never started at all.
//
// Empty when there is no choice to make: not a master playlist, no variants,
// or audio carried as separate renditions (#EXT-X-MEDIA TYPE=AUDIO), which only
// the master playlist ties to the picture.
std::string pickHlsVariant(std::string_view playlist, int maxHeight);

// Where iptv-org publishes a playlist. `kind` is "country" (an ISO 3166 code,
// "in") or "category" ("news"); the code is lowercased and must be letters,
// digits and "_" / "-" only, or the result is empty.
std::string iptvPlaylistUrl(std::string_view kind, std::string_view code);

// The lists of countries and categories, as iptv-org's API publishes them.
inline constexpr std::string_view kIptvCountriesUrl  = "https://iptv-org.github.io/api/countries.json";
inline constexpr std::string_view kIptvCategoriesUrl = "https://iptv-org.github.io/api/categories.json";

}  // namespace omnios
