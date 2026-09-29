// The keyboard as a console pad: one layout for every emulator OmniOS sets up,
// so a game plays the same from the keyboard whichever one runs it.
//
// It is laid out by position, as the pad is (EmulatorSetup.h): the four face
// buttons are Z (bottom), X (right), A (left) and S (top) — RetroArch's own
// default, which already had them there — so ✕ is Z on a PlayStation, and on
// Nintendo's systems, whose A is the right-hand button, A is X.
//
//   D-pad         arrow keys          L1 / R1   Q / W
//   face buttons  Z  X  A  S          L2 / R2   E / R
//   Start         Enter               L3 / R3   C / V
//   Select        Shift (the left)    left stick   I J K L (up left down right)
//                                     right stick  T F G H
//
// Each emulator names keys its own way; the functions below turn the layout
// into those names, so the settings they write and the overlay that shows the
// keys (keyboardControls) cannot disagree.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "GameLibrary.h"
#include "Router.h"

namespace omnios {

// A pad's controls, by position.
enum class PadButton {
    Up, Down, Left, Right,
    South, East, West, North,
    L1, R1, L2, R2, L3, R3,
    Start, Select,
    LeftUp, LeftDown, LeftLeft, LeftRight,
    RightUp, RightDown, RightLeft, RightRight,
};

// The keys the layout uses. Backspace is RPCS3's PS button alone.
enum class Key {
    Up, Down, Left, Right,
    Z, X, A, S, Q, W, E, R, C, V,
    Return, Shift, Backspace,
    I, J, K, L, T, F, G, H,
};

// The key that plays `button`.
Key keyFor(PadButton button);

// The key as each emulator writes it.
std::string qtKeyName(Key key);        // DuckStation, PCSX2, RPCS3: "Z", "Return", "Up"
int         qtKeyCode(Key key);        // Azahar: Qt::Key, 90 for Z
std::string x11KeyName(Key key);       // Dolphin (XInput2): "Z", "Return", "Shift_L"
std::string ryujinxKeyName(Key key);   // Ryubing: "Z", "Enter", "ShiftLeft"
std::string retroArchKeyName(Key key); // RetroArch: "z", "enter", "shift"

// How the key is shown to a person: "Z", "Enter", "↑".
std::string keyLabel(Key key);

// One entry of the controls bar: these keys play that. Several controls can
// share an entry, keys and names in the same order: keys Z X A S, button
// "✕ ○ □ △".
struct KeyHint {
    std::vector<std::string> keys;  // shown one keycap each
    std::string button;             // the console's name(s): "✕ ○ □ △", "D-pad"
};

// What the keyboard plays in `game` launched as `plan`, for the bar along the
// bottom of the game: the console's own button names, the stick clicks left
// off to keep it one line. Empty for a game whose emulator OmniOS does not
// set up for the keyboard (a PC game, a PS4 game in shadPS4, Steam).
std::vector<KeyHint> keyboardControls(const Game& game, const LaunchPlan& plan);

}  // namespace omnios
