#include "EmulatorSetup.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "Paths.h"
#include "Router.h"

namespace omnios {
namespace {

namespace fs = std::filesystem;

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

struct Line {
    std::string text;
    std::string section;  // the section it is in
    std::string key;      // empty for a header, comment or blank
    std::string value;
};

std::vector<Line> parse(std::string_view ini) {
    std::vector<Line> lines;
    std::string section;
    std::size_t pos = 0;
    while (pos < ini.size()) {
        std::size_t end = ini.find('\n', pos);
        if (end == std::string_view::npos) end = ini.size();
        std::string_view raw = ini.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        pos = end + 1;

        Line line;
        line.text = std::string(raw);
        const std::string_view t = trim(raw);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            section = std::string(t.substr(1, t.size() - 2));
        } else if (!t.empty() && t.front() != ';' && t.front() != '#') {
            const std::size_t eq = t.find('=');
            if (eq != std::string_view::npos) {
                line.key = std::string(trim(t.substr(0, eq)));
                line.value = std::string(trim(t.substr(eq + 1)));
            }
        }
        line.section = section;
        lines.push_back(std::move(line));
    }
    return lines;
}

// A PlayStation pad on the first controller SDL sees, by position, so a
// DualSense and an Xbox pad agree. DuckStation and PCSX2 name everything the
// same way except the four face buttons: "South" in one, "FaceSouth" in the
// other (read from their binaries; "A", "B", "X", "Y" are refused by both,
// as PCSX2's log says: "Invalid binding: 'SDL-0/A'"). A leading '@' marks a
// face button, completed by facePad() below.
const std::pair<const char*, const char*> kPlayStationPad[] = {
    {"Up", "DPadUp"},        {"Right", "DPadRight"},      {"Down", "DPadDown"},
    {"Left", "DPadLeft"},    {"Triangle", "@North"},      {"Circle", "@East"},
    {"Cross", "@South"},     {"Square", "@West"},         {"Select", "Back"},
    {"Start", "Start"},      {"L1", "LeftShoulder"},      {"R1", "RightShoulder"},
    {"L2", "+LeftTrigger"},  {"R2", "+RightTrigger"},     {"L3", "LeftStick"},
    {"R3", "RightStick"},    {"LLeft", "-LeftX"},         {"LRight", "+LeftX"},
    {"LDown", "+LeftY"},     {"LUp", "-LeftY"},           {"RLeft", "-RightX"},
    {"RRight", "+RightX"},   {"RDown", "+RightY"},        {"RUp", "-RightY"},
};

// The pad's bindings, with face buttons named `facePrefix` + direction.
void addPad(std::vector<IniSetting>& settings, const std::string& facePrefix) {
    for (const auto& [button, sdl] : kPlayStationPad) {
        const std::string_view name(sdl);
        const std::string binding = name.front() == '@' ? facePrefix + std::string(name.substr(1)) : std::string(name);
        settings.push_back({"Pad1", button, "SDL-0/" + binding, IniSetting::Mode::Add, {}});
    }
}

std::vector<IniSetting> duckStationSettings() {
    std::vector<IniSetting> s = {
        {"Main", "SetupWizardIncomplete", "false", IniSetting::Mode::Set, {"true"}},
        {"Main", "ConfirmPowerOff", "false", IniSetting::Mode::Set, {"true"}},
        {"BIOS", "SearchDirectory", biosDir().generic_string(), IniSetting::Mode::Set, {"bios", ""}},
        // Its own updater opens a window over the game, one a controller
        // cannot close. Flathub updates it, with everything else.
        {"AutoUpdater", "CheckAtStartup", "false", IniSetting::Mode::Set, {"true"}},
        // Its "Automatic" renderer chose OpenGL, and OpenGL full screen on
        // Wayland drew the game a few centimetres wide in the bottom-left
        // corner (seen in the VM; windowed, and with Vulkan, it filled the
        // screen). Every current Linux GPU driver has Vulkan.
        {"GPU", "Renderer", "Vulkan", IniSetting::Mode::Set, {"Automatic", ""}},
        // Select + Start opens DuckStation's pause menu, where Exit returns
        // to OmniOS (it was started with -batch).
        {"Hotkeys", "OpenPauseMenu", "SDL-0/Back & SDL-0/Start", IniSetting::Mode::Add, {}},
    };
    addPad(s, "");
    return s;
}

std::vector<IniSetting> pcsx2Settings() {
    std::vector<IniSetting> s = {
        {"UI", "SetupWizardIncomplete", "false", IniSetting::Mode::Set, {"true"}},
        {"UI", "ConfirmShutdown", "false", IniSetting::Mode::Set, {"true"}},
        {"Folders", "Bios", biosDir().generic_string(), IniSetting::Mode::Set, {"bios", ""}},
        {"AutoUpdater", "CheckAtStartup", "false", IniSetting::Mode::Set, {"true"}},
        {"Hotkeys", "OpenPauseMenu", "SDL-0/Back & SDL-0/Start", IniSetting::Mode::Add, {}},
    };
    addPad(s, "Face");
    return s;
}

// Azahar can bind "any controller" (maptype:all) by SDL's gamepad button and
// axis numbers, so unlike RPCS3 and Ryubing it needs no pad plugged in. Its
// settings are Qt's: QSettings writes a key "profiles/1/button_a" as
// profiles\1\button_a, quotes a value holding commas, and gives each key a
// "\default" twin which, while true, makes Azahar ignore the value and use
// its default (the keyboard) instead.
std::vector<IniSetting> azaharSettings() {
    // A 3DS button; what it becomes (SDL2's SDL_GameControllerButton and
    // SDL_GameControllerAxis numbers); Azahar's keyboard default.
    struct Bind { const char* key; const char* pad; const char* keyboard; };
    static const Bind kButtons[] = {
        {"button_a", "button:1", "code:65"},  // the right-hand face button, as on a 3DS
        {"button_b", "button:0", "code:83"},
        {"button_x", "button:3", "code:90"},
        {"button_y", "button:2", "code:88"},
        {"button_up", "button:11", "code:84"},
        {"button_down", "button:12", "code:71"},
        {"button_left", "button:13", "code:70"},
        {"button_right", "button:14", "code:72"},
        {"button_l", "button:9", "code:81"},
        {"button_r", "button:10", "code:87"},
        {"button_start", "button:6", "code:77"},
        {"button_select", "button:4", "code:78"},
        {"button_zl", "axis:4,direction:+,threshold:0.500000", "code:49"},
        {"button_zr", "axis:5,direction:+,threshold:0.500000", "code:50"},
        // Home is left on the keyboard: on a pad it is the Guide button,
        // which belongs to OmniOS.
    };
    const std::string any = ",engine:sdl,guid:0,maptype:all,port:0\"";
    const std::string profile = "profiles\\1\\";

    std::vector<IniSetting> s;
    const auto bind = [&](const std::string& key, const std::string& value, const std::string& keyboard) {
        s.push_back({"Controls", profile + key, value, IniSetting::Mode::Set, {keyboard}});
        s.push_back({"Controls", profile + key + "\\default", "false", IniSetting::Mode::Set, {"true"}});
    };
    for (const Bind& b : kButtons)
        bind(b.key, "\"api:controller," + std::string(b.pad) + any, "\"" + std::string(b.keyboard) + ",engine:keyboard\"");
    bind("circle_pad", "\"api:controller,axis_x:0,axis_y:1" + any,
         "\"down:code$016777237$1engine$0keyboard,engine:analog_from_button,left:code$016777234$1engine$0keyboard,"
         "modifier:code$068$1engine$0keyboard,modifier_scale:0.500000,right:code$016777236$1engine$0keyboard,"
         "up:code$016777235$1engine$0keyboard\"");
    bind("c_stick", "\"api:controller,axis_x:2,axis_y:3" + any,
         "\"down:code$075$1engine$0keyboard,engine:analog_from_button,left:code$074$1engine$0keyboard,"
         "modifier:code$068$1engine$0keyboard,modifier_scale:0.500000,right:code$076$1engine$0keyboard,"
         "up:code$073$1engine$0keyboard\"");
    // What its controls page shows as the mapping type: "all controllers".
    bind("input_maptype", "0", "2");
    return s;
}

// RPCS3's name for a pad: SDL's, numbered among pads of the same name.
std::string rpcs3Device(const Controller& pad) {
    return pad.name + " 1";
}

// A YAML double-quoted string.
std::string yamlQuoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string yamlUnquoted(std::string_view text) {
    text = trim(text);
    if (text.size() < 2 || (text.front() != '"' && text.front() != '\'') || text.back() != text.front())
        return std::string(text);
    std::string out;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        if (text.front() == '"' && text[i] == '\\' && i + 2 < text.size()) ++i;
        out += text[i];
    }
    return out;
}

// Player 1 on `pad` through RPCS3's SDL handler, with the defaults its own
// pad settings give an SDL pad (sdl_pad_handler::init_config), except the PS
// button: that is Guide there, and Guide is OmniOS's, so here it is Start +
// Select, as it also is in RPCS3's defaults.
std::string rpcs3Player(const Controller& pad) {
    static const std::pair<const char*, const char*> kButtons[] = {
        {"Left Stick Left", "LS X-"},  {"Left Stick Down", "LS Y-"},  {"Left Stick Right", "LS X+"},
        {"Left Stick Up", "LS Y+"},    {"Right Stick Left", "RS X-"}, {"Right Stick Down", "RS Y-"},
        {"Right Stick Right", "RS X+"}, {"Right Stick Up", "RS Y+"},  {"Start", "Start"},
        {"Select", "Back"},            {"PS Button", "Start&Back"},   {"Square", "West"},
        {"Cross", "South"},            {"Circle", "East"},            {"Triangle", "North"},
        {"Left", "Left"},              {"Down", "Down"},              {"Right", "Right"},
        {"Up", "Up"},                  {"R1", "RB"},                  {"R2", "RT"},
        {"R3", "RS"},                  {"L1", "LB"},                  {"L2", "LT"},
        {"L3", "LS"},                  {"Left Stick Deadzone", "8000"}, {"Right Stick Deadzone", "8000"},
        {"Left Stick Anti-Deadzone", "4259"}, {"Right Stick Anti-Deadzone", "4259"},
    };
    std::string out = "Player 1 Input:\n  Handler: SDL\n  Device: " + yamlQuoted(rpcs3Device(pad)) + "\n  Config:\n";
    for (const auto& [name, value] : kButtons) out += std::string("    ") + name + ": " + value + "\n";
    return out;
}

// Dolphin's bindings for a pad through its SDL backend, whose names are by
// position: "Button S" is the bottom face button on any pad.
using Bindings = std::vector<std::pair<const char*, const char*>>;

// A GameCube pad laid out as a GameCube pad is: A the big bottom button,
// B left of it, X right, Y above. Z on the right shoulder.
const Bindings kDolphinGameCube = {
    {"Buttons/A", "`Button S`"}, {"Buttons/B", "`Button W`"}, {"Buttons/X", "`Button E`"},
    {"Buttons/Y", "`Button N`"}, {"Buttons/Z", "`Shoulder R`"}, {"Buttons/Start", "`Start`"},
    {"Main Stick/Up", "`Left Y+`"}, {"Main Stick/Down", "`Left Y-`"},
    {"Main Stick/Left", "`Left X-`"}, {"Main Stick/Right", "`Left X+`"},
    {"C-Stick/Up", "`Right Y+`"}, {"C-Stick/Down", "`Right Y-`"},
    {"C-Stick/Left", "`Right X-`"}, {"C-Stick/Right", "`Right X+`"},
    {"Triggers/L", "`Trigger L`"}, {"Triggers/R", "`Trigger R`"},
    {"Triggers/L-Analog", "`Trigger L`"}, {"Triggers/R-Analog", "`Trigger R`"},
    {"D-Pad/Up", "`Pad N`"}, {"D-Pad/Down", "`Pad S`"}, {"D-Pad/Left", "`Pad W`"}, {"D-Pad/Right", "`Pad E`"},
};

// A Wii Remote with a Nunchuk, the way most Wii games are played: the
// Nunchuk's stick on the left stick, the pointer on the right one, B (the
// Remote's trigger) on the right trigger, C and Z on the left shoulder and
// trigger. Home is left off: on a pad that is Guide, and Guide is OmniOS's.
const Bindings kDolphinWiimote = {
    {"Source", "1"},
    {"Buttons/A", "`Button S`"}, {"Buttons/B", "`Trigger R`"}, {"Buttons/1", "`Button W`"},
    {"Buttons/2", "`Button N`"}, {"Buttons/-", "`Back`"}, {"Buttons/+", "`Start`"},
    {"D-Pad/Up", "`Pad N`"}, {"D-Pad/Down", "`Pad S`"}, {"D-Pad/Left", "`Pad W`"}, {"D-Pad/Right", "`Pad E`"},
    {"IR/Up", "`Right Y+`"}, {"IR/Down", "`Right Y-`"}, {"IR/Left", "`Right X-`"}, {"IR/Right", "`Right X+`"},
    {"Shake/X", "`Button E`"}, {"Shake/Y", "`Button E`"}, {"Shake/Z", "`Button E`"},
    {"Extension", "Nunchuk"},
    {"Nunchuk/Buttons/C", "`Shoulder L`"}, {"Nunchuk/Buttons/Z", "`Trigger L`"},
    {"Nunchuk/Stick/Up", "`Left Y+`"}, {"Nunchuk/Stick/Down", "`Left Y-`"},
    {"Nunchuk/Stick/Left", "`Left X-`"}, {"Nunchuk/Stick/Right", "`Left X+`"},
};

// Dolphin's name for a pad: backend, number among pads of that name, SDL's name.
std::string dolphinDevice(const Controller& pad) {
    return "SDL/0/" + pad.name;
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Writes through a temporary file, so the emulator never reads half of one.
bool writeFile(const fs::path& file, const std::string& text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path temp = file.string() + ".omnios-tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    fs::rename(temp, file, ec);
    return !ec;
}

// `file` put through `change`. A missing file starts as `fresh`, or is left
// missing when `fresh` is empty.
template <typename Change>
bool updateFile(const fs::path& file, const std::string& fresh, Change change) {
    std::error_code ec;
    const bool exists = fs::exists(file, ec);
    if (!exists && fresh.empty()) return true;
    const std::string current = exists ? readFile(file) : fresh;
    const std::string updated = change(current);
    if (exists && updated == current) return true;
    return writeFile(file, updated);
}

}  // namespace

std::string ryubingGamepadId(std::string_view guid) {
    if (guid.size() != 32) return {};
    std::string hex;
    for (const char c : guid) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (hex.find_first_not_of('0') == std::string::npos) return {};
    const auto byte = [&](int i) { return hex.substr(static_cast<std::size_t>(i) * 2, 2); };
    // .NET prints a Guid made from these 16 bytes with the first three
    // groups little-endian: 03020100-0504-0706-0809-0a0b0c0d0e0f. Ryubing
    // then replaces the first four digits, SDL's CRC of the name, with zeros.
    return "0-0000" + byte(1) + byte(0) + "-" + byte(5) + byte(4) + "-" + byte(7) + byte(6) + "-" + byte(8) +
           byte(9) + "-" + byte(10) + byte(11) + byte(12) + byte(13) + byte(14) + byte(15);
}

std::string applyRpcs3Pad(std::string_view yml, const std::vector<Controller>& controllers) {
    if (controllers.empty()) return std::string(yml);
    const Controller& pad = controllers.front();

    // Player 1's block: its header line and the indented lines under it.
    std::vector<std::string> lines;
    for (std::size_t pos = 0; pos < yml.size();) {
        std::size_t end = yml.find('\n', pos);
        if (end == std::string_view::npos) end = yml.size();
        lines.emplace_back(yml.substr(pos, end - pos));
        pos = end + 1;
    }
    std::size_t begin = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (trim(lines[i]) == "Player 1 Input:") begin = i;
    std::size_t end = begin + 1;
    while (end < lines.size() && (lines[end].empty() || lines[end].front() == ' ')) ++end;

    std::string handler = "Keyboard";  // RPCS3's player 1 with no config
    std::string device = "Keyboard";
    std::size_t deviceLine = lines.size();
    for (std::size_t i = begin + 1; i < end && i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.rfind("  Handler:", 0) == 0) handler = yamlUnquoted(line.substr(10));
        if (line.rfind("  Device:", 0) == 0) {
            device = yamlUnquoted(line.substr(9));
            deviceLine = i;
        }
    }

    std::string out;
    const auto join = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to && i < lines.size(); ++i) out += lines[i] + "\n";
    };
    if (handler == "Keyboard" || handler == "Null") {
        join(0, begin);
        out += rpcs3Player(pad);
        join(end, lines.size());
        return out;
    }
    if (handler != "SDL" || deviceLine == lines.size()) return std::string(yml);
    for (const Controller& connected : controllers)
        if (device == rpcs3Device(connected)) return std::string(yml);
    lines[deviceLine] = "  Device: " + yamlQuoted(rpcs3Device(pad));
    join(0, lines.size());
    return out;
}

std::string applyRyubingPad(std::string_view json, const std::vector<Controller>& controllers) {
    if (controllers.empty()) return std::string(json);
    const Controller& pad = controllers.front();
    const std::string id = ryubingGamepadId(pad.guid);
    std::string error;
    Json config = Json::parse(json, error);
    if (id.empty() || !error.empty() || !config.isObject()) return std::string(json);

    std::vector<Json> players = config["input_config"].items();
    Json* player1 = nullptr;
    for (Json& player : players)
        if (player["player_index"].asString() == "Player1") player1 = &player;

    if (player1 != nullptr && (*player1)["backend"].asString() == "GamepadSDL2") {
        for (const Controller& connected : controllers)
            if ((*player1)["id"].asString() == ryubingGamepadId(connected.guid)) return std::string(json);
        player1->set("id", Json(id));
        player1->set("name", Json(pad.name));
    } else if (player1 == nullptr || (*player1)["backend"].asString() == "WindowKeyboard") {
        // A Pro Controller, as Ryubing's own default for a pad sets it up
        // (the buttons by position: its A is the right-hand one), but with
        // rumble on.
        const auto object = [](std::map<std::string, Json> members) { return Json::object(std::move(members)); };
        const auto stick = [&](const char* which, const char* button) {
            return object({{"joystick", Json(which)}, {"invert_stick_x", Json(false)},
                           {"invert_stick_y", Json(false)}, {"rotate90_cw", Json(false)},
                           {"stick_button", Json(button)}});
        };
        Json mine = object({
            {"left_joycon_stick", stick("Left", "LeftStick")},
            {"right_joycon_stick", stick("Right", "RightStick")},
            {"deadzone_left", Json(0.1)}, {"deadzone_right", Json(0.1)},
            {"range_left", Json(1.0)}, {"range_right", Json(1.0)},
            {"trigger_threshold", Json(0.5)},
            {"motion", object({{"motion_backend", Json("GamepadDriver")}, {"sensitivity", Json(100.0)},
                               {"gyro_deadzone", Json(1.0)}, {"enable_motion", Json(true)}})},
            {"rumble", object({{"strong_rumble", Json(1.0)}, {"weak_rumble", Json(1.0)},
                               {"enable_rumble", Json(true)}})},
            {"led", object({{"enable_led", Json(false)}, {"turn_off_led", Json(false)},
                            {"use_rainbow", Json(false)}, {"led_color", Json(0.0)}})},
            {"left_joycon", object({{"button_minus", Json("Minus")}, {"button_l", Json("LeftShoulder")},
                                    {"button_zl", Json("LeftTrigger")}, {"button_sl", Json("Unbound")},
                                    {"button_sr", Json("Unbound")}, {"dpad_up", Json("DpadUp")},
                                    {"dpad_down", Json("DpadDown")}, {"dpad_left", Json("DpadLeft")},
                                    {"dpad_right", Json("DpadRight")}})},
            {"right_joycon", object({{"button_plus", Json("Plus")}, {"button_r", Json("RightShoulder")},
                                     {"button_zr", Json("RightTrigger")}, {"button_sl", Json("Unbound")},
                                     {"button_sr", Json("Unbound")}, {"button_a", Json("B")},
                                     {"button_b", Json("A")}, {"button_x", Json("Y")},
                                     {"button_y", Json("X")}})},
            {"version", Json(1.0)},
            {"backend", Json("GamepadSDL2")},
            {"id", Json(id)},
            {"name", Json(pad.name)},
            {"controller_type", Json("ProController")},
            {"player_index", Json("Player1")},
        });
        if (player1 != nullptr) *player1 = std::move(mine); else players.push_back(std::move(mine));
    } else {
        return std::string(json);
    }
    config.set("input_config", Json::array(std::move(players)));
    return config.dump(2);
}

std::string applyIniSettings(std::string_view ini, const std::vector<IniSetting>& settings) {
    std::vector<Line> lines = parse(ini);

    for (const IniSetting& setting : settings) {
        // Where the section's last line is, for a key that has to be added.
        std::size_t sectionEnd = lines.size();
        bool sectionFound = false;
        bool done = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            Line& line = lines[i];
            if (line.section != setting.section) continue;
            sectionFound = true;
            // Past trailing blank lines, so an added key sits with the others.
            if (!trim(line.text).empty()) sectionEnd = i + 1;
            if (line.key != setting.key) continue;
            if (setting.mode == IniSetting::Mode::Add) {
                if (line.value == setting.value) done = true;
            } else {
                if (std::find(setting.replaces.begin(), setting.replaces.end(), line.value) !=
                    setting.replaces.end()) {
                    line.value = setting.value;
                    line.text = setting.key + " = " + setting.value;
                }
                done = true;  // set, or chosen by the user and kept
            }
        }
        if (done) continue;

        Line added;
        added.section = setting.section;
        added.key = setting.key;
        added.value = setting.value;
        added.text = setting.key + " = " + setting.value;
        if (!sectionFound) {
            if (!lines.empty() && !trim(lines.back().text).empty()) lines.push_back(Line{"", lines.back().section, "", ""});
            Line header;
            header.text = "[" + setting.section + "]";
            header.section = setting.section;
            lines.push_back(header);
            lines.push_back(added);
        } else {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), added);
        }
    }

    std::string out;
    for (const Line& line : lines) {
        out += line.text;
        out += '\n';
    }
    return out;
}

std::string applyDolphinPad(std::string_view ini, std::string_view section,
                            const std::vector<Controller>& controllers) {
    if (controllers.empty()) return std::string(ini);
    const Bindings& bindings = section.rfind("Wiimote", 0) == 0 ? kDolphinWiimote : kDolphinGameCube;
    const Controller& pad = controllers.front();

    std::vector<std::string> lines;
    for (std::size_t pos = 0; pos < ini.size();) {
        std::size_t end = ini.find('\n', pos);
        if (end == std::string_view::npos) end = ini.size();
        std::string_view line = ini.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.emplace_back(line);
        pos = end + 1;
    }
    const std::string header = "[" + std::string(section) + "]";
    std::size_t begin = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (trim(lines[i]) == header) begin = i;
    std::size_t end = begin + 1;
    while (end < lines.size() && trim(lines[end]).rfind('[', 0) != 0) ++end;

    std::string device;  // none: Dolphin's default, the keyboard
    std::size_t deviceLine = lines.size();
    for (std::size_t i = begin + 1; i < end && i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos && trim(line.substr(0, eq)) == "Device") {
            device = std::string(trim(line.substr(eq + 1)));
            deviceLine = i;
        }
    }

    std::string out;
    const auto join = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to && i < lines.size(); ++i) out += lines[i] + "\n";
    };
    if (device.empty() || device.rfind("XInput2/", 0) == 0) {
        join(0, begin);
        out += header + "\nDevice = " + dolphinDevice(pad) + "\n";
        for (const auto& [key, value] : bindings) out += std::string(key) + " = " + value + "\n";
        join(end, lines.size());
        return out;
    }
    if (device.rfind("SDL/", 0) != 0) return std::string(ini);  // another backend: the user's
    for (const Controller& connected : controllers)
        if (device == dolphinDevice(connected)) return std::string(ini);
    lines[deviceLine] = "Device = " + dolphinDevice(pad);
    join(0, lines.size());
    return out;
}

bool prepareEmulator(std::string_view engineId, const std::vector<Controller>& controllers) {
    const auto ini = [](const fs::path& file, const std::string& fresh, const std::vector<IniSetting>& settings) {
        return updateFile(file, fresh, [&](const std::string& text) { return applyIniSettings(text, settings); });
    };

    if (engineId == "duckstation") {
        // The free BIOS, where DuckStation looks, unless the user has a PS1
        // BIOS of their own there (a PS1 BIOS is 512 KB). DuckStation knows
        // OpenBIOS by a signature inside it, takes it for any region, and
        // ranks it below every real BIOS, so one added later wins by itself.
        // A newer copy on the image replaces an older one.
        std::error_code ec;
        const fs::path from = openBiosImage();
        const fs::path to = biosDir() / "openbios.bin";
        bool own = false;
        for (fs::directory_iterator it(biosDir(), ec), end; !ec && it != end; it.increment(ec))
            if (it->path().filename() != "openbios.bin" && it->is_regular_file(ec) && it->file_size(ec) == 512 * 1024)
                own = true;
        if (!own && fs::exists(from, ec) &&
            !(fs::exists(to, ec) && fs::last_write_time(to, ec) >= fs::last_write_time(from, ec))) {
            fs::create_directories(biosDir(), ec);
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        }
        // Without its version a settings file is thrown away and replaced by
        // defaults, wizard and all; 3 is the version DuckStation writes today.
        // If a later one moves on, it resets once, and the next launch puts
        // these back over its defaults.
        return ini(homeDir() / ".var/app/org.duckstation.DuckStation/config/duckstation/settings.ini",
                   "[Main]\nSettingsVersion = 3\n", duckStationSettings());
    }
    if (engineId == "pcsx2") {
        return ini(homeDir() / ".var/app/net.pcsx2.PCSX2/config/PCSX2/inis/PCSX2.ini",
                   "[UI]\nSettingsVersion = 1\n",  // as for DuckStation, above
                   pcsx2Settings());
    }
    if (engineId == "dolphin") {
        // From the image, not Flathub: its settings are in ~/.config. Player
        // 1's GameCube pad and Wii Remote both, since one Dolphin plays both.
        if (controllers.empty()) return true;
        const fs::path dir = homeDir() / ".config/dolphin-emu";
        const auto pad = [&](const char* file, const char* section) {
            return updateFile(dir / file, std::string("[") + section + "]\n",
                              [&](const std::string& text) { return applyDolphinPad(text, section, controllers); });
        };
        const bool gameCube = pad("GCPadNew.ini", "GCPad1");
        const bool wii = pad("WiimoteNew.ini", "Wiimote1");
        return gameCube && wii;
    }
    if (engineId == "azahar") {
        // A missing file needs its one profile counted, or Azahar makes a
        // default one and never reads these.
        return ini(flatpakConfigDir("org.azahar_emu.Azahar") / "azahar-emu/qt-config.ini",
                   "[Controls]\nprofiles\\size=1\n", azaharSettings());
    }
    if (engineId == "rpcs3") {
        // Its message boxes, each one a stop a controller cannot get past:
        // the welcome on first start, "installed" after the system software,
        // "are you sure" on leaving a game. (Its "install firmware?" question
        // has no setting; Router.cpp says so where it offers the install.)
        const fs::path dir = flatpakConfigDir("net.rpcs3.RPCS3") / "rpcs3";
        const bool boxes = ini(dir / "GuiConfigs/CurrentSettings.ini", "",
                               {
                                   {"Meta", "infoBoxEnabledWelcome", "false", IniSetting::Mode::Set, {"true"}},
                                   {"Meta", "infoBoxEnabledInstallPUP", "false", IniSetting::Mode::Set, {"true"}},
                                   {"Meta", "confirmationBoxExitGame", "false", IniSetting::Mode::Set, {"true"}},
                               });
        // Until a pad is set up in it RPCS3 plays on the keyboard. With no
        // file it uses Default.yml, whatever the active configuration says.
        const bool pad = controllers.empty() ||
                         updateFile(dir / "input_configs/global/Default.yml", "Player 1 Input:\n",
                                    [&](const std::string& text) { return applyRpcs3Pad(text, controllers); });
        return boxes && pad;
    }
    if (engineId == "ryubing") {
        // The Switch's keys, from Games/bios where the user put them, into
        // Ryubing's own system folder where it looks. A newer copy replaces
        // an older one.
        const fs::path dir = flatpakConfigDir("io.github.ryubing.Ryujinx") / "Ryujinx";
        std::error_code ec;
        for (const char* name : {"prod.keys", "title.keys"}) {
            const fs::path from = biosDir() / name;
            if (!fs::exists(from, ec)) continue;
            const fs::path to = dir / "system" / name;
            if (fs::exists(to, ec) && fs::last_write_time(to, ec) >= fs::last_write_time(from, ec)) continue;
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }
        // The pad goes into its Config.json, which it writes on its first
        // start: before that there is nothing to add it to (a partial file
        // would be replaced by its defaults), so the very first start is on
        // the keyboard, and every one after it on the pad.
        return updateFile(dir / "Config.json", "",
                          [&](const std::string& text) { return applyRyubingPad(text, controllers); });
    }
    return true;
}
}  // namespace omnios
