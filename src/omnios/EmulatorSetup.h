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
// The keyboard is mapped too, in one layout shared by every emulator
// (KeyboardLayout.h): beside the pad where an emulator takes several bindings
// per button (DuckStation, PCSX2, Dolphin, RetroArch), and instead of it,
// when no pad is connected as the game starts, where it takes one (Azahar,
// RPCS3, Ryubing).
//
// Only ever over the emulator's own defaults: a setting the user changed in
// the emulator is left as they chose it.
#pragma once

#include <filesystem>
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
// the first of `controllers`. Player 1 not set up, or still on the keyboard,
// is replaced by an SDL pad; an SDL pad that is no longer connected is
// pointed at the one that is; any other handler is the user's and left
// alone. With no controllers, player 1 not set up or on SDL gets the
// keyboard's layout, and a keyboard already set up is kept.
std::string applyRpcs3Pad(std::string_view yml, const std::vector<Controller>& controllers);

// Ryubing's Config.json with player 1 on the first of `controllers`, on the
// same terms as RPCS3's above; with none, Ryubing's own keyboard default is
// replaced by the shared layout too. Returned unchanged if it does not parse.
std::string applyRyubingPad(std::string_view json, const std::vector<Controller>& controllers);

// Dolphin's GCPadNew.ini (section "GCPad1") or WiimoteNew.ini ("Wiimote1")
// with that player on the first of `controllers` and the keyboard both (each
// binding "`pad control` | `keyboard:key`"), or on the keyboard alone when no
// pad was ever set up. Moving off Dolphin's keyboard default sets every
// binding; otherwise a binding the user changed is kept. Another backend
// than SDL or the keyboard is the user's and left alone.
std::string applyDolphinPad(std::string_view ini, std::string_view section,
                            const std::vector<Controller>& controllers);

// Prepares engine `engineId`'s Flathub install to start a game straight away.
// Nothing to do, and true, for an engine it has no settings for. False when
// the settings file could not be written.
bool prepareEmulator(std::string_view engineId, const std::vector<Controller>& controllers = {});

// Whether Escape in engine `engineId`'s games should open OmniOS's game menu
// (Resume, Quit game), for want of a pause menu of the engine's own: Dolphin
// (Escape does nothing), Azahar (it leaves full screen, for a small window
// over the library) and Ryubing (it leaves full screen, then stops the game).
// RetroArch, DuckStation, PCSX2 and RPCS3 open their own on Escape.
bool escapeOpensGameMenu(std::string_view engineId);

// RetroArch's save state in slot 0 for game file `content`, as RetroArch
// names it: the file's name less its extension, then ".state". It is in
// savestate_directory (~/.config/retroarch/states unless the user's
// retroarch.cfg says otherwise), or in the folder there for the core
// (sort_savestates_enable, on by default), or beside the game
// (savestates_in_content_dir). The newest of those; empty when there is none.
std::filesystem::path retroarchStateFile(const std::filesystem::path& content);

}  // namespace omnios
