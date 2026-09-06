#include "GameScanner.h"

#include "SteamLibrary.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "Detector.h"
#include "Manifest.h"
#include "Paths.h"

namespace omnios {
namespace {

namespace fs = std::filesystem;

// Files that live alongside games but are not games. Artwork, notes and the
// firmware/keys a user has to supply for some emulators.
const std::set<std::string> kIgnoredExtensions = {
    "jpg", "jpeg", "png", "webp", "gif", "bmp", "txt", "md", "nfo",
    "log", "json", "xml", "ini", "cfg", "sav", "srm", "part", "tmp",
    "crdownload", "keys", "bin_",
};

const std::set<std::string> kIgnoredNames = {
    "prod.keys", "title.keys", "bios", "firmware", "covers", "screenshots",
    "saves", "system",
};

bool isHidden(const fs::path& path) {
    const std::string name = path.filename().string();
    return !name.empty() && name.front() == '.';
}

std::string toLower(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

std::string readFile(const fs::path& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    ok = true;
    return buffer.str();
}

// Every ~/Games/ subfolder that exists, with the platform it implies. "pc" is
// shared by linux and windows, so it implies nothing and the bytes have to
// decide.
std::vector<std::pair<std::string, Platform>> folderHints() {
    std::map<std::string, Platform> hints;
    for (const PlatformInfo& info : allPlatforms()) {
        const std::string folder(info.folder);
        const auto it = hints.find(folder);
        if (it == hints.end()) {
            hints.emplace(folder, info.platform);
        } else if (it->second != info.platform) {
            it->second = Platform::Unknown;  // ambiguous folder
        }
    }
    return {hints.begin(), hints.end()};
}

}  // namespace

std::uint64_t pathSize(const fs::path& path) {
    std::error_code ec;

    if (fs::is_regular_file(path, ec)) {
        const auto size = fs::file_size(path, ec);
        return ec ? 0 : static_cast<std::uint64_t>(size);
    }
    if (!fs::is_directory(path, ec)) return 0;

    std::uint64_t total = 0;
    // skip_permission_denied keeps a single unreadable subdirectory from
    // aborting the size of an otherwise fine install.
    fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    if (ec) return 0;
    for (const fs::directory_entry& entry : it) {
        std::error_code entryEc;
        if (entry.is_regular_file(entryEc)) {
            const auto size = entry.file_size(entryEc);
            if (!entryEc) total += static_cast<std::uint64_t>(size);
        }
    }
    return total;
}

std::string titleFromFilename(std::string_view filename) {
    std::string stem(filename);

    // Drop the extension.
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos && dot != 0)
        stem = stem.substr(0, dot);

    // Drop bracketed region/version tags that dumps carry: "[USA]", "(v1.02)".
    std::string cleaned;
    int depth = 0;
    for (const char c : stem) {
        if (c == '[' || c == '(') { ++depth; continue; }
        if (c == ']' || c == ')') { if (depth > 0) --depth; continue; }
        if (depth == 0) cleaned.push_back(c);
    }

    // Separators to spaces, then collapse runs.
    std::string spaced;
    bool pendingSpace = false;
    for (const unsigned char c : cleaned) {
        if (c == '_' || c == '-' || c == '.' || std::isspace(c) != 0) {
            pendingSpace = !spaced.empty();
        } else {
            if (pendingSpace) spaced.push_back(' ');
            pendingSpace = false;
            spaced.push_back(static_cast<char>(c));
        }
    }

    if (spaced.empty()) return std::string(filename);

    // Title-case words that are entirely lowercase, leaving "GTA" and "FFVII"
    // alone — an all-caps word in a dump name is usually deliberate.
    std::string title;
    bool        wordStart = true;
    bool        wordIsLower = true;
    for (std::size_t i = 0; i < spaced.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(spaced[i]);
        if (wordStart) {
            std::size_t end = spaced.find(' ', i);
            if (end == std::string::npos) end = spaced.size();
            wordIsLower = std::all_of(spaced.begin() + static_cast<long>(i),
                                      spaced.begin() + static_cast<long>(end),
                                      [](unsigned char ch) {
                                          return std::isupper(ch) == 0;
                                      });
        }
        if (wordStart && wordIsLower) {
            title.push_back(static_cast<char>(std::toupper(c)));
        } else {
            title.push_back(static_cast<char>(c));
        }
        wordStart = (c == ' ');
    }
    return title;
}

GameScanner::GameScanner() : root_(gamesDir()) {}

GameScanner::GameScanner(fs::path gamesRoot) : root_(std::move(gamesRoot)) {}

bool GameScanner::identify(const fs::path& path, Platform folderHint, Game& game,
                           std::string& reason) const {
    reason.clear();

    const std::string name = path.filename().string();
    if (isHidden(path)) {
        reason = "hidden file";
        return false;
    }
    if (kIgnoredNames.count(toLower(name)) != 0) {
        reason = "support folder, not a game";
        return false;
    }

    std::error_code ec;
    const bool isDirectory = fs::is_directory(path, ec);

    if (!isDirectory && kIgnoredExtensions.count(fileExtension(name)) != 0) {
        reason = "not a game file";
        return false;
    }

    game = Game{};
    game.path = path;

    // An installed .opkg carries its own metadata; trust it over any guess.
    const fs::path manifestPath = path / "manifest.json";
    if (isDirectory && fs::exists(manifestPath, ec)) {
        bool              read = false;
        const std::string text = readFile(manifestPath, read);
        if (!read) {
            reason = "manifest.json could not be read";
            return false;
        }
        const ManifestResult parsed = parseManifest(text);
        if (!parsed.ok()) {
            reason = "manifest.json is invalid: " + parsed.errors.front();
            return false;
        }

        const Manifest& manifest = parsed.manifest;
        game.id           = manifest.id.empty()
                                ? makeGameId(manifest.platform, manifest.title)
                                : manifest.id;
        game.title        = manifest.title;
        game.platform     = manifest.platform;
        game.executable   = manifest.executable;
        game.version      = manifest.version;
        game.developer    = manifest.developer;
        game.publisher    = manifest.publisher;
        game.description  = manifest.description;
        game.tags         = manifest.tags;
        game.format       = "opkg";
        game.engineOverride   = manifest.compatibility.engine;
        game.detectionSource  = DetectionSource::Magic;
        game.fromManifest     = true;
        game.sizeBytes        = pathSize(path);

        // Bundled artwork sits next to the manifest.
        for (const char* art : {"cover.jpg", "cover.png", "icon.png"}) {
            if (fs::exists(path / art, ec)) {
                game.coverPath = path / art;
                break;
            }
        }
        return true;
    }

    const Detection detection = detectFile(path, folderHint);
    if (!detection.recognised()) {
        reason = detection.evidence;
        return false;
    }

    game.title           = titleFromFilename(name);
    game.platform        = detection.platform;
    game.format          = detection.format;
    game.detectionSource = detection.source;
    game.sizeBytes       = pathSize(path);
    game.id              = makeGameId(game.platform, game.title);

    // A directory-based native or Windows title needs an executable inside it;
    // without one the tile would launch a folder.
    if (isDirectory && (game.platform == Platform::Linux || game.platform == Platform::Windows)) {
        fs::directory_iterator it(path, ec);
        if (!ec) {
            for (const fs::directory_entry& entry : it) {
                const std::string extension = fileExtension(entry.path().filename().string());
                const bool windowsExe = extension == "exe";
                const bool linuxExe   = extension == "appimage" || extension == "sh";
                if ((game.platform == Platform::Windows && windowsExe) ||
                    (game.platform == Platform::Linux && linuxExe)) {
                    game.executable = entry.path().filename().string();
                    break;
                }
            }
        }
        if (game.executable.empty()) {
            reason = "directory has no launchable executable";
            return false;
        }
    }

    return true;
}

ScanReport GameScanner::scan(GameLibrary& library) const {
    ScanReport report;
    std::error_code ec;

    if (!fs::exists(root_, ec)) {
        report.warnings.push_back(root_.string() +
                                  " does not exist yet; nothing to scan");
        return report;
    }

    // Everything found this pass; anything cached but absent is uninstalled.
    std::set<std::string> seen;

    // Steam first, and not through identify(). Steam's folder holds its own
    // records rather than a pile of game files, and reading those is both more
    // accurate than guessing from directory names and the only way to get the
    // app id a launch needs.
    //
    // The default locations are read too, so a library that already existed
    // before OmniOS pointed Steam at ~/Games is not invisible.
    {
        std::vector<fs::path> steamRoots{root_ / "steam"};
        for (const fs::path& fallback : defaultSteamLibraries())
            steamRoots.push_back(fallback);

        for (const fs::path& steamapps : steamRoots) {
            for (Game& game : readSteamLibrary(steamapps)) {
                // A library reachable by two paths — ~/Games/steam being a
                // symlink to the real one is exactly how this is set up — must
                // not produce the game twice.
                if (seen.count(game.id) != 0) continue;

                if (const Game* cached = library.find(game.id); cached != nullptr) {
                    if (game.coverPath.empty()) game.coverPath = cached->coverPath;
                    if (game.engineOverride.empty()) game.engineOverride = cached->engineOverride;
                    ++report.updated;
                } else {
                    ++report.added;
                }
                seen.insert(game.id);
                library.add(std::move(game));
            }
        }
    }

    for (const auto& hint : folderHints()) {
        // Handled above, and its contents are Steam's business rather than a
        // tree of game files.
        if (hint.first == "steam") continue;

        const fs::path folder = root_ / hint.first;
        if (!fs::is_directory(folder, ec)) continue;

        fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            report.warnings.push_back("could not read " + folder.string() + ": " + ec.message());
            continue;
        }

        for (const fs::directory_entry& entry : it) {
            Game        game;
            std::string reason;
            if (!identify(entry.path(), hint.second, game, reason)) {
                ++report.skipped;
                // Only report things that plausibly were meant to be games;
                // artwork and readmes are noise.
                if (reason != "not a game file" && reason != "hidden file" &&
                    reason != "support folder, not a game") {
                    report.unidentified.push_back(entry.path().filename().string() +
                                                  " (" + reason + ")");
                }
                continue;
            }

            // Preserve what a scan cannot rederive: fetched cover art and any
            // engine the user pinned by hand.
            if (const Game* cached = library.find(game.id); cached != nullptr) {
                if (game.coverPath.empty()) game.coverPath = cached->coverPath;
                if (game.engineOverride.empty()) game.engineOverride = cached->engineOverride;
                ++report.updated;
            } else {
                ++report.added;
            }

            seen.insert(game.id);
            library.add(std::move(game));
        }
    }

    std::vector<std::string> stale;
    for (const Game& game : library.games()) {
        if (seen.count(game.id) == 0) stale.push_back(game.id);
    }
    for (const std::string& id : stale) {
        library.remove(id);
        ++report.removed;
    }

    library.sortByTitle();
    return report;
}

}  // namespace omnios
