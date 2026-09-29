#include "CoverArt.h"

#include <algorithm>
#include <cctype>

#include "Paths.h"

namespace omnios {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string extensionOf(const fs::path& path) {
    std::string ext = lower(path.extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    return ext;
}

// The title alone: everything before the first bracketed tag, lowercase,
// letters and digits only, with "&" read as "and" and a trailing ", The"
// ("Legend of Zelda, The") and a leading "The" both dropped. So
// "Nova the Squirrel (2019-04-30)(NovaSquirrel)" and "Nova the Squirrel" are
// the same title, and "Super Mario Bros. 3" is not "Super Mario Bros.".
std::string titleKey(std::string_view name) {
    const std::size_t tag = name.find_first_of("([");
    std::string title = lower(name.substr(0, tag));
    for (std::size_t at; (at = title.find('&')) != std::string::npos;) title.replace(at, 1, " and ");
    for (const std::string_view article : {", the", ", a", ", an"}) {
        const std::size_t at = title.find(article);
        // At the end of the title, or before a subtitle: "Legend of Zelda, The - A Link to the Past".
        if (at != std::string::npos) {
            const std::size_t after = at + article.size();
            if (after == title.size() || title.compare(after, 2, " -") == 0 || title[after] == ' ')
                title.erase(at, article.size());
        }
    }
    std::string key;
    for (const unsigned char c : title)
        if (std::isalnum(c)) key += static_cast<char>(c);
    if (key.rfind("the", 0) == 0 && key.size() > 3) key.erase(0, 3);
    return key;
}

// The bracketed tags, lowercase: "(usa)(rev 1)[b]".
std::string tagsOf(std::string_view name) {
    const std::size_t tag = name.find_first_of("([");
    return tag == std::string_view::npos ? std::string() : lower(name.substr(tag));
}

// How well a box art name suits a game whose own name has tags `ownTags`.
int score(std::string_view name, std::string_view lookup, const std::string& ownTags) {
    const std::string tags = tagsOf(name);
    int s = 0;
    if (name == lookup) s += 100;  // the very file
    // The game's own region, when its file says one.
    for (const char* region : {"(usa", "(europe", "(japan", "(world", "(korea", "(brazil", "(australia"}) {
        if (ownTags.find(region) != std::string::npos && tags.find(region) != std::string::npos) s += 20;
    }
    if (tags.find("(world") != std::string::npos) s += 6;
    else if (tags.find("(usa") != std::string::npos) s += 5;
    else if (tags.find("(europe") != std::string::npos) s += 4;
    for (const char* unfinished : {"(beta", "(proto", "(demo", "(sample", "(kiosk", "(pirate", "(hack", "[b"}) {
        if (tags.find(unfinished) != std::string::npos && ownTags.find(unfinished) == std::string::npos) s -= 30;
    }
    return s;
}

// Percent-encoding for a path segment: all but the unreserved characters.
std::string encode(std::string_view text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else if (text.compare(i, 5, "&amp;") == 0) {
            out += '&';
            i += 4;
        } else {
            out += text[i];
        }
    }
    return out;
}

}  // namespace

std::string thumbnailSystem(const Game& game) {
    switch (game.platform) {
        case Platform::PS1:      return "Sony - PlayStation";
        case Platform::PS2:      return "Sony - PlayStation 2";
        case Platform::PS3:      return "Sony - PlayStation 3";
        case Platform::PS4:      return "Sony - PlayStation 4";
        case Platform::GameCube: return "Nintendo - GameCube";
        case Platform::Wii:      return "Nintendo - Wii";
        case Platform::N3DS:     return "Nintendo - Nintendo 3DS";
        case Platform::GBA:      return "Nintendo - Game Boy Advance";
        case Platform::Retro:    break;
        default:                 return {};
    }
    // The retro folder holds many systems; the file says which, as it does
    // for the emulator (Router.cpp).
    const std::string ext = extensionOf(game.path);
    if (ext == "nes") return "Nintendo - Nintendo Entertainment System";
    if (ext == "sfc" || ext == "smc") return "Nintendo - Super Nintendo Entertainment System";
    if (ext == "gb") return "Nintendo - Game Boy";
    if (ext == "gbc") return "Nintendo - Game Boy Color";
    if (ext == "gba") return "Nintendo - Game Boy Advance";
    if (ext == "nds") return "Nintendo - Nintendo DS";
    if (ext == "n64" || ext == "z64" || ext == "v64") return "Nintendo - Nintendo 64";
    if (ext == "md" || ext == "gen" || ext == "smd") return "Sega - Mega Drive - Genesis";
    if (ext == "sms") return "Sega - Master System - Mark III";
    if (ext == "gg") return "Sega - Game Gear";
    return {};
}

std::string thumbnailLookupName(const Game& game) {
    std::error_code ec;
    if (fs::is_directory(game.path, ec)) return game.path.filename().string();
    const std::string stem = game.path.stem().string();
    return stem.empty() ? game.title : stem;
}

std::vector<std::string> parseThumbnailIndex(std::string_view html) {
    std::vector<std::string> names;
    const std::string_view open = "href=\"";
    for (std::size_t at = html.find(open); at != std::string_view::npos; at = html.find(open, at)) {
        at += open.size();
        const std::size_t end = html.find('"', at);
        if (end == std::string_view::npos) break;
        const std::string_view href = html.substr(at, end - at);
        at = end;
        if (href.size() <= 4 || lower(href.substr(href.size() - 4)) != ".png" || href.find('/') != std::string_view::npos)
            continue;
        names.push_back(decode(href.substr(0, href.size() - 4)));
    }
    return names;
}

std::string bestThumbnail(std::string_view lookup, const std::vector<std::string>& names) {
    const std::string key = titleKey(lookup);
    if (key.empty()) return {};
    const std::string ownTags = tagsOf(lookup);
    const std::string* best = nullptr;
    int bestScore = 0;
    for (const std::string& name : names) {
        if (titleKey(name) != key) continue;
        const int s = score(name, lookup, ownTags);
        // Ties go to the later name: of several dated builds, the newest.
        if (best == nullptr || s > bestScore || (s == bestScore && name > *best)) {
            best = &name;
            bestScore = s;
        }
    }
    return best ? *best : std::string();
}

std::string thumbnailIndexUrl(std::string_view system) {
    return "https://thumbnails.libretro.com/" + encode(system) + "/Named_Boxarts/";
}

std::string thumbnailUrl(std::string_view system, std::string_view name) {
    return thumbnailIndexUrl(system) + encode(name) + ".png";
}

fs::path coverCacheFile(const Game& game) {
    return libraryDir() / "covers" / (game.id + ".png");
}

}  // namespace omnios
