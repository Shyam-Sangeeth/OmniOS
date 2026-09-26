// Which hand is on the machine: a controller, or a keyboard and mouse.
//
// Anything that asks for text shows the on-screen keyboard only for a
// controller — someone holding a keyboard wants to type on it, not steer a grid
// of keys — and names buttons the way the pad in hand does. This is that one
// piece of state, for the installer and the sign-in screen; the launcher keeps
// the same pair on its own controller.
#pragma once

#include <QObject>
#include <QVariantMap>

class GamepadInput;

// What a pad of the given family (GamepadInput::kind) calls its buttons, by
// position: {south, east, west, north, l1, r1, start, guide}. ✕ ○ □ △ on a
// PlayStation pad, A B X Y on an Xbox one.
QVariantMap buttonNamesFor(const QString& kind);

class InputMode : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool usingController READ usingController NOTIFY usingControllerChanged)
    Q_PROPERTY(QVariantMap buttonNames READ buttonNames NOTIFY buttonNamesChanged)

public:
    // Watches the whole application's input, and pad for presses and its kind.
    explicit InputMode(GamepadInput* pad, QObject* parent = nullptr);

    bool usingController() const { return usingController_; }
    QVariantMap buttonNames() const;

signals:
    void usingControllerChanged();
    void buttonNamesChanged();

protected:
    // A key without the controller's scan code, a click or a touch means a
    // keyboard or a pointer is what is being used now.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setUsingController(bool on);

    GamepadInput* pad_;
    bool usingController_ = false;
};
