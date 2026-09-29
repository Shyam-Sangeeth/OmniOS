#include "KeyboardLayout.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

namespace omnios {

Key keyFor(PadButton button) {
    switch (button) {
        case PadButton::Up:         return Key::Up;
        case PadButton::Down:       return Key::Down;
        case PadButton::Left:       return Key::Left;
        case PadButton::Right:      return Key::Right;
        case PadButton::South:      return Key::Z;
        case PadButton::East:       return Key::X;
        case PadButton::West:       return Key::A;
        case PadButton::North:      return Key::S;
        case PadButton::L1:         return Key::Q;
        case PadButton::R1:         return Key::W;
        case PadButton::L2:         return Key::E;
        case PadButton::R2:         return Key::R;
        case PadButton::L3:         return Key::C;
        case PadButton::R3:         return Key::V;
        case PadButton::Start:      return Key::Return;
        case PadButton::Select:     return Key::Shift;
        case PadButton::LeftUp:     return Key::I;
        case PadButton::LeftDown:   return Key::K;
        case PadButton::LeftLeft:   return Key::J;
        case PadButton::LeftRight:  return Key::L;
        case PadButton::RightUp:    return Key::T;
        case PadButton::RightDown:  return Key::G;
        case PadButton::RightLeft:  return Key::F;
        case PadButton::RightRight: return Key::H;
    }
    return Key::Return;
}

namespace {

// The letter keys' letter; 0 for the others.
char letter(Key key) {
    switch (key) {
        case Key::Z: return 'Z';
        case Key::X: return 'X';
        case Key::A: return 'A';
        case Key::S: return 'S';
        case Key::Q: return 'Q';
        case Key::W: return 'W';
        case Key::E: return 'E';
        case Key::R: return 'R';
        case Key::C: return 'C';
        case Key::V: return 'V';
        case Key::I: return 'I';
        case Key::J: return 'J';
        case Key::K: return 'K';
        case Key::L: return 'L';
        case Key::T: return 'T';
        case Key::F: return 'F';
        case Key::G: return 'G';
        case Key::H: return 'H';
        default:     return 0;
    }
}

}  // namespace

std::string qtKeyName(Key key) {
    if (const char c = letter(key)) return std::string(1, c);
    switch (key) {
        case Key::Up:        return "Up";
        case Key::Down:      return "Down";
        case Key::Left:      return "Left";
        case Key::Right:     return "Right";
        case Key::Return:    return "Return";
        case Key::Shift:     return "Shift";
        case Key::Backspace: return "Backspace";
        default:             return {};
    }
}

int qtKeyCode(Key key) {
    if (const char c = letter(key)) return c;  // Qt::Key_A is 'A', and on
    switch (key) {
        case Key::Up:        return 0x01000013;
        case Key::Down:      return 0x01000015;
        case Key::Left:      return 0x01000012;
        case Key::Right:     return 0x01000014;
        case Key::Return:    return 0x01000004;
        case Key::Shift:     return 0x01000020;
        case Key::Backspace: return 0x01000003;
        default:             return 0;
    }
}

std::string x11KeyName(Key key) {
    // Dolphin names a key by its keysym, a letter in capitals.
    if (key == Key::Shift) return "Shift_L";
    if (key == Key::Backspace) return "BackSpace";
    return qtKeyName(key);
}

std::string ryujinxKeyName(Key key) {
    if (key == Key::Return) return "Enter";
    if (key == Key::Shift) return "ShiftLeft";
    if (key == Key::Backspace) return "BackSpace";
    return qtKeyName(key);
}

std::string retroArchKeyName(Key key) {
    if (key == Key::Return) return "enter";
    std::string name = qtKeyName(key);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

std::string keyLabel(Key key) {
    switch (key) {
        case Key::Up:     return "↑";
        case Key::Down:   return "↓";
        case Key::Left:   return "←";
        case Key::Right:  return "→";
        case Key::Return: return "Enter";
        default:          return qtKeyName(key);
    }
}


namespace {

using B = PadButton;

// An entry of the bar, from the pad controls whose keys it names.
KeyHint hint(std::initializer_list<PadButton> buttons, std::string button) {
    KeyHint h;
    for (const PadButton b : buttons) h.keys.push_back(keyLabel(keyFor(b)));
    h.button = std::move(button);
    return h;
}

// Several controls in one entry, keys and names in the same order —
// "Z X A S  ✕ ○ □ △" — so the bar stays one line.
KeyHint group(std::initializer_list<std::pair<PadButton, const char*>> controls) {
    KeyHint h;
    for (const auto& [position, name] : controls) {
        h.keys.push_back(keyLabel(keyFor(position)));
        if (!h.button.empty()) h.button += ' ';
        h.button += name;
    }
    return h;
}

// The ones every console has, and the sticks, in the order they are read:
// up, left, down, right, as the keys sit on the keyboard.
KeyHint dpad() { return hint({B::Up, B::Left, B::Down, B::Right}, "D-pad"); }
KeyHint leftStick(std::string name) { return hint({B::LeftUp, B::LeftLeft, B::LeftDown, B::LeftRight}, std::move(name)); }
KeyHint rightStick(std::string name) {
    return hint({B::RightUp, B::RightLeft, B::RightDown, B::RightRight}, std::move(name));
}
KeyHint startSelect(const char* start = "Start", const char* select = "Select") {
    return group({{B::Start, start}, {B::Select, select}});
}

// L3 and R3 (C and V) are left off the bar, as the stick clicks are rarely
// wanted: they are in the layout all the same.
std::vector<KeyHint> playStation(bool ps3) {
    std::vector<KeyHint> hints = {
        group({{B::South, "✕"}, {B::East, "○"}, {B::West, "□"}, {B::North, "△"}}),
        dpad(),
        group({{B::L1, "L1"}, {B::R1, "R1"}, {B::L2, "L2"}, {B::R2, "R2"}}),
        startSelect(),
        leftStick("L stick"), rightStick("R stick"),
    };
    if (ps3) hints.push_back({{keyLabel(Key::Backspace)}, "PS"});
    return hints;
}

// Nintendo's A is the right-hand button, so it is the key X.
KeyHint nintendoFaces(bool xy) {
    if (!xy) return group({{B::East, "A"}, {B::South, "B"}});
    return group({{B::East, "A"}, {B::South, "B"}, {B::North, "X"}, {B::West, "Y"}});
}

std::string extensionOf(const Game& game, const LaunchPlan& plan) {
    std::string name = plan.target.empty() ? game.path.filename().string()
                                           : std::filesystem::path(plan.target).filename().string();
    const std::size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::vector<KeyHint> retroArch(const Game& game, const LaunchPlan& plan) {
    const std::string& core = plan.core;
    if (core == "play") return playStation(false);
    if (core == "nestopia") return {nintendoFaces(false), dpad(), startSelect()};
    if (core == "snes9x" || core == "melonds")
        return {nintendoFaces(true), dpad(), group({{B::L1, "L"}, {B::R1, "R"}}), startSelect()};
    if (core == "mgba") {
        if (game.platform == Platform::GBA || extensionOf(game, plan) == "gba")
            return {nintendoFaces(false), dpad(), group({{B::L1, "L"}, {B::R1, "R"}}), startSelect()};
        return {nintendoFaces(false), dpad(), startSelect()};
    }
    if (core == "parallel_n64") {
        // ParaLLEl-N64's own mapping of RetroArch's pad: A is the bottom
        // button, B the left one, Z the left trigger, C on the right stick.
        return {group({{B::South, "A"}, {B::West, "B"}, {B::L2, "Z"}}), group({{B::L1, "L"}, {B::R1, "R"}}),
                hint({B::Start}, "Start"), dpad(), leftStick("Stick"), rightStick("C buttons")};
    }
    if (core == "genesis_plus_gx") {
        const std::string ext = extensionOf(game, plan);
        if (ext == "sms" || ext == "gg") return {group({{B::South, "1"}, {B::East, "2"}}), dpad(), hint({B::Start}, "Start")};
        // Genesis Plus GX: RetroArch's Y, B and A are the Mega Drive's A, B
        // and C; L, X and R its X, Y and Z.
        return {group({{B::West, "A"}, {B::South, "B"}, {B::East, "C"}}),
                group({{B::L1, "X"}, {B::North, "Y"}, {B::R1, "Z"}}), dpad(), startSelect("Start", "Mode")};
    }
    return {};
}

}  // namespace

std::vector<KeyHint> keyboardControls(const Game& game, const LaunchPlan& plan) {
    if (plan.engineId == "retroarch") return retroArch(game, plan);
    if (plan.engineId == "duckstation" || plan.engineId == "pcsx2") return playStation(false);
    if (plan.engineId == "rpcs3") return playStation(true);
    if (plan.engineId == "azahar") {
        return {nintendoFaces(true), dpad(), group({{B::L1, "L"}, {B::R1, "R"}, {B::L2, "ZL"}, {B::R2, "ZR"}}),
                startSelect(), leftStick("Circle Pad"), rightStick("C-Stick")};
    }
    if (plan.engineId == "ryubing") {
        return {nintendoFaces(true), dpad(), group({{B::L1, "L"}, {B::R1, "R"}, {B::L2, "ZL"}, {B::R2, "ZR"}}),
                startSelect("+", "−"), leftStick("L stick"), rightStick("R stick")};
    }
    if (plan.engineId == "dolphin") {
        if (game.platform == Platform::Wii) {
            // A Wii Remote held upright with a Nunchuk (EmulatorSetup.cpp).
            return {group({{B::South, "A"}, {B::R2, "B"}, {B::West, "1"}, {B::North, "2"}}),
                    startSelect("+", "−"), dpad(), leftStick("Nunchuk"),
                    group({{B::L1, "C"}, {B::L2, "Z"}}), rightStick("Pointer"), hint({B::East}, "Shake")};
        }
        return {group({{B::South, "A"}, {B::West, "B"}, {B::East, "X"}, {B::North, "Y"}}),
                group({{B::R1, "Z"}, {B::L2, "L"}, {B::R2, "R"}}), hint({B::Start}, "Start"), dpad(),
                leftStick("Stick"), rightStick("C-stick")};
    }
    return {};
}

}  // namespace omnios
