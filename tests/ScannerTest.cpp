#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "Test.h"
#include "omnios/GameScanner.h"

using namespace omnios;
namespace fs = std::filesystem;

namespace {

// A throwaway ~/Games/ tree. Each test builds the exact layout it needs, so the
// scanner is exercised against real directory entries rather than a mock.
class GamesTree {
public:
    GamesTree() {
        static int counter = 0;
        root_ = fs::temp_directory_path() /
                ("omnios-games-" + std::to_string(++counter) + "-" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }

    ~GamesTree() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    const fs::path& root() const { return root_; }

    // Writes a file under <root>/<folder>/<name> with the given leading bytes,
    // padded out so offset-based signatures have room.
    fs::path file(const std::string& folder, const std::string& name,
                  const std::vector<std::uint8_t>& bytes = {}, std::size_t padTo = 0) {
        const fs::path dir = root_ / folder;
        std::error_code ec;
        fs::create_directories(dir, ec);

        const fs::path path = dir / name;
        std::ofstream out(path, std::ios::binary);
        for (const std::uint8_t byte : bytes) out.put(static_cast<char>(byte));
        for (std::size_t i = bytes.size(); i < padTo; ++i) out.put('\0');
        return path;
    }

    fs::path dir(const std::string& relative) {
        const fs::path path = root_ / relative;
        std::error_code ec;
        fs::create_directories(path, ec);
        return path;
    }

    void write(const fs::path& path, std::string_view text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }

private:
    fs::path root_;
};

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list) { return list; }

}  // namespace

TEST("scanner: finds titles across platform folders") {
    GamesTree tree;
    tree.file("ps4", "god-of-war.pkg", bytes({0x7F, 'C', 'N', 'T'}), 4096);
    tree.file("switch", "zelda-totk.nsp", bytes({'P', 'F', 'S', '0'}), 4096);
    tree.file("ps2", "shadow.iso", bytes({}), 64);  // no magic, folder decides

    GameLibrary library;
    const ScanReport report = GameScanner(tree.root()).scan(library);

    CHECK_EQ(report.added, 3);
    CHECK_EQ(library.size(), std::size_t(3));
    CHECK(library.byPlatform(Platform::PS4).size() == 1);
    CHECK(library.byPlatform(Platform::Switch).size() == 1);
    CHECK(library.byPlatform(Platform::PS2).size() == 1);
}

TEST("scanner: an installed .opkg uses its manifest instead of guessing") {
    GamesTree tree;
    const fs::path game = tree.dir("ps4/God of War");
    tree.write(game / "manifest.json", R"({
        "omni_version": "1.0",
        "id": "com.sony.godofwar",
        "title": "God of War",
        "platform": "ps4",
        "developer": "Santa Monica Studio",
        "executable": "game/eboot.bin",
        "compatibility": { "tier": "api_layer", "engine": "shadps4" },
        "tags": ["action"]
    })");
    tree.write(game / "cover.jpg", "not really a jpeg");
    std::error_code ec;
    fs::create_directories(game / "game", ec);
    tree.write(game / "game" / "eboot.bin", "payload");

    GameLibrary library;
    GameScanner(tree.root()).scan(library);

    const Game* found = library.find("com.sony.godofwar");
    CHECK(found != nullptr);
    if (found != nullptr) {
        CHECK_EQ(found->title, std::string("God of War"));
        CHECK_EQ(found->developer, std::string("Santa Monica Studio"));
        CHECK_EQ(found->executable, std::string("game/eboot.bin"));
        CHECK_EQ(found->engineOverride, std::string("shadps4"));
        CHECK(found->fromManifest);
        CHECK(!found->coverPath.empty());
        CHECK(found->sizeBytes > 0);
    }
}

TEST("scanner: the pc folder is split by magic, not by folder") {
    GamesTree tree;
    tree.file("pc", "hades.exe", bytes({'M', 'Z'}), 1024);
    tree.file("pc", "celeste", bytes({0x7F, 'E', 'L', 'F', 2, 1, 1, 0x00}), 1024);

    GameLibrary library;
    GameScanner(tree.root()).scan(library);

    CHECK_EQ(library.byPlatform(Platform::Windows).size(), std::size_t(1));
    CHECK_EQ(library.byPlatform(Platform::Linux).size(), std::size_t(1));
}

TEST("scanner: artwork, notes and key files are skipped without noise") {
    GamesTree tree;
    tree.file("switch", "zelda.nsp", bytes({'P', 'F', 'S', '0'}), 4096);
    tree.file("switch", "cover.jpg", bytes({0xFF, 0xD8}), 128);
    tree.file("switch", "README.txt");
    tree.file("switch", "prod.keys");
    tree.file("switch", ".hidden-thing");

    GameLibrary library;
    const ScanReport report = GameScanner(tree.root()).scan(library);

    CHECK_EQ(library.size(), std::size_t(1));
    CHECK_EQ(report.skipped, 4);
    CHECK(report.unidentified.empty());
}

TEST("scanner: an unrecognisable file is reported so the user can see why") {
    GamesTree tree;
    // A .dat in a folder with no hint of its own and no signature.
    tree.file("pc", "mystery.dat", bytes({0x01, 0x02, 0x03}), 512);

    GameLibrary library;
    const ScanReport report = GameScanner(tree.root()).scan(library);

    CHECK(library.empty());
    CHECK_EQ(report.unidentified.size(), std::size_t(1));
    CHECK(report.unidentified[0].find("mystery.dat") != std::string::npos);
}

TEST("scanner: a rescan removes titles whose files are gone") {
    GamesTree tree;
    const fs::path pkg = tree.file("ps4", "bloodborne.pkg", bytes({0x7F, 'C', 'N', 'T'}), 4096);
    tree.file("ps2", "ico.iso", bytes({}), 64);

    GameScanner scanner(tree.root());
    GameLibrary library;
    scanner.scan(library);
    CHECK_EQ(library.size(), std::size_t(2));

    std::error_code ec;
    fs::remove(pkg, ec);

    const ScanReport report = scanner.scan(library);
    CHECK_EQ(report.removed, 1);
    CHECK_EQ(report.updated, 1);
    CHECK_EQ(library.size(), std::size_t(1));
    CHECK(library.find("ps4.bloodborne") == nullptr);
}

TEST("scanner: a rescan keeps fetched art and a user-pinned engine") {
    GamesTree tree;
    tree.file("ps2", "ico.iso", bytes({}), 64);

    GameScanner scanner(tree.root());
    GameLibrary library;
    scanner.scan(library);

    const Game* first = library.find("ps2.ico");
    CHECK(first != nullptr);
    if (first == nullptr) return;

    // Simulate Phase 7.4 filling in art and the user pinning an engine.
    Game edited = *first;
    edited.coverPath      = "/home/omni/.omnios/library/ps2.ico/cover.jpg";
    edited.engineOverride = "retroarch";
    library.add(edited);

    scanner.scan(library);

    const Game* second = library.find("ps2.ico");
    CHECK(second != nullptr);
    if (second != nullptr) {
        CHECK_EQ(second->coverPath.generic_string(),
                 std::string("/home/omni/.omnios/library/ps2.ico/cover.jpg"));
        CHECK_EQ(second->engineOverride, std::string("retroarch"));
    }
}

TEST("scanner: a Windows game directory launches its executable") {
    GamesTree tree;
    const fs::path hades = tree.dir("pc/Hades");
    tree.write(hades / "Hades.exe", "MZ payload");
    tree.write(hades / "readme.txt", "notes");

    GameLibrary library;
    GameScanner(tree.root()).scan(library);

    const Game* found = library.find("windows.hades");
    // The directory has no magic of its own; the folder hint is ambiguous for
    // "pc", so this is correctly left unidentified rather than mis-filed.
    CHECK(found == nullptr);
    CHECK(library.empty());
}

TEST("scanner: an empty games tree scans cleanly") {
    GamesTree tree;
    GameLibrary library;
    const ScanReport report = GameScanner(tree.root()).scan(library);

    CHECK_EQ(report.added, 0);
    CHECK(library.empty());
    CHECK(report.warnings.empty());
}

TEST("scanner: a missing games root warns instead of failing") {
    GameLibrary library;
    const ScanReport report =
        GameScanner(fs::temp_directory_path() / "omnios-does-not-exist").scan(library);

    CHECK_EQ(report.warnings.size(), std::size_t(1));
    CHECK(library.empty());
}

TEST("scanner: filenames become readable titles") {
    CHECK_EQ(titleFromFilename("god-of-war.pkg"), std::string("God Of War"));
    CHECK_EQ(titleFromFilename("zelda_totk_v1.2.1 [USA].nsp"), std::string("Zelda Totk V1 2 1"));
    CHECK_EQ(titleFromFilename("GTA-San-Andreas.iso"), std::string("GTA San Andreas"));
    CHECK_EQ(titleFromFilename("Celeste"), std::string("Celeste"));
}

TEST("scanner: a PS3 or PS4 game folder is launched through its EBOOT") {
    GamesTree tree;
    tree.dir("ps3/Some Disc Game/PS3_GAME/USRDIR");
    tree.write(tree.root() / "ps3/Some Disc Game/PS3_GAME/USRDIR/EBOOT.BIN", "SCE");
    tree.dir("ps3/Some Download/USRDIR");
    tree.write(tree.root() / "ps3/Some Download/USRDIR/EBOOT.BIN", "SCE");
    tree.dir("ps3/Empty Folder");
    tree.dir("ps4/CUSA00001");
    tree.write(tree.root() / "ps4/CUSA00001/eboot.bin", "SCE");

    GameLibrary library;
    GameScanner(tree.root()).scan(library);

    int ps3 = 0, ps4 = 0;
    for (const Game& game : library.games()) {
        if (game.platform == Platform::PS3) {
            ++ps3;
            CHECK(game.executable == "PS3_GAME/USRDIR/EBOOT.BIN" || game.executable == "USRDIR/EBOOT.BIN");
        }
        if (game.platform == Platform::PS4) {
            ++ps4;
            CHECK_EQ(game.executable, std::string("eboot.bin"));
        }
    }
    CHECK_EQ(ps3, 2);  // not the empty folder
    CHECK_EQ(ps4, 1);
}

TEST("scanner: a PS1 or PS2 game folder is launched through its disc image") {
    GamesTree tree;
    tree.dir("ps1/Tetrade");
    tree.write(tree.root() / "ps1/Tetrade/TETRADE_PSX.bin", "tracks");
    tree.write(tree.root() / "ps1/Tetrade/TETRADE_PSX.cue", "FILE \"TETRADE_PSX.bin\" BINARY");
    tree.dir("ps1/Two Discs");
    tree.write(tree.root() / "ps1/Two Discs/Game (Disc 2).cue", "cue");
    tree.write(tree.root() / "ps1/Two Discs/Game (Disc 1).cue", "cue");
    tree.write(tree.root() / "ps1/Two Discs/Game.m3u", "Game (Disc 1).cue");
    tree.dir("ps2/Notes Only");
    tree.write(tree.root() / "ps2/Notes Only/readme.txt", "hi");

    GameLibrary library;
    GameScanner(tree.root()).scan(library);

    std::map<std::string, std::string> executables;
    for (const Game& game : library.games()) executables[game.title] = game.executable;
    CHECK_EQ(executables["Tetrade"], std::string("TETRADE_PSX.cue"));
    CHECK_EQ(executables["Two Discs"], std::string("Game.m3u"));
    CHECK(executables.find("Notes Only") == executables.end());
}
