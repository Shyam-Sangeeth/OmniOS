#include "Epg.h"

#include <algorithm>
#include <cctype>

namespace omnios {
namespace {

bool isAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// The value of name="..." in a tag's text, as written (entities and all), or
// empty.
std::string_view attribute(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        const std::size_t quote = at + name.size();
        // A whole attribute: after a space, and followed by ="
        if (at > 0 && std::isspace(static_cast<unsigned char>(tag[at - 1])) && quote + 1 < tag.size()
            && tag[quote] == '=' && tag[quote + 1] == '"') {
            const std::size_t end = tag.find('"', quote + 2);
            if (end == std::string_view::npos) return {};
            return tag.substr(quote + 2, end - quote - 2);
        }
        at = quote;
    }
    return {};
}

void appendUtf8(std::string& out, unsigned long code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x110000) {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// XML text with its entities read: "Tom &amp; Jerry" -> "Tom & Jerry". One it
// does not know is left as it is.
std::string decodeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out += text[i]; continue; }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) { out += '&'; continue; }
        const std::string_view name = text.substr(i + 1, semi - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            unsigned long code = 0;
            bool ok = name.size() > (hex ? 2u : 1u);
            for (std::size_t k = hex ? 2 : 1; k < name.size() && ok; ++k) {
                const char c = name[k];
                if (isDigit(c)) code = code * (hex ? 16 : 10) + static_cast<unsigned long>(c - '0');
                else if (hex && c >= 'a' && c <= 'f') code = code * 16 + static_cast<unsigned long>(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F') code = code * 16 + static_cast<unsigned long>(c - 'A' + 10);
                else ok = false;
                if (code > 0x10FFFF) ok = false;
            }
            if (!ok) { out += '&'; continue; }
            appendUtf8(out, code);
        } else {
            out += '&';
            continue;
        }
        i = semi;
    }
    return out;
}

// The text of the first <name ...>text</name> in `body`, or empty.
std::string_view element(std::string_view body, std::string_view name) {
    std::string open = "<";
    open += name;
    std::size_t at = 0;
    while ((at = body.find(open, at)) != std::string_view::npos) {
        const std::size_t after = at + open.size();
        // <title> or <title lang="en">, not <title-something>
        if (after < body.size() && (body[after] == '>' || std::isspace(static_cast<unsigned char>(body[after])))) {
            const std::size_t start = body.find('>', after);
            if (start == std::string_view::npos || body[start - 1] == '/') return {};
            std::string close = "</";
            close += name;
            const std::size_t end = body.find(close, start + 1);
            if (end == std::string_view::npos) return {};
            return body.substr(start + 1, end - start - 1);
        }
        at = after;
    }
    return {};
}

std::string trimmed(std::string text) {
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && space(text.back())) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && space(text[start])) ++start;
    return text.substr(start);
}

// Days from 1970-01-01 to the given date (proleptic Gregorian).
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}  // namespace

std::string guideKey(std::string_view name) {
    // Words, lowercased, with bracketed notes gone and "&" said.
    std::vector<std::string> words;
    std::string word;
    int depth = 0;
    const auto flush = [&]() {
        if (!word.empty()) words.push_back(std::move(word));
        word.clear();
    };
    for (const char c : name) {
        if (c == '(' || c == '[') { flush(); ++depth; continue; }
        if (c == ')' || c == ']') { flush(); depth = std::max(0, depth - 1); continue; }
        if (depth > 0) continue;
        if (c == '&') { flush(); words.emplace_back("and"); continue; }
        if (isAsciiAlnum(c)) word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (static_cast<unsigned char>(c) >= 0x80) word += c;  // a letter in another script
        else flush();
    }
    flush();

    std::string key;
    for (const std::string& w : words) {
        if (w == "hd" || w == "sd" || w == "uhd" || w == "fhd" || w == "tv" || w == "channel") continue;
        key += w;
    }
    return key;
}

std::string guideCountry(std::string_view tvgId) {
    const std::size_t at = tvgId.find('@');
    if (at != std::string_view::npos) tvgId = tvgId.substr(0, at);
    const std::size_t dot = tvgId.rfind('.');
    if (dot == std::string_view::npos) return {};
    std::string country;
    for (const char c : tvgId.substr(dot + 1)) {
        if (!isAsciiAlnum(c)) return {};
        country += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return country;
}

std::vector<std::string> epgFilesFor(std::string_view indexHtml, std::string_view country) {
    std::string prefix = "epg_ripper_";
    for (const char c : country) {
        if (!isAsciiAlnum(c)) return {};
        prefix += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (prefix.size() == 11) return {};

    std::vector<std::string> files;
    std::size_t at = 0;
    while ((at = indexHtml.find(prefix, at)) != std::string_view::npos) {
        std::size_t end = at + prefix.size();
        // Only a number after the country: "US2", not "US_LOCALS1" or "USA1".
        const std::size_t digits = end;
        while (end < indexHtml.size() && isDigit(indexHtml[end])) ++end;
        const std::string_view suffix = ".xml.gz";
        if (end > digits && indexHtml.substr(end, suffix.size()) == suffix) {
            std::string file(kEpgIndexUrl);
            file += indexHtml.substr(at, end + suffix.size() - at);
            if (std::find(files.begin(), files.end(), file) == files.end()) files.push_back(std::move(file));
        }
        at = end;
    }
    return files;
}

std::int64_t parseXmltvTime(std::string_view text) {
    // YYYYMMDDhhmm[ss], then optionally " +hhmm" or " -hhmm".
    std::size_t n = 0;
    while (n < text.size() && isDigit(text[n])) ++n;
    if (n != 12 && n != 14) return 0;
    const auto number = [&text](std::size_t at, std::size_t count) {
        std::int64_t value = 0;
        for (std::size_t i = at; i < at + count; ++i) value = value * 10 + (text[i] - '0');
        return value;
    };
    const std::int64_t year = number(0, 4);
    const auto month = static_cast<unsigned>(number(4, 2));
    const auto day = static_cast<unsigned>(number(6, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    std::int64_t seconds = daysFromCivil(year, month, day) * 86400 + number(8, 2) * 3600 + number(10, 2) * 60
                           + (n == 14 ? number(12, 2) : 0);

    std::size_t at = n;
    while (at < text.size() && text[at] == ' ') ++at;
    if (at + 5 <= text.size() && (text[at] == '+' || text[at] == '-')) {
        bool ok = true;
        for (std::size_t i = at + 1; i < at + 5; ++i) ok = ok && isDigit(text[i]);
        if (ok) {
            const std::int64_t offset = number(at + 1, 2) * 3600 + number(at + 3, 2) * 60;
            // 20:00 at +05:30 is 14:30 UTC.
            seconds += text[at] == '+' ? -offset : offset;
        }
    }
    return seconds;
}

std::unordered_map<std::string, Schedule> parseXmltv(std::string_view xml,
                                                     const std::unordered_set<std::string>& wanted,
                                                     std::int64_t from, std::int64_t to) {
    // The channels wanted, by the guide's id, with the keys each goes by.
    std::unordered_map<std::string, std::vector<std::string>> keysById;
    std::size_t at = 0;
    while ((at = xml.find("<channel ", at)) != std::string_view::npos) {
        const std::size_t tagEnd = xml.find('>', at);
        if (tagEnd == std::string_view::npos) break;
        const std::string_view id = attribute(xml.substr(at, tagEnd - at), "id");
        // <channel id="x"/> has no names to go by.
        if (xml[tagEnd - 1] == '/') { at = tagEnd; continue; }
        const std::size_t end = xml.find("</channel>", tagEnd);
        if (end == std::string_view::npos) break;
        std::string_view body = xml.substr(tagEnd + 1, end - tagEnd - 1);
        std::vector<std::string> keys;
        std::size_t name = 0;
        while ((name = body.find("<display-name", name)) != std::string_view::npos) {
            body = body.substr(name);
            const std::string key = guideKey(decodeText(element(body, "display-name")));
            if (!key.empty() && wanted.count(key) && std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
            name = 1;
        }
        if (!id.empty() && !keys.empty()) keysById[std::string(id)] = std::move(keys);
        at = end;
    }
    if (keysById.empty()) return {};

    // Their programmes, by id.
    std::unordered_map<std::string, Schedule> byId;
    std::string id;
    at = 0;
    while ((at = xml.find("<programme ", at)) != std::string_view::npos) {
        const std::size_t tagEnd = xml.find('>', at);
        if (tagEnd == std::string_view::npos) break;
        const std::string_view tag = xml.substr(at, tagEnd - at);
        const std::size_t end = xml.find("</programme>", tagEnd);
        if (end == std::string_view::npos) break;
        at = end;

        id.assign(attribute(tag, "channel"));
        if (!keysById.count(id)) continue;
        Programme programme;
        programme.start = parseXmltvTime(attribute(tag, "start"));
        programme.stop = parseXmltvTime(attribute(tag, "stop"));
        if (programme.start == 0 || programme.stop <= programme.start) continue;
        if (programme.stop <= from || programme.start >= to) continue;
        const std::string_view body = xml.substr(tagEnd + 1, end - tagEnd - 1);
        programme.title = trimmed(decodeText(element(body, "title")));
        if (programme.title.empty()) continue;
        programme.description = trimmed(decodeText(element(body, "desc")));
        byId[id].push_back(std::move(programme));
    }

    // Per key, the fullest of the channels that go by it: a guide can list
    // one channel twice, once without programmes.
    std::unordered_map<std::string, Schedule> byKey;
    std::unordered_map<std::string, std::size_t> best;
    for (auto& [channelId, schedule] : byId) {
        for (const std::string& key : keysById[channelId]) {
            if (best[key] >= schedule.size()) continue;
            best[key] = schedule.size();
            byKey[key] = schedule;
        }
    }
    for (auto& [key, schedule] : byKey) {
        std::sort(schedule.begin(), schedule.end(),
                  [](const Programme& a, const Programme& b) { return a.start < b.start; });
    }
    return byKey;
}

OnAir onAir(const Schedule& schedule, std::int64_t when) {
    OnAir result;
    // The first programme starting after `when`; the one before it may be on.
    const auto after = std::upper_bound(schedule.begin(), schedule.end(), when,
                                        [](std::int64_t t, const Programme& p) { return t < p.start; });
    if (after != schedule.begin()) {
        const Programme& before = *(after - 1);
        if (before.stop > when) result.now = &before;
    }
    if (after != schedule.end()) result.next = &*after;
    return result;
}

}  // namespace omnios
