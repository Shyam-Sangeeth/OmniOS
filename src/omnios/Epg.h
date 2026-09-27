// What is on: TV guides (EPG) for OmniOS TV's channels.
//
// iptv-org lists channels but hosts no guides — its epg project is a scraper
// for anyone to run, and the community copies it lists have gone. The guides
// here come from epgshare01 (https://epgshare01.online/epgshare01/), which
// publishes XMLTV files per country, several days ahead, rebuilt daily.
//
// Its channel ids are its own ("Colors.HD.in", where iptv-org has
// "ColorsTV.in"), so channels are matched by name instead: guideKey() reduces
// both sides' names to the part that tells channels apart, and a channel whose
// key is in the guide gets that guide's programmes. Measured on India, that
// finds about three in five of iptv-org's channels; the rest are small local
// channels no guide covers.
//
// This is the part that needs no network: file names, names, and reading one.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omnios {

struct Programme {
    std::int64_t start = 0;  // seconds since the epoch, UTC
    std::int64_t stop = 0;
    std::string title;
    std::string description;  // may be empty
};

// A channel's programmes, by start.
using Schedule = std::vector<Programme>;

// What tells channels apart in a name: "Sony Yay (1080p)", "SONY YAY!" and
// "Sony YAY HD" are all "sonyyay". Lowercased; quality and notes in brackets
// dropped, and the words hd, sd, tv and channel; "&" said as "and"; anything
// else not a letter or digit dropped. Letters outside ASCII are kept as they
// are. Empty for a name with nothing left, which matches nothing.
std::string guideKey(std::string_view name);

// The country a channel is from, by its iptv-org id: "SonyYay.in@SD" -> "in".
// Empty when the id has none.
std::string guideCountry(std::string_view tvgId);

// Where epgshare01 lists its files.
inline constexpr std::string_view kEpgIndexUrl = "https://epgshare01.online/epgshare01/";

// The guide files for a country ("in"), from the index page: every
// "epg_ripper_IN<n>.xml.gz" linked there, as full URLs, in the order found.
std::vector<std::string> epgFilesFor(std::string_view indexHtml, std::string_view country);

// Reads an XMLTV guide. Keeps the programmes of every channel whose name's
// guideKey is in `wanted` and that overlap [from, to), keyed by that key. A
// channel listed but with no programmes is passed over, so that a second
// listing of the same channel with programmes is the one used.
std::unordered_map<std::string, Schedule> parseXmltv(std::string_view xml,
                                                     const std::unordered_set<std::string>& wanted,
                                                     std::int64_t from, std::int64_t to);

// XMLTV's time, "20260927203000 +0530", as seconds since the epoch; 0 when
// it cannot be read. With no offset, UTC.
std::int64_t parseXmltvTime(std::string_view text);

// The programme on at `when`, and the one after it; null where there is none.
struct OnAir {
    const Programme* now = nullptr;
    const Programme* next = nullptr;
};
OnAir onAir(const Schedule& schedule, std::int64_t when);

}  // namespace omnios
