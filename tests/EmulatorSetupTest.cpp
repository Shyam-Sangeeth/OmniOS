#include "Test.h"
#include "omnios/EmulatorSetup.h"
#include "omnios/Json.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace omnios;

TEST("emulator setup: a setting still at the emulator's default is changed") {
    const std::string ini = "[Main]\nSettingsVersion = 3\nSetupWizardIncomplete = true\n\n[BIOS]\nSearchDirectory = bios\n";
    const std::string out = applyIniSettings(ini, {
        {"Main", "SetupWizardIncomplete", "false", IniSetting::Mode::Set, {"true"}},
        {"BIOS", "SearchDirectory", "/home/me/Games/bios", IniSetting::Mode::Set, {"bios"}},
    });
    CHECK(out.find("SetupWizardIncomplete = false") != std::string::npos);
    CHECK(out.find("SearchDirectory = /home/me/Games/bios") != std::string::npos);
    CHECK(out.find("SettingsVersion = 3") != std::string::npos);
}

TEST("emulator setup: a setting the user chose is kept") {
    const std::string ini = "[BIOS]\nSearchDirectory = /mnt/my-bios\n";
    const std::string out = applyIniSettings(ini, {
        {"BIOS", "SearchDirectory", "/home/me/Games/bios", IniSetting::Mode::Set, {"bios"}},
    });
    CHECK_EQ(out, ini);
}

TEST("emulator setup: missing keys and sections are added") {
    const std::string out = applyIniSettings("[Main]\nSettingsVersion = 3\n", {
        {"Main", "ConfirmPowerOff", "false", IniSetting::Mode::Set, {"true"}},
        {"Pad1", "Up", "SDL-0/DPadUp", IniSetting::Mode::Add, {}},
    });
    CHECK_EQ(out, std::string("[Main]\nSettingsVersion = 3\nConfirmPowerOff = false\n\n[Pad1]\nUp = SDL-0/DPadUp\n"));
}

TEST("emulator setup: a binding is added beside the keyboard's, once") {
    const std::string ini = "[Pad1]\nType = AnalogController\nUp = Keyboard/Up\n\n[Pad2]\nType = None\n";
    const IniSetting pad{"Pad1", "Up", "SDL-0/DPadUp", IniSetting::Mode::Add, {}};
    const std::string once = applyIniSettings(ini, {pad});
    CHECK_EQ(once, std::string("[Pad1]\nType = AnalogController\nUp = Keyboard/Up\nUp = SDL-0/DPadUp\n\n[Pad2]\nType = None\n"));
    CHECK_EQ(applyIniSettings(once, {pad}), once);
}

TEST("emulator setup: DuckStation and PCSX2 name the face buttons their own way") {
    // Where each looks for its settings, under a scratch home.
    namespace fs = std::filesystem;
    const fs::path home = fs::temp_directory_path() / "omnios-emu-home";
    fs::remove_all(home);
    const char* old = std::getenv("HOME");
    const std::string oldHome = old ? old : "";
#ifdef _WIN32
    _putenv_s("HOME", home.string().c_str());
#else
    setenv("HOME", home.string().c_str(), 1);
#endif
    CHECK(prepareEmulator("duckstation"));
    CHECK(prepareEmulator("pcsx2"));
    const auto read = [](const fs::path& file) {
        std::ifstream in(file);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string duck = read(home / ".var/app/org.duckstation.DuckStation/config/duckstation/settings.ini");
    const std::string ps2 = read(home / ".var/app/net.pcsx2.PCSX2/config/PCSX2/inis/PCSX2.ini");
    CHECK(duck.find("SettingsVersion = 3") != std::string::npos);
    // DuckStation: SDL's letters, by position (it refuses "South").
    CHECK(duck.find("Cross = SDL-0/A") != std::string::npos);
    CHECK(duck.find("Circle = SDL-0/B") != std::string::npos);
    CHECK(duck.find("Square = SDL-0/X") != std::string::npos);
    CHECK(duck.find("Triangle = SDL-0/Y") != std::string::npos);
    CHECK(duck.find("SDL-0/South") == std::string::npos);
    CHECK(duck.find("SetupWizardIncomplete = false") != std::string::npos);
    // The keyboard beside the pad, in the shared layout, and Escape for the
    // pause menu.
    CHECK(duck.find("Cross = SDL-0/A\nCross = Keyboard/Z\n") != std::string::npos);
    CHECK(duck.find("Circle = Keyboard/X") != std::string::npos);
    CHECK(duck.find("Start = Keyboard/Return") != std::string::npos);
    CHECK(duck.find("LUp = Keyboard/I") != std::string::npos);
    CHECK(duck.find("OpenPauseMenu = Keyboard/Escape") != std::string::npos);
    CHECK(ps2.find("Cross = Keyboard/Z") != std::string::npos);
    CHECK(ps2.find("OpenPauseMenu = Keyboard/Escape") != std::string::npos);
    // PCSX2: "Face" and a direction (it refuses "A").
    CHECK(ps2.find("SettingsVersion = 1") != std::string::npos);
    CHECK(ps2.find("Cross = SDL-0/FaceSouth") != std::string::npos);
    CHECK(ps2.find("Triangle = SDL-0/FaceNorth") != std::string::npos);
    CHECK(ps2.find("SDL-0/A\n") == std::string::npos);
    CHECK(prepareEmulator("dolphin"));  // the keyboard, with no pad
#ifdef _WIN32
    _putenv_s("HOME", oldHome.c_str());
#else
    setenv("HOME", oldHome.c_str(), 1);
#endif
    std::error_code ec;
    fs::remove_all(home, ec);
}

namespace {

const Controller kXbox{"Xbox 360 Controller", "030003f05e0400008e02000014010000"};
const Controller kDualSense{"PS5 Controller", "0300b7e54c050000e60c000011810000"};

}  // namespace

TEST("emulator setup: Ryubing's id for a pad is SDL's GUID as .NET prints it, CRC zeroed") {
    CHECK_EQ(ryubingGamepadId(kXbox.guid), std::string("0-00000003-045e-0000-8e02-000014010000"));
    CHECK_EQ(ryubingGamepadId("030003F05E0400008E02000014010000"), ryubingGamepadId(kXbox.guid));
    CHECK(ryubingGamepadId("").empty());
    CHECK(ryubingGamepadId("00000000000000000000000000000000").empty());
    CHECK(ryubingGamepadId("zz0003f05e0400008e02000014010000").empty());
}

TEST("emulator setup: RPCS3's player 1 moves from the keyboard to the pad") {
    const std::string fresh = applyRpcs3Pad("Player 1 Input:\n", {kXbox});
    CHECK(fresh.find("  Handler: SDL\n  Device: \"Xbox 360 Controller 1\"\n  Config:\n") != std::string::npos);
    CHECK(fresh.find("    Cross: South\n") != std::string::npos);
    CHECK(fresh.find("    PS Button: Start&Back\n") != std::string::npos);

    const std::string keyboard =
        "Player 1 Input:\n  Handler: Keyboard\n  Device: Keyboard\n  Config:\n    Cross: K\n"
        "Player 2 Input:\n  Handler: Evdev\n  Device: My Pad\n";
    const std::string out = applyRpcs3Pad(keyboard, {kXbox});
    CHECK(out.find("Handler: Keyboard") == std::string::npos);
    CHECK(out.find("Cross: K") == std::string::npos);
    CHECK(out.find("Player 2 Input:\n  Handler: Evdev\n  Device: My Pad\n") != std::string::npos);
    CHECK_EQ(applyRpcs3Pad(out, {kXbox}), out);  // once
    CHECK_EQ(applyRpcs3Pad(keyboard, {}), keyboard);  // a keyboard set up already is kept
}

TEST("emulator setup: RPCS3's player 1 is the keyboard's layout with no pad") {
    const std::string fresh = applyRpcs3Pad("Player 1 Input:\n", {});
    CHECK(fresh.find("  Handler: Keyboard\n  Device: Keyboard\n  Config:\n") != std::string::npos);
    CHECK(fresh.find("    Cross: Z\n") != std::string::npos);
    CHECK(fresh.find("    Circle: X\n") != std::string::npos);
    CHECK(fresh.find("    Start: Return\n") != std::string::npos);
    CHECK(fresh.find("    PS Button: Backspace\n") != std::string::npos);
    CHECK_EQ(applyRpcs3Pad(fresh, {}), fresh);  // once

    // The pad it was on is gone: the keyboard, until a pad is back.
    const std::string pad = applyRpcs3Pad("Player 1 Input:\n", {kXbox});
    const std::string unplugged = applyRpcs3Pad(pad, {});
    CHECK(unplugged.find("Handler: Keyboard") != std::string::npos);
    CHECK(applyRpcs3Pad(unplugged, {kXbox}).find("Handler: SDL") != std::string::npos);
}

TEST("emulator setup: RPCS3 follows a new pad, and keeps a handler the user chose") {
    const std::string mine = "Player 1 Input:\n  Handler: SDL\n  Device: Xbox 360 Controller 1\n  Config:\n    Cross: East\n";
    const std::string swapped = applyRpcs3Pad(mine, {kDualSense});
    CHECK(swapped.find("  Device: \"PS5 Controller 1\"\n") != std::string::npos);
    CHECK(swapped.find("Cross: East") != std::string::npos);  // the user's buttons stay
    CHECK_EQ(applyRpcs3Pad(mine, {kDualSense, kXbox}), mine);  // still connected

    const std::string dualsense = "Player 1 Input:\n  Handler: DualSense\n  Device: DualSense Pad #1\n";
    CHECK_EQ(applyRpcs3Pad(dualsense, {kXbox}), dualsense);
}

TEST("emulator setup: Ryubing's player 1 moves from the keyboard to the pad") {
    const std::string config = R"({"version": 70, "docked_mode": true, "input_config": [
        {"backend": "WindowKeyboard", "id": "0", "name": "Keyboard", "player_index": "Player1",
         "right_joycon": {"button_a": "Z"}}]})";
    std::string error;
    const std::string out = applyRyubingPad(config, {kXbox});
    const Json parsed = Json::parse(out, error);
    CHECK(error.empty());
    CHECK_EQ(parsed["version"].asNumber(), 70.0);
    CHECK(parsed["docked_mode"].asBool());
    const Json& player = parsed["input_config"].items().at(0);
    CHECK_EQ(player["backend"].asString(), std::string("GamepadSDL2"));
    CHECK_EQ(player["id"].asString(), std::string("0-00000003-045e-0000-8e02-000014010000"));
    CHECK_EQ(player["player_index"].asString(), std::string("Player1"));
    CHECK_EQ(player["right_joycon"]["button_a"].asString(), std::string("B"));  // by position
    CHECK_EQ(player["left_joycon_stick"]["joystick"].asString(), std::string("Left"));
    CHECK_EQ(applyRyubingPad(out, {kXbox}), out);  // once
    CHECK_EQ(applyRyubingPad("not json", {kXbox}), std::string("not json"));
}

TEST("emulator setup: Ryubing follows a new pad and keeps the user's buttons") {
    const std::string config = R"({"input_config": [{"backend": "GamepadSDL2", "id": "0-00000003-045e-0000-8e02-000014010000",
        "name": "Xbox 360 Controller", "player_index": "Player1", "right_joycon": {"button_a": "A"}}]})";
    std::string error;
    const Json swapped = Json::parse(applyRyubingPad(config, {kDualSense}), error);
    const Json& player = swapped["input_config"].items().at(0);
    CHECK_EQ(player["id"].asString(), ryubingGamepadId(kDualSense.guid));
    CHECK_EQ(player["name"].asString(), std::string("PS5 Controller"));
    CHECK_EQ(player["right_joycon"]["button_a"].asString(), std::string("A"));
    CHECK_EQ(applyRyubingPad(config, {kDualSense, kXbox}), config);
}

TEST("emulator setup: Ryubing's player 1 is the keyboard's layout with no pad") {
    const auto player1 = [](const std::string& json) {
        std::string error;
        const Json parsed = Json::parse(json, error);
        return parsed["input_config"].items().at(0);
    };
    // Its own keyboard default (A on Z) and a pad no longer there both
    // become the shared layout: A, the right-hand button, on X.
    const std::string ryubings = R"({"input_config": [{"backend": "WindowKeyboard", "id": "0",
        "player_index": "Player1", "right_joycon": {"button_a": "Z", "button_b": "X"}}]})";
    const Json fromDefault = player1(applyRyubingPad(ryubings, {}));
    CHECK_EQ(fromDefault["backend"].asString(), std::string("WindowKeyboard"));
    CHECK_EQ(fromDefault["right_joycon"]["button_a"].asString(), std::string("X"));
    CHECK_EQ(fromDefault["right_joycon"]["button_b"].asString(), std::string("Z"));
    CHECK_EQ(fromDefault["right_joycon"]["button_plus"].asString(), std::string("Enter"));
    CHECK_EQ(fromDefault["left_joycon"]["button_minus"].asString(), std::string("ShiftLeft"));
    CHECK_EQ(fromDefault["left_joycon_stick"]["stick_up"].asString(), std::string("I"));

    const std::string pad = applyRyubingPad(ryubings, {kXbox});
    CHECK_EQ(player1(applyRyubingPad(pad, {}))["backend"].asString(), std::string("WindowKeyboard"));

    // A keyboard the user laid out their own way is theirs.
    const std::string theirs = R"({"input_config": [{"backend": "WindowKeyboard", "id": "0",
        "player_index": "Player1", "right_joycon": {"button_a": "K"}}]})";
    CHECK_EQ(applyRyubingPad(theirs, {}), theirs);
}

TEST("emulator setup: Azahar takes any controller, over its keyboard defaults only") {
    const std::string ini =
        "[Controls]\nprofiles\\1\\button_a=\"code:65,engine:keyboard\"\nprofiles\\1\\button_a\\default=true\n"
        "profiles\\1\\button_b=\"code:75,engine:keyboard\"\nprofiles\\1\\button_b\\default=false\nprofiles\\size=1\n";
    namespace fs = std::filesystem;
    const fs::path home = fs::temp_directory_path() / "omnios-azahar-home";
    const fs::path file = home / ".var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini";
    fs::remove_all(home);
    fs::create_directories(file.parent_path());
    std::ofstream(file) << ini;
    const char* old = std::getenv("HOME");
    const std::string oldHome = old ? old : "";
#ifdef _WIN32
    _putenv_s("HOME", home.string().c_str());
#else
    setenv("HOME", home.string().c_str(), 1);
#endif
    CHECK(prepareEmulator("azahar", {kXbox}));
    const auto read = [&] {
        std::ifstream in(file);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string out = read();
    CHECK(out.find("profiles\\1\\button_a = \"api:controller,button:1,engine:sdl,guid:0,maptype:all,port:0\"") !=
          std::string::npos);
    CHECK(out.find("profiles\\1\\button_a\\default = false") != std::string::npos);
    CHECK(out.find("profiles\\1\\button_b=\"code:75,engine:keyboard\"") != std::string::npos);  // the user's
    CHECK(out.find("profiles\\1\\circle_pad = \"api:controller,axis_x:0,axis_y:1,") != std::string::npos);
    CHECK(out.find("button_home") == std::string::npos);  // Guide is OmniOS's

    // With no pad, the keyboard's layout: A (the right-hand button) on X,
    // the Circle Pad on I J K L. The user's own binding still stays.
    CHECK(prepareEmulator("azahar"));
    const std::string keyboard = read();
    CHECK(keyboard.find("profiles\\1\\button_a = \"code:88,engine:keyboard\"") != std::string::npos);
    CHECK(keyboard.find("profiles\\1\\button_b=\"code:75,engine:keyboard\"") != std::string::npos);
    CHECK(keyboard.find("profiles\\1\\button_start = \"code:16777220,engine:keyboard\"") != std::string::npos);
    CHECK(keyboard.find("profiles\\1\\circle_pad = \"down:code$075$1engine$0keyboard,") != std::string::npos);
    CHECK(keyboard.find("up:code$073$1engine$0keyboard\"") != std::string::npos);

    fs::remove(file);
    CHECK(prepareEmulator("azahar", {kXbox}));
    const std::string fresh = read();
    CHECK(fresh.find("profiles\\size=1") != std::string::npos);
    CHECK(fresh.find("profiles\\1\\button_b = \"api:controller,button:0,") != std::string::npos);
#ifdef _WIN32
    _putenv_s("HOME", oldHome.c_str());
#else
    setenv("HOME", oldHome.c_str(), 1);
#endif
    std::error_code ec;
    fs::remove_all(home, ec);
}

TEST("emulator setup: DuckStation gets the free BIOS unless the user has a PS1 BIOS") {
    namespace fs = std::filesystem;
    const fs::path home = fs::temp_directory_path() / "omnios-openbios-home";
    fs::remove_all(home);
    fs::create_directories(home);
    const fs::path image = home / "image-openbios.bin";
    { std::ofstream(image) << std::string(512 * 1024, 'o'); }
    const char* old = std::getenv("HOME");
    const std::string oldHome = old ? old : "";
    const auto set = [](const char* name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        if (value.empty()) unsetenv(name); else setenv(name, value.c_str(), 1);
#endif
    };
    set("HOME", home.string());
    set("OMNIOS_OPENBIOS", image.string());

    CHECK(prepareEmulator("duckstation"));
    CHECK(fs::exists(home / "Games/bios/openbios.bin"));

    fs::remove(home / "Games/bios/openbios.bin");
    { std::ofstream(home / "Games/bios/scph1001.bin") << std::string(512 * 1024, 's'); }
    CHECK(prepareEmulator("duckstation"));
    CHECK(!fs::exists(home / "Games/bios/openbios.bin"));  // theirs is used

    set("OMNIOS_OPENBIOS", "");
    set("HOME", oldHome);
    std::error_code ec;
    fs::remove_all(home, ec);
}

TEST("emulator setup: Dolphin's GameCube pad and Wii Remote move from the keyboard to the pad") {
    const std::string keyboard =
        "[GCPad1]\nDevice = XInput2/0/Virtual core pointer\nButtons/A = `X`\n[GCPad2]\nDevice = XInput2/0/Virtual core pointer\n";
    const std::string out = applyDolphinPad(keyboard, "GCPad1", {kDualSense});
    CHECK(out.find("[GCPad1]\nDevice = SDL/0/PS5 Controller\n"
                   "Buttons/A = `Button S` | `XInput2/0/Virtual core pointer:Z`\n") != std::string::npos);
    CHECK(out.find("Main Stick/Up = `Left Y+` | `XInput2/0/Virtual core pointer:I`\n") != std::string::npos);
    CHECK(out.find("Buttons/A = `X`") == std::string::npos);
    CHECK(out.find("[GCPad2]\nDevice = XInput2/0/Virtual core pointer\n") != std::string::npos);
    CHECK_EQ(applyDolphinPad(out, "GCPad1", {kDualSense}), out);  // once

    const std::string wii = applyDolphinPad("", "Wiimote1", {kXbox});
    CHECK(wii.find("[Wiimote1]\nDevice = SDL/0/Xbox 360 Controller\nSource = 1\n") != std::string::npos);
    CHECK(wii.find("Extension = Nunchuk") != std::string::npos);
    CHECK(wii.find("Home") == std::string::npos);  // Guide is OmniOS's
}

TEST("emulator setup: Dolphin follows a new pad and keeps another backend") {
    // Buttons/A is the user's own; Buttons/B is what an older OmniOS wrote,
    // the pad alone, and gets the keyboard added.
    const std::string mine =
        "[GCPad1]\nDevice = SDL/0/Xbox 360 Controller\nButtons/A = `Button E`\nButtons/B = `Button W`\n";
    const std::string swapped = applyDolphinPad(mine, "GCPad1", {kDualSense});
    CHECK(swapped.find("Device = SDL/0/PS5 Controller\nButtons/A = `Button E`\n") != std::string::npos);
    CHECK(swapped.find("Buttons/B = `Button W` | `XInput2/0/Virtual core pointer:A`\n") != std::string::npos);
    const std::string kept = applyDolphinPad(mine, "GCPad1", {kXbox});
    CHECK(kept.find("Device = SDL/0/Xbox 360 Controller\nButtons/A = `Button E`\n") != std::string::npos);
    CHECK_EQ(applyDolphinPad(kept, "GCPad1", {kXbox}), kept);  // once
    const std::string evdev = "[GCPad1]\nDevice = evdev/0/My Pad\n";
    CHECK_EQ(applyDolphinPad(evdev, "GCPad1", {kXbox}), evdev);
}

TEST("emulator setup: Dolphin plays on the keyboard with no pad") {
    // Nothing set up: the keyboard, in the shared layout.
    const std::string fresh = applyDolphinPad("", "GCPad1", {});
    CHECK(fresh.find("[GCPad1]\nDevice = XInput2/0/Virtual core pointer\nButtons/A = `Z`\n") != std::string::npos);
    CHECK(fresh.find("Buttons/Start = `Return`\n") != std::string::npos);
    CHECK(fresh.find("D-Pad/Up = `Up`\n") != std::string::npos);
    CHECK_EQ(applyDolphinPad(fresh, "GCPad1", {}), fresh);  // once

    // A pad set up and now unplugged stays the device: its bindings name
    // the keyboard as well.
    const std::string pad = applyDolphinPad("", "GCPad1", {kXbox});
    CHECK_EQ(applyDolphinPad(pad, "GCPad1", {}), pad);
    CHECK(pad.find("Buttons/A = `Button S` | `XInput2/0/Virtual core pointer:Z`") != std::string::npos);

    const std::string wii = applyDolphinPad("", "Wiimote1", {});
    CHECK(wii.find("Source = 1\nButtons/A = `Z`\nButtons/B = `R`\n") != std::string::npos);
    CHECK(wii.find("Extension = Nunchuk\n") != std::string::npos);
}
