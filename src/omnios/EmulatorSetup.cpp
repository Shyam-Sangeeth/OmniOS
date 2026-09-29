#include "EmulatorSetup.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "KeyboardLayout.h"
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
// DualSense and an Xbox pad agree, and the keyboard beside it
// (KeyboardLayout.h). DuckStation and PCSX2 name everything the same way
// except the four face buttons. PCSX2 says "FaceSouth" and refuses "A"
// ("Invalid binding: 'SDL-0/A'" in its log). DuckStation says "A" to "Y",
// SDL's own positional names ("A" is the bottom button on any pad), and
// refuses "South" ("Invalid binding: 'SDL-0/South'"), which it only uses for
// hat directions. A leading '@' marks a face button, named by addPad().
struct PlayStationBind { const char* button; const char* sdl; PadButton position; };
const PlayStationBind kPlayStationPad[] = {
    {"Up", "DPadUp", PadButton::Up},             {"Right", "DPadRight", PadButton::Right},
    {"Down", "DPadDown", PadButton::Down},       {"Left", "DPadLeft", PadButton::Left},
    {"Triangle", "@North", PadButton::North},    {"Circle", "@East", PadButton::East},
    {"Cross", "@South", PadButton::South},       {"Square", "@West", PadButton::West},
    {"Select", "Back", PadButton::Select},       {"Start", "Start", PadButton::Start},
    {"L1", "LeftShoulder", PadButton::L1},       {"R1", "RightShoulder", PadButton::R1},
    {"L2", "+LeftTrigger", PadButton::L2},       {"R2", "+RightTrigger", PadButton::R2},
    {"L3", "LeftStick", PadButton::L3},          {"R3", "RightStick", PadButton::R3},
    {"LLeft", "-LeftX", PadButton::LeftLeft},    {"LRight", "+LeftX", PadButton::LeftRight},
    {"LDown", "+LeftY", PadButton::LeftDown},    {"LUp", "-LeftY", PadButton::LeftUp},
    {"RLeft", "-RightX", PadButton::RightLeft},  {"RRight", "+RightX", PadButton::RightRight},
    {"RDown", "+RightY", PadButton::RightDown},  {"RUp", "-RightY", PadButton::RightUp},
};

// How an emulator names the four face buttons.
enum class FaceNames { Letters, FacePrefixed };

// The pad's bindings, with face buttons named as `faces` says, and the
// keyboard's: both at once, as each button takes several bindings. Escape
// opens the pause menu, which is the keyboard's way out of a game there, as
// Select + Start is the pad's.
void addPad(std::vector<IniSetting>& settings, FaceNames faces) {
    for (const PlayStationBind& b : kPlayStationPad) {
        const std::string_view name(b.sdl);
        std::string binding(name);
        if (name.front() == '@') {
            const std::string_view direction = name.substr(1);
            if (faces == FaceNames::FacePrefixed) {
                binding = "Face" + std::string(direction);
            } else {
                binding = direction == "South" ? "A" : direction == "East" ? "B" : direction == "West" ? "X" : "Y";
            }
        }
        settings.push_back({"Pad1", b.button, "SDL-0/" + binding, IniSetting::Mode::Add, {}});
        settings.push_back({"Pad1", b.button, "Keyboard/" + qtKeyName(keyFor(b.position)), IniSetting::Mode::Add, {}});
    }
    settings.push_back({"Hotkeys", "OpenPauseMenu", "Keyboard/Escape", IniSetting::Mode::Add, {}});
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
    addPad(s, FaceNames::Letters);
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
    addPad(s, FaceNames::FacePrefixed);
    return s;
}

// Azahar can bind "any controller" (maptype:all) by SDL's gamepad button and
// axis numbers, so unlike RPCS3 and Ryubing it needs no pad plugged in. Its
// settings are Qt's: QSettings writes a key "profiles/1/button_a" as
// profiles\1\button_a, quotes a value holding commas, and gives each key a
// "\default" twin which, while true, makes Azahar ignore the value and use
// its default (the keyboard) instead.
//
// One binding per button, so it is the pad or the keyboard: the pad when one
// is connected as the game starts, the keyboard's layout (KeyboardLayout.h)
// when none is. Either replaces the other and Azahar's own defaults, never a
// binding the user chose.
std::vector<IniSetting> azaharSettings(bool pad) {
    // A 3DS button; its position on the pad and so its key; what it is on a
    // pad (SDL2's SDL_GameControllerButton and SDL_GameControllerAxis
    // numbers); Azahar's own keyboard default.
    struct Bind { const char* key; PadButton position; const char* pad; const char* azahar; };
    static const Bind kButtons[] = {
        {"button_a", PadButton::East, "button:1", "code:65"},  // the right-hand face button, as on a 3DS
        {"button_b", PadButton::South, "button:0", "code:83"},
        {"button_x", PadButton::North, "button:3", "code:90"},
        {"button_y", PadButton::West, "button:2", "code:88"},
        {"button_up", PadButton::Up, "button:11", "code:84"},
        {"button_down", PadButton::Down, "button:12", "code:71"},
        {"button_left", PadButton::Left, "button:13", "code:70"},
        {"button_right", PadButton::Right, "button:14", "code:72"},
        {"button_l", PadButton::L1, "button:9", "code:81"},
        {"button_r", PadButton::R1, "button:10", "code:87"},
        {"button_start", PadButton::Start, "button:6", "code:77"},
        {"button_select", PadButton::Select, "button:4", "code:78"},
        {"button_zl", PadButton::L2, "axis:4,direction:+,threshold:0.500000", "code:49"},
        {"button_zr", PadButton::R2, "axis:5,direction:+,threshold:0.500000", "code:50"},
        // Home is left as it is: on a pad it is the Guide button, which
        // belongs to OmniOS.
    };
    const std::string any = ",engine:sdl,guid:0,maptype:all,port:0\"";
    const std::string profile = "profiles\\1\\";
    const auto code = [](PadButton position) { return std::to_string(qtKeyCode(keyFor(position))); };
    // A stick on four keys, as Azahar writes one: its fields in order, D (its
    // default) for half a push.
    const auto keys = [](const std::string& down, const std::string& left, const std::string& right,
                         const std::string& up) {
        const auto key = [](const std::string& c) { return "code$0" + c + "$1engine$0keyboard"; };
        return "\"down:" + key(down) + ",engine:analog_from_button,left:" + key(left) + ",modifier:" + key("68") +
               ",modifier_scale:0.500000,right:" + key(right) + ",up:" + key(up) + "\"";
    };

    std::vector<IniSetting> s;
    const auto bind = [&](const std::string& key, const std::string& padValue, const std::string& keyboard,
                          const std::string& azahar) {
        s.push_back({"Controls", profile + key, pad ? padValue : keyboard, IniSetting::Mode::Set,
                     {azahar, padValue, keyboard}});
        s.push_back({"Controls", profile + key + "\\default", "false", IniSetting::Mode::Set, {"true"}});
    };
    for (const Bind& b : kButtons)
        bind(b.key, "\"api:controller," + std::string(b.pad) + any,
             "\"code:" + code(b.position) + ",engine:keyboard\"", "\"" + std::string(b.azahar) + ",engine:keyboard\"");
    bind("circle_pad", "\"api:controller,axis_x:0,axis_y:1" + any,
         keys(code(PadButton::LeftDown), code(PadButton::LeftLeft), code(PadButton::LeftRight), code(PadButton::LeftUp)),
         keys("16777237", "16777234", "16777236", "16777235"));
    bind("c_stick", "\"api:controller,axis_x:2,axis_y:3" + any,
         keys(code(PadButton::RightDown), code(PadButton::RightLeft), code(PadButton::RightRight),
              code(PadButton::RightUp)),
         keys("75", "74", "76", "73"));
    // What its controls page shows as the mapping type: "all controllers".
    if (pad) s.push_back({"Controls", profile + "input_maptype", "0", IniSetting::Mode::Set, {"2"}});
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

// Player 1 on the keyboard, in the layout every emulator here shares
// (KeyboardLayout.h); keys named as Qt names them, which is how RPCS3's
// keyboard handler stores them. The PS button stays RPCS3's own, Backspace.
std::string rpcs3Keyboard() {
    static const std::pair<const char*, PadButton> kButtons[] = {
        {"Left Stick Left", PadButton::LeftLeft},   {"Left Stick Down", PadButton::LeftDown},
        {"Left Stick Right", PadButton::LeftRight}, {"Left Stick Up", PadButton::LeftUp},
        {"Right Stick Left", PadButton::RightLeft}, {"Right Stick Down", PadButton::RightDown},
        {"Right Stick Right", PadButton::RightRight}, {"Right Stick Up", PadButton::RightUp},
        {"Start", PadButton::Start},   {"Select", PadButton::Select}, {"Square", PadButton::West},
        {"Cross", PadButton::South},   {"Circle", PadButton::East},   {"Triangle", PadButton::North},
        {"Left", PadButton::Left},     {"Down", PadButton::Down},     {"Right", PadButton::Right},
        {"Up", PadButton::Up},         {"R1", PadButton::R1},         {"R2", PadButton::R2},
        {"R3", PadButton::R3},         {"L1", PadButton::L1},         {"L2", PadButton::L2},
        {"L3", PadButton::L3},
    };
    std::string out = "Player 1 Input:\n  Handler: Keyboard\n  Device: Keyboard\n  Config:\n";
    for (const auto& [name, position] : kButtons)
        out += std::string("    ") + name + ": " + qtKeyName(keyFor(position)) + "\n";
    out += "    PS Button: " + qtKeyName(Key::Backspace) + "\n";
    return out;
}

// Dolphin's bindings: each control of the emulated pad, and the position on
// the real one that plays it. A few settings are not bindings but values
// (`literal`), such as which extension the Wii Remote has.
struct DolphinBind { const char* control; PadButton position; const char* literal = nullptr; };
using Bindings = std::vector<DolphinBind>;

// A GameCube pad laid out as a GameCube pad is: A the big bottom button,
// B left of it, X right, Y above. Z on the right shoulder.
const Bindings kDolphinGameCube = {
    {"Buttons/A", PadButton::South}, {"Buttons/B", PadButton::West}, {"Buttons/X", PadButton::East},
    {"Buttons/Y", PadButton::North}, {"Buttons/Z", PadButton::R1}, {"Buttons/Start", PadButton::Start},
    {"Main Stick/Up", PadButton::LeftUp}, {"Main Stick/Down", PadButton::LeftDown},
    {"Main Stick/Left", PadButton::LeftLeft}, {"Main Stick/Right", PadButton::LeftRight},
    {"C-Stick/Up", PadButton::RightUp}, {"C-Stick/Down", PadButton::RightDown},
    {"C-Stick/Left", PadButton::RightLeft}, {"C-Stick/Right", PadButton::RightRight},
    {"Triggers/L", PadButton::L2}, {"Triggers/R", PadButton::R2},
    {"Triggers/L-Analog", PadButton::L2}, {"Triggers/R-Analog", PadButton::R2},
    {"D-Pad/Up", PadButton::Up}, {"D-Pad/Down", PadButton::Down},
    {"D-Pad/Left", PadButton::Left}, {"D-Pad/Right", PadButton::Right},
};

// A Wii Remote with a Nunchuk, the way most Wii games are played: the
// Nunchuk's stick on the left stick, the pointer on the right one, B (the
// Remote's trigger) on the right trigger, C and Z on the left shoulder and
// trigger. Home is left off: on a pad that is Guide, and Guide is OmniOS's.
const Bindings kDolphinWiimote = {
    {"Source", PadButton::Start, "1"},
    {"Buttons/A", PadButton::South}, {"Buttons/B", PadButton::R2}, {"Buttons/1", PadButton::West},
    {"Buttons/2", PadButton::North}, {"Buttons/-", PadButton::Select}, {"Buttons/+", PadButton::Start},
    {"D-Pad/Up", PadButton::Up}, {"D-Pad/Down", PadButton::Down},
    {"D-Pad/Left", PadButton::Left}, {"D-Pad/Right", PadButton::Right},
    {"IR/Up", PadButton::RightUp}, {"IR/Down", PadButton::RightDown},
    {"IR/Left", PadButton::RightLeft}, {"IR/Right", PadButton::RightRight},
    {"Shake/X", PadButton::East}, {"Shake/Y", PadButton::East}, {"Shake/Z", PadButton::East},
    {"Extension", PadButton::Start, "Nunchuk"},
    {"Nunchuk/Buttons/C", PadButton::L1}, {"Nunchuk/Buttons/Z", PadButton::L2},
    {"Nunchuk/Stick/Up", PadButton::LeftUp}, {"Nunchuk/Stick/Down", PadButton::LeftDown},
    {"Nunchuk/Stick/Left", PadButton::LeftLeft}, {"Nunchuk/Stick/Right", PadButton::LeftRight},
};

// Dolphin's SDL backend names a pad's controls by position too: "Button S"
// is the bottom face button on any pad.
const char* dolphinSdlControl(PadButton position) {
    switch (position) {
        case PadButton::Up:         return "Pad N";
        case PadButton::Down:       return "Pad S";
        case PadButton::Left:       return "Pad W";
        case PadButton::Right:      return "Pad E";
        case PadButton::South:      return "Button S";
        case PadButton::East:       return "Button E";
        case PadButton::West:       return "Button W";
        case PadButton::North:      return "Button N";
        case PadButton::L1:         return "Shoulder L";
        case PadButton::R1:         return "Shoulder R";
        case PadButton::L2:         return "Trigger L";
        case PadButton::R2:         return "Trigger R";
        case PadButton::L3:         return "Thumb L";
        case PadButton::R3:         return "Thumb R";
        case PadButton::Start:      return "Start";
        case PadButton::Select:     return "Back";
        case PadButton::LeftUp:     return "Left Y+";
        case PadButton::LeftDown:   return "Left Y-";
        case PadButton::LeftLeft:   return "Left X-";
        case PadButton::LeftRight:  return "Left X+";
        case PadButton::RightUp:    return "Right Y+";
        case PadButton::RightDown:  return "Right Y-";
        case PadButton::RightLeft:  return "Right X-";
        case PadButton::RightRight: return "Right X+";
    }
    return "";
}

// Dolphin's keyboard, which it reads through X11 (it runs under XWayland).
constexpr const char* kDolphinKeyboard = "XInput2/0/Virtual core pointer";

// Dolphin's name for a pad: backend, number among pads of that name, SDL's name.
std::string dolphinDevice(const Controller& pad) {
    return "SDL/0/" + pad.name;
}

// What a binding is, on the pad's device and on the keyboard's. A control on
// another device than the one the section is on is named with that device:
// so on the pad, the key is added with "|" (either one plays it).
std::string dolphinPadOnly(const DolphinBind& b) {
    return b.literal ? b.literal : "`" + std::string(dolphinSdlControl(b.position)) + "`";
}
std::string dolphinPadAndKeyboard(const DolphinBind& b) {
    if (b.literal) return b.literal;
    return dolphinPadOnly(b) + " | `" + kDolphinKeyboard + ":" + x11KeyName(keyFor(b.position)) + "`";
}
std::string dolphinKeyboard(const DolphinBind& b) {
    return b.literal ? b.literal : "`" + x11KeyName(keyFor(b.position)) + "`";
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

    std::string handler;  // none: nothing set up, RPCS3's default keyboard
    std::string device;
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
    const auto replace = [&](const std::string& player) {
        join(0, begin);
        out += player;
        join(end, lines.size());
        return out;
    };
    // No pad: the keyboard's layout, where player 1 was not set up or was on
    // a pad through SDL (one no longer there). A keyboard set up already is
    // left as it is: RPCS3's defaults are written only by RPCS3's own dialog.
    if (controllers.empty())
        return handler.empty() || handler == "SDL" ? replace(rpcs3Keyboard()) : std::string(yml);
    const Controller& pad = controllers.front();
    if (handler.empty() || handler == "Keyboard" || handler == "Null") return replace(rpcs3Player(pad));
    if (handler != "SDL" || deviceLine == lines.size()) return std::string(yml);
    for (const Controller& connected : controllers)
        if (device == rpcs3Device(connected)) return std::string(yml);
    lines[deviceLine] = "  Device: " + yamlQuoted(rpcs3Device(pad));
    join(0, lines.size());
    return out;
}

namespace {

// Player 1 on the keyboard, in the layout every emulator here shares
// (KeyboardLayout.h): a Pro Controller, its A the right-hand button.
Json ryubingKeyboard() {
    const auto object = [](std::map<std::string, Json> members) { return Json::object(std::move(members)); };
    const auto key = [](PadButton position) { return Json(ryujinxKeyName(keyFor(position))); };
    return object({
        {"left_joycon_stick", object({{"stick_up", key(PadButton::LeftUp)}, {"stick_down", key(PadButton::LeftDown)},
                                      {"stick_left", key(PadButton::LeftLeft)},
                                      {"stick_right", key(PadButton::LeftRight)},
                                      {"stick_button", key(PadButton::L3)}})},
        {"right_joycon_stick", object({{"stick_up", key(PadButton::RightUp)}, {"stick_down", key(PadButton::RightDown)},
                                       {"stick_left", key(PadButton::RightLeft)},
                                       {"stick_right", key(PadButton::RightRight)},
                                       {"stick_button", key(PadButton::R3)}})},
        {"left_joycon", object({{"button_minus", key(PadButton::Select)}, {"button_l", key(PadButton::L1)},
                                {"button_zl", key(PadButton::L2)}, {"button_sl", Json("Unbound")},
                                {"button_sr", Json("Unbound")}, {"dpad_up", key(PadButton::Up)},
                                {"dpad_down", key(PadButton::Down)}, {"dpad_left", key(PadButton::Left)},
                                {"dpad_right", key(PadButton::Right)}})},
        {"right_joycon", object({{"button_plus", key(PadButton::Start)}, {"button_r", key(PadButton::R1)},
                                 {"button_zr", key(PadButton::R2)}, {"button_sl", Json("Unbound")},
                                 {"button_sr", Json("Unbound")}, {"button_x", key(PadButton::North)},
                                 {"button_b", key(PadButton::South)}, {"button_y", key(PadButton::West)},
                                 {"button_a", key(PadButton::East)}})},
        {"version", Json(1.0)},
        {"backend", Json("WindowKeyboard")},
        {"id", Json("0")},
        {"name", Json("Keyboard")},
        {"controller_type", Json("ProController")},
        {"player_index", Json("Player1")},
    });
}

}  // namespace

std::string applyRyubingPad(std::string_view json, const std::vector<Controller>& controllers) {
    if (controllers.empty()) {
        // No pad: the keyboard's layout, where player 1 was on a pad (one no
        // longer there), missing, or still Ryubing's own keyboard default,
        // which has its A on Z. A keyboard the user set up is kept.
        std::string error;
        Json config = Json::parse(json, error);
        if (!error.empty() || !config.isObject()) return std::string(json);
        std::vector<Json> players = config["input_config"].items();
        Json* player1 = nullptr;
        for (Json& player : players)
            if (player["player_index"].asString() == "Player1") player1 = &player;
        const bool replace = player1 == nullptr || (*player1)["backend"].asString() == "GamepadSDL2" ||
                             ((*player1)["backend"].asString() == "WindowKeyboard" &&
                              (*player1)["right_joycon"]["button_a"].asString() == "Z");
        if (!replace) return std::string(json);
        if (player1 != nullptr) *player1 = ryubingKeyboard(); else players.push_back(ryubingKeyboard());
        config.set("input_config", Json::array(std::move(players)));
        return config.dump(2);
    }
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
    const Bindings& bindings = section.rfind("Wiimote", 0) == 0 ? kDolphinWiimote : kDolphinGameCube;

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

    // The section as it is: its device, and every other key's value.
    std::string device;  // none: nothing set up yet
    std::vector<std::pair<std::string, std::string>> values;
    for (std::size_t i = begin + 1; i < end && i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string key(trim(line.substr(0, eq)));
        const std::string value(trim(line.substr(eq + 1)));
        if (key == "Device") device = value;
        else values.emplace_back(key, value);
    }
    const bool sdl = device.rfind("SDL/", 0) == 0;
    const bool keyboard = device.rfind("XInput2/", 0) == 0;
    if (!device.empty() && !sdl && !keyboard) return std::string(ini);  // another backend: the user's

    // The pad when there is one, and the keyboard beside it. A pad that has
    // gone stays the device, its bindings naming the keyboard as well; with
    // no pad ever set up, the keyboard is the device.
    std::string target = device.empty() ? kDolphinKeyboard : device;
    if (!controllers.empty()) {
        bool connected = false;
        for (const Controller& pad : controllers) connected = connected || device == dolphinDevice(pad);
        if (!connected) target = dolphinDevice(controllers.front());
    }
    const bool onPad = target.rfind("SDL/", 0) == 0;
    // Moving from the keyboard (Dolphin's own default, or no setup at all)
    // to a pad sets every binding; otherwise only the ones still at a value
    // OmniOS wrote are brought up to date, and the user's own are kept.
    const bool fresh = device.empty() || (keyboard && onPad);

    std::string body;
    body += header + "\nDevice = " + target + "\n";
    std::vector<bool> used(values.size(), false);
    for (const DolphinBind& b : bindings) {
        const std::string mine = onPad ? dolphinPadAndKeyboard(b) : dolphinKeyboard(b);
        std::string value = mine;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (used[i] || values[i].first != b.control) continue;
            used[i] = true;
            const std::string& now = values[i].second;
            const bool ours = now == dolphinPadOnly(b) || now == dolphinPadAndKeyboard(b) || now == dolphinKeyboard(b);
            if (!fresh && !ours) value = now;
            break;
        }
        body += std::string(b.control) + " = " + value + "\n";
    }
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!used[i] && !fresh) body += values[i].first + " = " + values[i].second + "\n";

    std::string out;
    for (std::size_t i = 0; i < begin && i < lines.size(); ++i) out += lines[i] + "\n";
    out += body;
    for (std::size_t i = end; i < lines.size(); ++i) out += lines[i] + "\n";
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
        // 1's GameCube pad and Wii Remote both, since one Dolphin plays both;
        // with no pad, the keyboard's layout.
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
                   "[Controls]\nprofiles\\size=1\n", azaharSettings(!controllers.empty()));
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
        // Player 1 on the pad, or on the keyboard's layout when there is none.
        // With no file RPCS3 uses Default.yml, whatever the active
        // configuration says.
        const bool pad = updateFile(dir / "input_configs/global/Default.yml", "Player 1 Input:\n",
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
