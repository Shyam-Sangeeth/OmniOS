// Delivering a controller press as a key, for the full-screen pages that run
// on their own — the installer and the sign-in screen. The launcher has its
// own version, which also has to raise its window and move Hyprland back to
// its workspace.
#pragma once

#include <functional>

// Posts a press and a release of key to the window that has focus, or to the
// first visible window when none does. SDL reads the controller straight from
// the input devices, so a press arrives whether or not the compositor has given
// this process focus, and Qt drops a key sent to a window with no focused item.
//
// wantFocus, when given, is asked first to make sure something inside the
// window holds QML focus.
void deliverKey(int key, const std::function<void()>& wantFocus = {});
