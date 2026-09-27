#include "Iptv.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace omnios {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// The value of key="value" in an #EXTINF line's attributes, or empty.
std::string attribute(std::string_view attributes, std::string_view key) {
    std::string needle(key);
    needle += "=\"";
    std::size_t at = 0;
    while ((at = attributes.find(needle, at)) != std::string_view::npos) {
        // A whole key, not the tail of a longer one ("tvg-id" inside
        // "xtvg-id").
        if (at == 0 || std::isspace(static_cast<unsigned char>(attributes[at - 1]))) {
            const std::size_t start = at + needle.size();
            const std::size_t end = attributes.find('"', start);
            if (end == std::string_view::npos) return {};
            return std::string(attributes.substr(start, end - start));
        }
        at += needle.size();
    }
    return {};
}

bool isWebUrl(std::string_view url) {
    return startsWith(url, "http://") || startsWith(url, "https://");
}

}  // namespace

std::vector<TvChannel> parseM3u(std::string_view text) {
    std::vector<TvChannel> channels;
    TvChannel pending;
    bool inEntry = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) continue;

        if (startsWith(line, "#EXTINF:")) {
            // A new entry; one left without a URL is dropped.
            pending = TvChannel{};
            inEntry = true;
            // Attributes come before the first comma that is outside quotes;
            // the name is everything after it (and may contain commas).
            bool quoted = false;
            std::size_t comma = std::string_view::npos;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '"') quoted = !quoted;
                else if (line[i] == ',' && !quoted) { comma = i; break; }
            }
            const std::string_view attributes = line.substr(0, comma);
            pending.id    = attribute(attributes, "tvg-id");
            pending.logo  = attribute(attributes, "tvg-logo");
            pending.group = attribute(attributes, "group-title");
            if (comma != std::string_view::npos)
                pending.name = std::string(trim(line.substr(comma + 1)));
        } else if (startsWith(line, "#EXTVLCOPT:")) {
            const std::string_view option = line.substr(11);
            if (startsWith(option, "http-user-agent="))
                pending.userAgent = std::string(option.substr(16));
            else if (startsWith(option, "http-referrer="))
                pending.referrer = std::string(option.substr(14));
        } else if (line.front() == '#') {
            continue;  // #EXTM3U and anything else a player may ignore
        } else if (inEntry) {
            inEntry = false;
            if (!isWebUrl(line)) continue;
            pending.url = std::string(line);
            if (pending.name.empty()) pending.name = pending.url;
            channels.push_back(std::move(pending));
            pending = TvChannel{};
        }
        if (end == text.size()) break;
    }
    return channels;
}

void TvCatalog::add(std::string_view groupedM3u, TvFacet facet) {
    // Streams already seen, by URL. Rebuilt per call rather than kept: three
    // calls in all, and a member map would be one more thing to keep in step.
    std::unordered_map<std::string, std::size_t> byUrl;
    byUrl.reserve(listings_.size());
    for (std::size_t i = 0; i < listings_.size(); ++i) byUrl.emplace(listings_[i].channel.url, i);

    for (TvChannel& channel : parseM3u(groupedM3u)) {
        std::string group = channel.group;
        auto [it, fresh] = byUrl.emplace(channel.url, listings_.size());
        if (fresh) {
            TvListing listing;
            listing.channel = std::move(channel);
            listing.channel.group.clear();  // it said which group, not what the channel is
            listings_.push_back(std::move(listing));
        }
        TvListing& listing = listings_[it->second];
        // Some entries give several at once: "Music;Religious".
        std::vector<std::string>& into = facet == TvFacet::Country    ? listing.countries
                                       : facet == TvFacet::Category   ? listing.categories
                                                                      : listing.languages;
        std::size_t start = 0;
        while (start <= group.size()) {
            const std::size_t end = std::min(group.find(';', start), group.size());
            std::string one(trim(std::string_view(group).substr(start, end - start)));
            if (!one.empty() && std::find(into.begin(), into.end(), one) == into.end())
                into.push_back(std::move(one));
            start = end + 1;
        }
    }
}

std::string pickHlsVariant(std::string_view playlist, int maxHeight) {
    struct Variant { long long bandwidth = 0; int height = 0; std::string uri; };
    std::vector<Variant> variants;
    bool pending = false;
    Variant next;

    std::size_t pos = 0;
    while (pos < playlist.size()) {
        std::size_t end = playlist.find('\n', pos);
        if (end == std::string_view::npos) end = playlist.size();
        const std::string_view line = trim(playlist.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) continue;

        if (startsWith(line, "#EXT-X-MEDIA:") && line.find("TYPE=AUDIO") != std::string_view::npos)
            return {};
        if (startsWith(line, "#EXT-X-STREAM-INF:")) {
            next = Variant{};
            pending = true;
            const auto number = [&line](std::string_view key) -> long long {
                std::size_t at = line.find(key);
                // A whole attribute: after the colon or a comma, not the end
                // of another ("AVERAGE-BANDWIDTH" holds "BANDWIDTH").
                while (at != std::string_view::npos && at > 0 && line[at - 1] != ':' && line[at - 1] != ',')
                    at = line.find(key, at + 1);
                if (at == std::string_view::npos) return 0;
                long long value = 0;
                for (std::size_t i = at + key.size(); i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])); ++i)
                    value = value * 10 + (line[i] - '0');
                return value;
            };
            next.bandwidth = number("BANDWIDTH=");
            const std::size_t res = line.find("RESOLUTION=");
            if (res != std::string_view::npos) {
                const std::size_t x = line.find('x', res);
                if (x != std::string_view::npos) {
                    int height = 0;
                    for (std::size_t i = x + 1; i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])); ++i)
                        height = height * 10 + (line[i] - '0');
                    next.height = height;
                }
            }
        } else if (line.front() != '#' && pending) {
            next.uri = std::string(line);
            variants.push_back(next);
            pending = false;
        }
    }
    if (variants.empty()) return {};

    const Variant* best = nullptr;
    for (const Variant& v : variants) {
        const bool fits = v.height == 0 || v.height <= maxHeight;
        if (!fits) continue;
        if (!best || v.height > best->height || (v.height == best->height && v.bandwidth > best->bandwidth))
            best = &v;
    }
    if (!best) {
        // All too tall: the smallest of them.
        for (const Variant& v : variants)
            if (!best || v.height < best->height || (v.height == best->height && v.bandwidth < best->bandwidth))
                best = &v;
    }
    return best->uri;
}

std::string iptvGroupedPlaylistUrl(TvFacet facet) {
    switch (facet) {
        case TvFacet::Country:  return "https://iptv-org.github.io/iptv/index.country.m3u";
        case TvFacet::Category: return "https://iptv-org.github.io/iptv/index.category.m3u";
        case TvFacet::Language: return "https://iptv-org.github.io/iptv/index.language.m3u";
    }
    return {};
}

std::string iptvPlaylistUrl(std::string_view kind, std::string_view code) {
    std::string lower;
    for (const char c : code) {
        const auto u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '_' && c != '-') return {};
        lower += static_cast<char>(std::tolower(u));
    }
    if (lower.empty()) return {};
    if (kind == "country")  return "https://iptv-org.github.io/iptv/countries/" + lower + ".m3u";
    if (kind == "category") return "https://iptv-org.github.io/iptv/categories/" + lower + ".m3u";
    return {};
}

}  // namespace omnios
