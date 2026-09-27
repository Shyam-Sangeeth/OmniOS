#include "Test.h"
#include "omnios/Iptv.h"

using namespace omnios;

TEST("iptv: an entry gives its name, logo, category and stream") {
    // The shape iptv-org publishes.
    const auto channels = parseM3u(
        "#EXTM3U x-tvg-url=\"https://example.org/guide.xml\"\n"
        "#EXTINF:-1 tvg-id=\"DDNews.in@SD\" tvg-logo=\"https://i.imgur.com/dd.png\" "
        "group-title=\"News\",DD News (720p)\n"
        "https://example.org/ddnews/index.m3u8\n");
    CHECK_EQ(channels.size(), std::size_t{1});
    CHECK_EQ(channels.at(0).name, "DD News (720p)");
    CHECK_EQ(channels.at(0).id, "DDNews.in@SD");
    CHECK_EQ(channels.at(0).logo, "https://i.imgur.com/dd.png");
    CHECK_EQ(channels.at(0).group, "News");
    CHECK_EQ(channels.at(0).url, "https://example.org/ddnews/index.m3u8");
}

TEST("iptv: a stream's user agent and referrer come from its #EXTVLCOPT lines") {
    const auto channels = parseM3u(
        "#EXTINF:-1 tvg-id=\"A.us\" group-title=\"Sports\",Channel A\n"
        "#EXTVLCOPT:http-referrer=https://example.org/\n"
        "#EXTVLCOPT:http-user-agent=Mozilla/5.0 (X11; Linux)\n"
        "https://a.example.org/live.m3u8\n"
        "#EXTINF:-1,Channel B\n"
        "https://b.example.org/live.m3u8\n");
    CHECK_EQ(channels.size(), std::size_t{2});
    CHECK_EQ(channels.at(0).referrer, "https://example.org/");
    CHECK_EQ(channels.at(0).userAgent, "Mozilla/5.0 (X11; Linux)");
    // Options belong to the entry they follow, not the next one.
    CHECK(channels.at(1).referrer.empty());
    CHECK(channels.at(1).userAgent.empty());
    CHECK_EQ(channels.at(1).name, "Channel B");
}

TEST("iptv: a comma inside an attribute does not end the attributes") {
    const auto channels = parseM3u(
        "#EXTINF:-1 group-title=\"Kids,Animation\" tvg-logo=\"https://x/y.png\",Cartoons, Always\n"
        "http://cartoons.example.org/live\n");
    CHECK_EQ(channels.size(), std::size_t{1});
    CHECK_EQ(channels.at(0).group, "Kids,Animation");
    CHECK_EQ(channels.at(0).name, "Cartoons, Always");
}

TEST("iptv: only web streams are handed to the player") {
    // A file path or another scheme in a playlist is not something to open.
    const auto channels = parseM3u(
        "#EXTINF:-1,Local\n/etc/passwd\n"
        "#EXTINF:-1,Rtmp\nrtmp://example.org/live\n"
        "#EXTINF:-1,No url\n"
        "#EXTINF:-1,Good\r\nhttps://ok.example.org/a.m3u8\r\n");
    CHECK_EQ(channels.size(), std::size_t{1});
    CHECK_EQ(channels.at(0).name, "Good");
    CHECK_EQ(channels.at(0).url, "https://ok.example.org/a.m3u8");
}

TEST("iptv: playlist addresses are built only from plain codes") {
    CHECK_EQ(iptvPlaylistUrl("country", "IN"), "https://iptv-org.github.io/iptv/countries/in.m3u");
    CHECK_EQ(iptvPlaylistUrl("category", "news"), "https://iptv-org.github.io/iptv/categories/news.m3u");
    CHECK(iptvPlaylistUrl("country", "../index").empty());
    CHECK(iptvPlaylistUrl("country", "").empty());
    CHECK(iptvPlaylistUrl("language", "eng").empty());
}

TEST("iptv: the grouped playlists merge into one listing per stream") {
    // The same stream under two countries, two categories and one language;
    // another stream under one of each.
    TvCatalog catalog;
    catalog.add("#EXTINF:-1 tvg-id=\"A.in\" group-title=\"India\",Alpha News\nhttps://a.example/live.m3u8\n"
                "#EXTINF:-1 tvg-id=\"B.us\" group-title=\"United States\",Bravo\nhttps://b.example/live.m3u8\n"
                "#EXTINF:-1 tvg-id=\"A.in\" group-title=\"International\",Alpha News\nhttps://a.example/live.m3u8\n",
                TvFacet::Country);
    catalog.add("#EXTINF:-1 group-title=\"News\",Alpha News\nhttps://a.example/live.m3u8\n"
                "#EXTINF:-1 group-title=\"Music;Religious\",Bravo\nhttps://b.example/live.m3u8\n",
                TvFacet::Category);
    catalog.add("#EXTINF:-1 group-title=\"Hindi\",Alpha News\nhttps://a.example/live.m3u8\n",
                TvFacet::Language);

    const auto& all = catalog.listings();
    CHECK_EQ(all.size(), std::size_t{2});
    const TvListing& alpha = all.at(0);
    CHECK_EQ(alpha.channel.name, "Alpha News");
    CHECK_EQ(alpha.countries.size(), std::size_t{2});
    CHECK_EQ(alpha.countries.at(1), "International");
    CHECK_EQ(alpha.categories.size(), std::size_t{1});
    CHECK_EQ(alpha.languages.at(0), "Hindi");
    // A group naming several categories counts as each of them.
    const TvListing& bravo = all.at(1);
    CHECK_EQ(bravo.categories.size(), std::size_t{2});
    CHECK_EQ(bravo.categories.at(1), "Religious");
    CHECK(bravo.languages.empty());
    // The group said which list it was filed in, not what the channel is.
    CHECK(alpha.channel.group.empty());
}

TEST("iptv: from a master playlist, the tallest picture that fits is played") {
    // The shape of a real one: eight qualities, each with its own sound.
    const std::string master =
        "#EXTM3U\n#EXT-X-VERSION:3\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=208208,AVERAGE-BANDWIDTH=189200,RESOLUTION=256x144\nv144.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1799600,RESOLUTION=1280x720\nv720a.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2692800,RESOLUTION=1280x720\nv720b.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=4571600,RESOLUTION=1920x1080\nhttps://cdn.example/v1080.m3u8?token=x\n";
    CHECK_EQ(pickHlsVariant(master, 1080), "https://cdn.example/v1080.m3u8?token=x");
    // On a 720p screen, the better of the two 720p ones.
    CHECK_EQ(pickHlsVariant(master, 720), "v720b.m3u8");
    // Nothing small enough: the smallest there is.
    CHECK_EQ(pickHlsVariant(master, 100), "v144.m3u8");
}

TEST("iptv: a playlist that is not a choice of qualities is left alone") {
    // A media playlist (segments, not variants).
    CHECK(pickHlsVariant("#EXTM3U\n#EXTINF:6.0,\nseg1.ts\n#EXTINF:6.0,\nseg2.ts\n", 1080).empty());
    // Sound as separate renditions: only the master playlist ties it to the picture.
    CHECK(pickHlsVariant("#EXTM3U\n"
                         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"Hindi\",URI=\"hi.m3u8\"\n"
                         "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=1280x720,AUDIO=\"a\"\nv.m3u8\n", 1080).empty());
    CHECK(pickHlsVariant("", 1080).empty());
}
