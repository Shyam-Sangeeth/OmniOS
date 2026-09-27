#include "Test.h"
#include "omnios/Epg.h"

using namespace omnios;

TEST("epg: one channel's names from the playlist and the guide meet") {
    CHECK_EQ(guideKey("Sony Yay (1080p)"), "sonyyay");
    CHECK_EQ(guideKey("SONY YAY!"), "sonyyay");
    CHECK_EQ(guideKey("Colors HD"), guideKey("Colors (1080p) [Geo-blocked]"));
    CHECK_EQ(guideKey("&TV HD"), guideKey("And TV"));
    CHECK_EQ(guideKey("NDTV 24x7"), "ndtv24x7");
    // Different channels stay different.
    CHECK(guideKey("Sony Yay Hindi") != guideKey("Sony Yay"));
    CHECK(guideKey("Zee News") != guideKey("Zee Business"));
    // Nothing left: matches nothing.
    CHECK(guideKey("HD (720p)").empty());
}

TEST("epg: a channel's country is the end of its iptv-org id") {
    CHECK_EQ(guideCountry("SonyYay.in@SD"), "in");
    CHECK_EQ(guideCountry("BBCNews.uk"), "uk");
    CHECK(guideCountry("NoCountry").empty());
    CHECK(guideCountry("").empty());
}

TEST("epg: a country's guide files are found in the index, and only its own") {
    const std::string html =
        "<a href=\"epg_ripper_IN1.xml.gz\">epg_ripper_IN1.xml.gz</a>"
        "<a href=\"epg_ripper_IN4.xml.gz\">epg_ripper_IN4.xml.gz</a>"
        "<a href=\"epg_ripper_US2.xml.gz\">x</a>"
        "<a href=\"epg_ripper_US_LOCALS1.xml.gz\">x</a>"
        "<a href=\"epg_ripper_INDIA1.xml.gz\">x</a>";
    const auto india = epgFilesFor(html, "in");
    CHECK_EQ(india.size(), std::size_t{2});
    CHECK_EQ(india.at(0), "https://epgshare01.online/epgshare01/epg_ripper_IN1.xml.gz");
    CHECK_EQ(india.at(1), "https://epgshare01.online/epgshare01/epg_ripper_IN4.xml.gz");
    const auto us = epgFilesFor(html, "us");
    CHECK_EQ(us.size(), std::size_t{1});
    CHECK(epgFilesFor(html, "fr").empty());
    CHECK(epgFilesFor(html, "../").empty());
}

TEST("epg: XMLTV times are read with their offset") {
    // 20:00 in India is 14:30 UTC.
    CHECK_EQ(parseXmltvTime("20260927200000 +0530"), parseXmltvTime("20260927143000 +0000"));
    CHECK_EQ(parseXmltvTime("19700101000000 +0000"), std::int64_t{0} + 0);
    CHECK_EQ(parseXmltvTime("19700102000000"), std::int64_t{86400});
    CHECK_EQ(parseXmltvTime("197001020100 -0100"), std::int64_t{86400 + 7200});
    CHECK_EQ(parseXmltvTime("2026-09-27"), std::int64_t{0});
}

namespace {

const char kGuide[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="SONY.YAY!.in">
    <display-name lang="en">SONY YAY!</display-name>
  </channel>
  <channel id="Sony.yay.in">
    <display-name lang="en">Sony Yay</display-name>
  </channel>
  <channel id="Other.in"><display-name>Somebody Else</display-name></channel>
  <channel id="Empty.in"/>
  <programme start="20260927200000 +0530" stop="20260927203000 +0530" channel="Sony.yay.in">
    <title lang="en">Tom &amp; Jerry</title>
    <desc lang="en">A cat, a mouse.</desc>
  </programme>
  <programme start="20260927190000 +0530" stop="20260927200000 +0530" channel="Sony.yay.in">
    <title lang="en">Oggy</title>
  </programme>
  <programme start="20260927203000 +0530" stop="20260927210000 +0530" channel="Sony.yay.in">
    <title lang="en">Shin&#x2019;chan</title>
  </programme>
  <programme start="20260927200000 +0530" stop="20260927210000 +0530" channel="Other.in">
    <title>Not wanted</title>
  </programme>
</tv>
)";

}  // namespace

TEST("epg: a guide gives each wanted channel its programmes, in order") {
    const std::int64_t eight = parseXmltvTime("20260927200000 +0530");
    const auto guide = parseXmltv(kGuide, {guideKey("Sony Yay (1080p)")}, 0, eight * 2);
    CHECK_EQ(guide.size(), std::size_t{1});
    const Schedule& yay = guide.at("sonyyay");
    // From the listing that has programmes, not the one that has none.
    CHECK_EQ(yay.size(), std::size_t{3});
    CHECK_EQ(yay.at(0).title, "Oggy");
    CHECK_EQ(yay.at(1).title, "Tom & Jerry");
    CHECK_EQ(yay.at(1).description, "A cat, a mouse.");
    CHECK_EQ(yay.at(2).title, "Shin\xE2\x80\x99" "chan");
}

TEST("epg: only programmes in the window are kept") {
    const std::int64_t eight = parseXmltvTime("20260927200000 +0530");
    const auto guide = parseXmltv(kGuide, {"sonyyay"}, eight, eight + 1800);
    CHECK_EQ(guide.at("sonyyay").size(), std::size_t{1});
    CHECK_EQ(guide.at("sonyyay").at(0).title, "Tom & Jerry");
    CHECK(parseXmltv(kGuide, {"nobody"}, 0, eight * 2).empty());
}

TEST("epg: what is on now, and next") {
    const std::int64_t eight = parseXmltvTime("20260927200000 +0530");
    const Schedule yay = parseXmltv(kGuide, {"sonyyay"}, 0, eight * 2).at("sonyyay");
    OnAir air = onAir(yay, eight + 60);
    CHECK(air.now != nullptr && air.now->title == "Tom & Jerry");
    CHECK(air.next != nullptr && air.next->title == "Shin\xE2\x80\x99" "chan");
    // On the hour: the new programme, not the old.
    air = onAir(yay, eight);
    CHECK(air.now != nullptr && air.now->title == "Tom & Jerry");
    // Before the guide starts: nothing on, the first next.
    air = onAir(yay, eight - 7200);
    CHECK(air.now == nullptr);
    CHECK(air.next != nullptr && air.next->title == "Oggy");
    // After it ends: nothing.
    air = onAir(yay, eight + 7200);
    CHECK(air.now == nullptr && air.next == nullptr);
}
