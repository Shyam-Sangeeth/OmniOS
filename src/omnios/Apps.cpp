#include "Apps.h"

#include <algorithm>

#include "Paths.h"
#include "Router.h"

namespace omnios {
namespace {

// The app registry.
//
// mpv is started with a window forced open: launched bare from a tile it would
// otherwise exit immediately having been given no file, which on a console
// looks exactly like a crash.
//
// Thunar opens on the games directory rather than $HOME. On a console the
// reason to open a file manager is almost always to move a game or read a USB
// stick, and §18's install flow starts from exactly there.
const std::vector<App> kApps = {
    {"video", "Video Player", "mpv",
     "--player-operation-mode=pseudo-gui --force-window=yes --idle=yes",
     "mpv",
     "Play video and music from a drive or USB stick",
     "#6C63FF"},

    {"files", "Files", "thunar", "%GAMES%", "thunar",
     "Browse drives, copy games, open a USB stick",
     "#3A8FFF"},
};

}  // namespace

const std::vector<App>& allApps() { return kApps; }

const App* findApp(std::string_view id) {
    const auto it = std::find_if(kApps.begin(), kApps.end(),
                                 [id](const App& app) { return app.id == id; });
    return it == kApps.end() ? nullptr : &*it;
}

bool appAvailable(const App& app) { return commandExists(app.command); }

std::vector<std::string> appArgv(const App& app) {
    std::vector<std::string> argv{std::string(app.command)};

    const std::string args(app.args);
    std::size_t start = 0;
    while (start < args.size()) {
        const std::size_t end = args.find(' ', start);
        const std::string token =
            args.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty()) {
            argv.push_back(token == "%GAMES%" ? gamesDir().generic_string() : token);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return argv;
}

}  // namespace omnios
