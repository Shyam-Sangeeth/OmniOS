// Settings an emulator needs before OmniOS first starts a game in it.
//
// An emulator installed from Flathub starts, the first time, in a setup wizard
// — language, a BIOS picker, game folders, controller mapping — and on a
// console with only a controller that is where the game ends. So OmniOS fills
// in what the wizard would have asked, in the emulator's own settings file,
// before the first launch: the wizard marked done, the BIOS folder pointed at
// ~/Games/bios, a controller mapped.
//
// Every pad is mapped by position, the way the emulators' own defaults do it:
// the bottom face button is the bottom one on the console too, so a DualSense
// and an Xbox pad play the same, and on Nintendo's systems (whose A is on the
// right) A is the right-hand button whatever the pad prints on it.
//
// Only ever over the emulator's own defaults: a setting the user changed in
// the emulator is left as they chose it.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace omnios {

struct IniSetting {
    std::string section;
    std::string key;
    std::string value;
    // Set: the key's value becomes `value` when it is absent or one of
    // `replaces` (the emulator's own defaults). Add: a `key = value` line is
    // added unless that exact line is there — for keys that take several
    // values, as a button's bindings do.
    enum class Mode { Set, Add } mode = Mode::Set;
    std::vector<std::string> replaces;
};

// `ini` with `settings` applied. Sections and keys are matched exactly;
// anything the settings do not name is kept as it was, in order.
std::string applyIniSettings(std::string_view ini, const std::vector<IniSetting>& settings);

// A connected controller, as SDL describes it. RPCS3 binds a pad by its name
// and Ryubing by an id made from its GUID, so those two can only be set up
// for a pad that is plugged in; the launcher (which reads the pads with SDL
// itself) passes them, the one last used first.
struct Controller {
    std::string name;  // SDL's name for it, e.g. "Xbox 360 Controller"
    std::string guid;  // SDL's GUID as SDL_GUIDToString writes it: 32 hex digits
};

// Ryubing's id for the first pad with SDL GUID `guid` ("0-" and a .NET Guid
// string with SDL's CRC zeroed, as its SDL2GamepadDriver makes it). Empty for
// a GUID that is not 32 hex digits or is all zeros.
std::string ryubingGamepadId(std::string_view guid);

// RPCS3's input config (input_configs/global/Default.yml) with player 1 on
// the first of `controllers`. Player 1 still on RPCS3's default keyboard is
// replaced by an SDL pad; an SDL pad that is no longer connected is pointed
// at the one that is; any other handler is the user's and left alone.
std::string applyRpcs3Pad(std::string_view yml, const std::vector<Controller>& controllers);

// Ryubing's Config.json with player 1 on the first of `controllers`, on the
// same terms as RPCS3's above. Returned unchanged if it does not parse.
std::string applyRyubingPad(std::string_view json, const std::vector<Controller>& controllers);

// Dolphin's GCPadNew.ini (section "GCPad1") or WiimoteNew.ini ("Wiimote1")
// with that player on the first of `controllers`, on the same terms as
// RPCS3's above; Dolphin's default is the keyboard ("XInput2/...").
std::string applyDolphinPad(std::string_view ini, std::string_view section,
                            const std::vector<Controller>& controllers);

// Prepares engine `engineId`'s Flathub install to start a game straight away.
// Nothing to do, and true, for an engine it has no settings for. False when
// the settings file could not be written.
bool prepareEmulator(std::string_view engineId, const std::vector<Controller>& controllers = {});

}  // namespace omnios
