#include "Test.h"
#include "omnios/KeyboardLayout.h"

using namespace omnios;

namespace {

LaunchPlan planFor(const char* engine, const char* core = "") {
    LaunchPlan plan;
    plan.engineId = engine;
    plan.core = core;
    return plan;
}

Game gameOn(Platform platform, const char* file) {
    Game game;
    game.platform = platform;
    game.path = file;
    return game;
}

}  // namespace

TEST("keyboard: the face buttons sit by position, as on the pad") {
    CHECK(keyFor(PadButton::South) == Key::Z);
    CHECK(keyFor(PadButton::East) == Key::X);
    CHECK(keyFor(PadButton::West) == Key::A);
    CHECK(keyFor(PadButton::North) == Key::S);
    CHECK(keyFor(PadButton::Start) == Key::Return);
    CHECK(keyFor(PadButton::LeftUp) == Key::I);
    CHECK(keyFor(PadButton::RightLeft) == Key::F);
}

TEST("keyboard: each emulator's name for a key") {
    CHECK_EQ(qtKeyName(Key::Return), std::string("Return"));
    CHECK_EQ(qtKeyName(Key::Z), std::string("Z"));
    CHECK_EQ(qtKeyCode(Key::Z), 90);
    CHECK_EQ(qtKeyCode(Key::Up), 0x01000013);
    CHECK_EQ(x11KeyName(Key::Shift), std::string("Shift_L"));
    CHECK_EQ(ryujinxKeyName(Key::Return), std::string("Enter"));
    CHECK_EQ(ryujinxKeyName(Key::Shift), std::string("ShiftLeft"));
    CHECK_EQ(retroArchKeyName(Key::Return), std::string("enter"));
    CHECK_EQ(retroArchKeyName(Key::Up), std::string("up"));
    CHECK_EQ(retroArchKeyName(Key::Q), std::string("q"));
    CHECK_EQ(keyLabel(Key::Up), std::string("↑"));
}

TEST("keyboard: the bar names the console's own buttons") {
    // PlayStation: ✕ is the bottom button, so Z; the four in one entry, keys
    // and names in the same order.
    const std::vector<KeyHint> ps1 = keyboardControls(gameOn(Platform::PS1, "a.cue"), planFor("duckstation"));
    CHECK(!ps1.empty());
    CHECK_EQ(ps1.at(0).keys.size(), std::size_t{4});
    CHECK_EQ(ps1.at(0).keys.at(0), std::string("Z"));
    CHECK_EQ(ps1.at(0).keys.at(1), std::string("X"));
    CHECK_EQ(ps1.at(0).button, std::string("✕ ○ □ △"));

    // Nintendo's A is the right-hand button, so X.
    const std::vector<KeyHint> n3ds = keyboardControls(gameOn(Platform::N3DS, "a.3dsx"), planFor("azahar"));
    CHECK_EQ(n3ds.at(0).keys.at(0), std::string("X"));
    CHECK_EQ(n3ds.at(0).button, std::string("A B X Y"));

    // A GameCube's A is its big bottom button.
    const std::vector<KeyHint> gc = keyboardControls(gameOn(Platform::GameCube, "a.iso"), planFor("dolphin"));
    CHECK_EQ(gc.at(0).keys.at(0), std::string("Z"));
    CHECK_EQ(gc.at(0).button, std::string("A B X Y"));

    // RetroArch by core: the NES has two buttons, the N64 C buttons on the right stick.
    const std::vector<KeyHint> nes = keyboardControls(gameOn(Platform::Retro, "a.nes"), planFor("retroarch", "nestopia"));
    CHECK_EQ(nes.at(0).button, std::string("A B"));
    CHECK_EQ(nes.at(0).keys.at(0), std::string("X"));
    bool cButtons = false;
    for (const KeyHint& h : keyboardControls(gameOn(Platform::Retro, "a.z64"), planFor("retroarch", "parallel_n64")))
        cButtons = cButtons || (h.button == "C buttons" && h.keys.size() == 4 && h.keys.at(0) == "T");
    CHECK(cButtons);

    // The Game Boy has no shoulder buttons; the Game Boy Advance has.
    const auto hasShoulders = [](const std::vector<KeyHint>& hints) {
        for (const KeyHint& h : hints)
            if (h.button == "L R") return true;
        return false;
    };
    CHECK(!hasShoulders(keyboardControls(gameOn(Platform::Retro, "a.gb"), planFor("retroarch", "mgba"))));
    CHECK(hasShoulders(keyboardControls(gameOn(Platform::GBA, "a.gba"), planFor("retroarch", "mgba"))));

    // Nothing to show where OmniOS does not set the keyboard up.
    CHECK(keyboardControls(gameOn(Platform::Windows, "a.exe"), planFor("proton")).empty());
    CHECK(keyboardControls(gameOn(Platform::Steam, ""), planFor("steam")).empty());
}
