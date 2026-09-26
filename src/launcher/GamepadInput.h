// Driving the shell with a controller.
//
// Everything in the launcher already answers to the keyboard: the grids move on
// arrows, tiles open on Return, menus close on Escape, Tab switches tab, M
// opens a tile's menu, F10 the power menu. So a gamepad does not need its own
// copy of any of that — it needs to become those keys. Each press is turned
// into a real key event and posted to whatever has focus, which means one
// mapping table here and no second implementation of the navigation to drift
// out of step with the first.
//
// SDL3 rather than evdev, because SDL carries the mapping database that makes a
// DualSense and an Xbox pad behave the same, and handles a controller being
// plugged in after boot. Qt6 has no gamepad module at all — QtGamepad was
// dropped in the move from Qt5 — so there is nothing closer to hand.
//
// SDL reads the input devices directly rather than through the compositor, so
// this receives presses even when the launcher is not the focused window. That
// is deliberate and it is the whole point of the Guide button: it is how you
// get back to the library from inside a game. It also means everything else has
// to be ignored while a game is running, or the grid would be quietly moving
// underneath it.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class GamepadInput : public QObject {
    Q_OBJECT

public:
    explicit GamepadInput(QObject* parent = nullptr);
    ~GamepadInput() override;

    // False when the launcher was built without SDL3, or SDL would not start.
    // The shell has to keep working on the keyboard either way.
    bool available() const { return available_; }

    // While something is running, only the Guide button is honoured. Every
    // other press belongs to the game.
    void setAppRunning(bool running) { appRunning_ = running; }

    // The family of the pad last used: "playstation", "xbox", "nintendo" or
    // "generic". It decides what the buttons are called on screen — a
    // DualSense has no "A", it has ✕ in the same place.
    QString kind() const { return kind_; }

signals:
    // A button that maps onto a key the shell already understands.
    void keyPressed(int key);
    // The Guide button — the one press that means "back to the library",
    // wherever you are.
    void homeRequested();
    // Any press or deliberate stick movement, including while a game is
    // running. KDE's idle timer counts keyboards and mice but not gamepads —
    // the launcher reads the pad itself — so someone playing or browsing with
    // only a controller would have the screen dim, blank and the machine sleep
    // under them. This is what tells KDE otherwise.
    void activity();
    void kindChanged();

private:
    void poll();
    void emitKey(int key);
    void handleDirection(int key, bool pressed);

    void setKind(const QString& kind);

    bool available_  = false;
    bool appRunning_ = false;
    QString kind_ = QStringLiteral("generic");

    QTimer timer_;
    // The direction currently held, so it can repeat, and when it last fired.
    int   heldKey_    = 0;
    qint64 heldSince_ = 0;
    qint64 lastRepeat_ = 0;
    // Which way each analog axis is currently pushed, so a stick held over the
    // threshold produces one press and then repeats rather than a press every
    // time SDL reports a slightly different value.
    int axisDirection_[2] = {0, 0};
};
