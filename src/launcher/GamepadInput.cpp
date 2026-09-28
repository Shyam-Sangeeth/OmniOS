#include "GamepadInput.h"

#include <QDateTime>

#include <cstdlib>
#include <QDebug>
#include <Qt>

#ifdef OMNIOS_HAS_GAMEPAD
#include <SDL3/SDL.h>
#endif

namespace {

// How the buttons map. Console conventions, so that nobody has to learn
// anything: the bottom face button confirms, the right one goes back, the
// shoulders change tab.
//
// North is F5 rather than nothing, because a rescan is the one thing a person
// standing at the console with a USB stick actually wants, and it is harmless
// everywhere else.
constexpr int kPollIntervalMs = 16;

// A stick has to be pushed most of the way before it counts. Anything gentler
// and a worn thumbstick's resting drift walks the grid on its own.
constexpr int kAxisThreshold = 20000;   // of 32767
constexpr int kAxisRelease   = 12000;   // hysteresis, so it does not chatter

// Hold a direction and it repeats, at roughly the rate a keyboard does.
constexpr qint64 kRepeatDelayMs    = 400;
constexpr qint64 kRepeatIntervalMs = 120;

#ifdef OMNIOS_HAS_GAMEPAD
// Which family a pad belongs to, for naming its buttons on screen. The mapping
// itself does not change: presses are read by position (south, east, …), so a
// DualSense's ✕ is where an Xbox pad's A is and does the same thing.
QString kindOf(SDL_Gamepad* pad) {
    switch (SDL_GetGamepadType(pad)) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            return QStringLiteral("playstation");
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return QStringLiteral("xbox");
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return QStringLiteral("nintendo");
        default:
            return QStringLiteral("generic");
    }
}
#endif

}  // namespace

void GamepadInput::setKind(const QString& kind) {
    if (kind_ == kind) return;
    kind_ = kind;
    emit kindChanged();
}

GamepadInput::GamepadInput(QObject* parent) : QObject(parent) {
#ifdef OMNIOS_HAS_GAMEPAD
    // No signal handlers: SDL would install its own and take SIGINT away from
    // Qt, which turns Ctrl+C on the shell into something that does not stop it.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    // Keep delivering presses when the launcher is not the focused window.
    // Without this the Guide button would work everywhere except the place it
    // is needed — inside a running game.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        qWarning("omni-launcher: no gamepad support: %s", SDL_GetError());
        return;
    }
    available_ = true;

    connect(&timer_, &QTimer::timeout, this, &GamepadInput::poll);
    timer_.start(kPollIntervalMs);
    qWarning("omni-launcher: gamepad input ready");
#else
    qWarning("omni-launcher: built without SDL3; the shell is keyboard-only");
#endif
}

GamepadInput::~GamepadInput() {
#ifdef OMNIOS_HAS_GAMEPAD
    if (available_) SDL_Quit();
#endif
}

void GamepadInput::emitKey(int key) {
    if (key != 0) emit keyPressed(key);
}

void GamepadInput::handleDirection(int key, bool pressed) {
    if (pressed) {
        heldKey_    = key;
        heldSince_  = QDateTime::currentMSecsSinceEpoch();
        lastRepeat_ = heldSince_;
        emitKey(key);
    } else if (heldKey_ == key) {
        heldKey_ = 0;
    }
}

#ifndef OMNIOS_HAS_GAMEPAD

void GamepadInput::poll() {}

std::vector<omnios::Controller> GamepadInput::controllers() const { return {}; }

#else

std::vector<omnios::Controller> GamepadInput::controllers() const {
    std::vector<omnios::Controller> list;
    if (!available_) return list;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && ids != nullptr; ++i) {
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        char guid[33];
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), guid, sizeof(guid));
        omnios::Controller pad{name != nullptr ? name : "", guid};
        if (ids[i] == lastPad_) list.insert(list.begin(), std::move(pad));
        else list.push_back(std::move(pad));
    }
    SDL_free(ids);
    return list;
}

void GamepadInput::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_GAMEPAD_ADDED: {
                // Opening it is what starts the events flowing; a gamepad that
                // is merely detected sends nothing.
                SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which);
                // Logged because "the controller does nothing" has several
                // causes that look identical from the sofa: SDL not seeing the
                // device, seeing it as an unmapped joystick, or seeing it and
                // the keys going nowhere. This separates the first two from the
                // third.
                if (pad != nullptr) {
                    const char* name = SDL_GetGamepadName(pad);
                    const QString kind = kindOf(pad);
                    qWarning("omni-launcher: controller connected: %s (%s)",
                             name != nullptr ? name : "unnamed", qPrintable(kind));
                    setKind(kind);
                    // A DualSense or DualShock 4 lights up in OmniOS's colour, the
                    // way a PlayStation lights it in its own. Only through SDL's
                    // HID driver, which needs the hidraw access that
                    // 60-omnios-controllers.rules grants; over the kernel's
                    // evdev device alone SDL cannot reach the light bar, and
                    // this quietly does nothing.
                    if (kind == QLatin1String("playstation") &&
                        !SDL_SetGamepadLED(pad, 0x6C, 0x63, 0xFF))
                        qWarning("omni-launcher: light bar not reachable: %s", SDL_GetError());
                } else {
                    qWarning("omni-launcher: could not open controller: %s", SDL_GetError());
                }
                break;
            }

            case SDL_EVENT_GAMEPAD_REMOVED: {
                SDL_Gamepad* pad = SDL_GetGamepadFromID(event.gdevice.which);
                if (pad != nullptr) SDL_CloseGamepad(pad);
                // A controller unplugged mid-press would otherwise leave the
                // grid walking in that direction for ever.
                heldKey_ = 0;
                axisDirection_[0] = 0;
                axisDirection_[1] = 0;
                break;
            }

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const bool pressed = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                if (pressed) {
                    emit activity();
                    lastPad_ = event.gbutton.which;
                    // With two pads connected, the one being used names the
                    // buttons.
                    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(event.gbutton.which))
                        setKind(kindOf(pad));
                }

                // The one button that works while a game is running, and the
                // only way back to the library without a keyboard.
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
                    if (pressed) emit homeRequested();
                    break;
                }
                if (appRunning_) break;

                switch (event.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP:
                        handleDirection(Qt::Key_Up, pressed); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                        handleDirection(Qt::Key_Down, pressed); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                        handleDirection(Qt::Key_Left, pressed); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                        handleDirection(Qt::Key_Right, pressed); break;

                    // Face buttons, by position rather than by letter, so a
                    // PlayStation pad and an Xbox pad agree.
                    case SDL_GAMEPAD_BUTTON_SOUTH:
                        if (pressed) emitKey(Qt::Key_Return);
                        break;
                    case SDL_GAMEPAD_BUTTON_EAST:
                        if (pressed) emitKey(Qt::Key_Escape);
                        break;
                    case SDL_GAMEPAD_BUTTON_WEST:
                        if (pressed) emitKey(Qt::Key_M);
                        break;
                    case SDL_GAMEPAD_BUTTON_NORTH:
                        if (pressed) emitKey(Qt::Key_F5);
                        break;

                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                        if (pressed) emitKey(Qt::Key_Backtab);
                        break;
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                        if (pressed) emitKey(Qt::Key_Tab);
                        break;

                    case SDL_GAMEPAD_BUTTON_START:
                        if (pressed) emitKey(Qt::Key_F10);
                        break;
                    default:
                        break;
                }
                break;
            }

            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                if (std::abs(event.gaxis.value) > kAxisThreshold) emit activity();
                if (appRunning_) break;

                const bool horizontal = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
                const bool vertical   = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
                if (!horizontal && !vertical) break;

                const int index = horizontal ? 0 : 1;
                const int value = event.gaxis.value;

                // Hysteresis: cross the high threshold to engage, fall below
                // the low one to let go. One threshold makes a stick resting
                // near it fire over and over.
                int direction = axisDirection_[index];
                if (value > kAxisThreshold)       direction = 1;
                else if (value < -kAxisThreshold) direction = -1;
                else if (std::abs(value) < kAxisRelease) direction = 0;

                if (direction == axisDirection_[index]) break;
                axisDirection_[index] = direction;

                const int key = horizontal
                                    ? (direction > 0 ? Qt::Key_Right : Qt::Key_Left)
                                    : (direction > 0 ? Qt::Key_Down : Qt::Key_Up);
                handleDirection(key, direction != 0);
                break;
            }

            default:
                break;
        }
    }

    // Repeat whatever is being held, so holding a direction walks the grid.
    if (heldKey_ != 0 && !appRunning_) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - heldSince_ >= kRepeatDelayMs && now - lastRepeat_ >= kRepeatIntervalMs) {
            lastRepeat_ = now;
            emitKey(heldKey_);
        }
    }
}

#endif  // OMNIOS_HAS_GAMEPAD
