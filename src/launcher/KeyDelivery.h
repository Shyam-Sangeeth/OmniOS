// Delivering a controller press as a key, for the full-screen pages that run
// on their own — the installer and the sign-in screen. The launcher has its
// own version, which also has to raise its window through KWin.
#pragma once

#include <QEvent>
#include <QtGlobal>

#include <functional>

class QKeyEvent;

// The native scan code every controller press is posted with. No keyboard
// produces it, so a key handler can tell a press on a controller from the same
// key on a keyboard — which is how the on-screen keyboard knows to appear for
// one and stay out of the way of the other. Theme.qml carries the same number
// for QML (controllerScanCode); keep the two equal.
constexpr quint32 kControllerScanCode = 0x6F53;

// A key event for a controller press: an ordinary key, marked with the scan
// code above.
QKeyEvent* controllerKeyEvent(QEvent::Type type, int key);

// Posts a press and a release of key to the window that has focus, or to the
// first visible window when none does. SDL reads the controller straight from
// the input devices, so a press arrives whether or not the compositor has given
// this process focus, and Qt drops a key sent to a window with no focused item.
//
// wantFocus, when given, is asked first to make sure something inside the
// window holds QML focus.
void deliverKey(int key, const std::function<void()>& wantFocus = {});
