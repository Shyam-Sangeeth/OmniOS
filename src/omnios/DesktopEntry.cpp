#include "DesktopEntry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>

#include "Router.h"

namespace omnios {
namespace {

namespace fs = std::filesystem;

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

bool isTrue(const std::string& value) {
    return value == "true" || value == "True" || value == "TRUE" || value == "1";
}

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

// Splits an Exec line the way the spec asks: quoted arguments stay whole, and
// the field codes are dropped rather than passed through as literal "%U".
//
// %i, %c and %k expand to things a launcher is supposed to supply; none of them
// matter for starting an app from a tile, so they go too.
std::vector<std::string> parseExec(const std::string& exec) {
    std::vector<std::string> argv;
    std::string current;
    bool inQuotes = false;
    bool have = false;

    for (std::size_t i = 0; i < exec.size(); ++i) {
        const char c = exec[i];
        if (inQuotes) {
            if (c == '\\' && i + 1 < exec.size()) {
                current += exec[++i];
            } else if (c == '"') {
                inQuotes = false;
            } else {
                current += c;
            }
            have = true;
            continue;
        }
        if (c == '"') {
            inQuotes = true;
            have = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (have) argv.push_back(current);
            current.clear();
            have = false;
        } else if (c == '%' && i + 1 < exec.size()) {
            // A lone "%%" is an escaped percent; everything else is a field
            // code and the whole token goes.
            if (exec[i + 1] == '%') {
                current += '%';
                have = true;
            }
            ++i;
        } else {
            current += c;
            have = true;
        }
    }
    if (have) argv.push_back(current);

    // A field code on its own leaves an empty argument behind, and Flatpak's
    // file-forwarding markers have to go with them.
    //
    // Every Flatpak exports an Exec line shaped like this:
    //
    //   /usr/bin/flatpak run ... --file-forwarding org.videolan.VLC @@u %U @@
    //
    // The @@u ... @@ pair brackets the arguments that are file paths, for a
    // launcher that has files to hand in. Dropping %U and keeping the brackets
    // leaves "flatpak run ... @@u @@", and flatpak takes those as the files it
    // was promised. That is why an app installed from the store appeared as a
    // tile, started, and immediately went away again.
    const auto isNoise = [](const std::string& arg) {
        return arg.empty() || arg == "@@" || arg == "@@u";
    };
    argv.erase(std::remove_if(argv.begin(), argv.end(), isNoise), argv.end());
    return argv;
}

}  // namespace

fs::path baselineAppsFile() {
    return fs::path(envOr("OMNIOS_BASELINE_APPS", "/usr/share/omnios/baseline-apps.txt"));
}

const std::vector<std::string>& baselineApps() {
    // Read once: this is consulted for every entry on every refresh, and the
    // file cannot change without the image changing.
    static const std::vector<std::string> kBaseline = [] {
        std::vector<std::string> ids;
        std::ifstream in(baselineAppsFile());
        if (!in) return ids;
        std::string line;
        while (std::getline(in, line)) {
            const std::string id = trim(line);
            if (!id.empty() && id.front() != '#') ids.push_back(id);
        }
        return ids;
    }();
    return kBaseline;
}

std::vector<fs::path> applicationDirs() {
    std::vector<fs::path> dirs;

    const std::string home = envOr("HOME", "");
    const std::string dataHome =
        envOr("XDG_DATA_HOME", home.empty() ? std::string() : home + "/.local/share");

    // Highest precedence first. The user's own directory wins, then Flatpak's
    // exports, then the system.
    if (!dataHome.empty()) {
        dirs.emplace_back(fs::path(dataHome) / "applications");
        dirs.emplace_back(fs::path(dataHome) / "flatpak/exports/share/applications");
    }
    dirs.emplace_back("/var/lib/flatpak/exports/share/applications");
    dirs.emplace_back("/usr/local/share/applications");
    dirs.emplace_back("/usr/share/applications");

    // XDG_DATA_DIRS can name others; append the ones not already covered.
    const std::string extra = envOr("XDG_DATA_DIRS", "");
    std::size_t start = 0;
    while (start <= extra.size() && !extra.empty()) {
        const std::size_t colon = extra.find(':', start);
        const std::string entry =
            trim(extra.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (!entry.empty()) {
            const fs::path candidate = fs::path(entry) / "applications";
            if (std::find(dirs.begin(), dirs.end(), candidate) == dirs.end())
                dirs.push_back(candidate);
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return dirs;
}

bool parseDesktopEntry(const fs::path& file, DesktopApp& out) {
    std::ifstream in(file);
    if (!in) return false;

    // Only the [Desktop Entry] group matters. Actions and other groups carry
    // their own Name and Exec, and reading those would produce a tile that
    // launches the wrong thing.
    bool inMainGroup = false;
    std::string name, comment, exec, tryExec, icon, type;
    bool noDisplay = false, hidden = false, terminal = false;

    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        if (trimmed.front() == '[') {
            inMainGroup = (trimmed == "[Desktop Entry]");
            continue;
        }
        if (!inMainGroup) continue;

        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(trimmed.substr(0, equals));
        const std::string value = trim(trimmed.substr(equals + 1));

        // Localised keys look like Name[de]. The unlocalised one is what this
        // wants, and taking a localised value would mix languages on the grid.
        if (key == "Name") name = value;
        else if (key == "Comment") comment = value;
        else if (key == "Exec") exec = value;
        else if (key == "TryExec") tryExec = value;
        else if (key == "Icon") icon = value;
        else if (key == "Type") type = value;
        else if (key == "NoDisplay") noDisplay = isTrue(value);
        else if (key == "Hidden") hidden = isTrue(value);
        else if (key == "Terminal") terminal = isTrue(value);
    }

    if (type != "Application") return false;
    if (noDisplay || hidden || terminal) return false;
    if (name.empty() || exec.empty()) return false;

    std::vector<std::string> argv = parseExec(exec);
    if (argv.empty()) return false;

    // TryExec is the spec's own "is this actually installed" check, and it is
    // the reason a package can ship an entry for a helper it did not install.
    if (!tryExec.empty() && !commandExists(tryExec) && !fs::exists(fs::path(tryExec))) return false;
    if (!commandExists(argv.front()) && !fs::exists(fs::path(argv.front()))) return false;

    out.id      = file.stem().string();
    out.name    = name;
    out.comment = comment;
    out.argv    = std::move(argv);
    out.icon    = icon;
    out.path    = file;
    out.flatpak = file.generic_string().find("/flatpak/exports/") != std::string::npos;
    return true;
}

std::vector<DesktopApp> installedApps() {
    std::vector<DesktopApp> apps;
    std::set<std::string> seen;

    const std::vector<std::string>& baseline = baselineApps();
    const auto shippedWithImage = [&baseline](const std::string& id) {
        return std::find(baseline.begin(), baseline.end(), id) != baseline.end();
    };

    std::error_code ec;
    for (const fs::path& dir : applicationDirs()) {
        if (!fs::is_directory(dir, ec)) continue;

        // Not recursive: nested directories mean a dotted desktop id, and
        // nothing that matters here ships one.
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".desktop") continue;

            const std::string id = entry.path().stem().string();
            // Claimed by an earlier directory, so XDG precedence is respected
            // even when the later copy would have parsed fine.
            if (!seen.insert(id).second) continue;

            DesktopApp app;
            if (!parseDesktopEntry(entry.path(), app)) continue;
            // A Flatpak is always shown: it cannot have shipped with the image,
            // so it is something someone chose to install.
            if (!app.flatpak && shippedWithImage(id)) continue;

            apps.push_back(std::move(app));
        }
    }

    std::sort(apps.begin(), apps.end(), [](const DesktopApp& a, const DesktopApp& b) {
        return a.name < b.name;
    });
    return apps;
}

}  // namespace omnios
