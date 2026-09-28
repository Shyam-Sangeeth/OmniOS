#include "Detector.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <initializer_list>

namespace omnios {
namespace {

namespace fs = std::filesystem;

bool matchAt(const std::vector<std::uint8_t>& header, std::size_t offset,
             std::initializer_list<std::uint8_t> signature) {
    if (offset + signature.size() > header.size()) return false;
    std::size_t i = offset;
    for (const std::uint8_t byte : signature) {
        if (header[i++] != byte) return false;
    }
    return true;
}

bool matchAscii(const std::vector<std::uint8_t>& header, std::size_t offset,
                std::string_view text) {
    if (offset + text.size() > header.size()) return false;
    return std::memcmp(header.data() + offset, text.data(), text.size()) == 0;
}

// Searches the header for an ASCII marker. Used for volume identifiers inside
// ISO images, which sit at a fixed offset in theory and drift in practice.
bool containsAscii(const std::vector<std::uint8_t>& header, std::string_view text) {
    if (text.empty() || header.size() < text.size()) return false;
    const auto* begin = reinterpret_cast<const char*>(header.data());
    return std::search(begin, begin + header.size(), text.begin(), text.end()) !=
           begin + header.size();
}

Detection make(Platform platform, DetectionSource source, std::string format,
               std::string evidence, int confidence) {
    Detection detection;
    detection.platform   = platform;
    detection.source     = source;
    detection.format     = std::move(format);
    detection.evidence   = std::move(evidence);
    detection.confidence = confidence;
    return detection;
}

// ELF OS/ABI byte (e_ident[EI_OSABI], offset 7). The PS4 toolchain targets
// FreeBSD, which is what separates a PS4 executable from a Linux one — they are
// otherwise the same x86-64 ELF (OmniOS.md §16, Tier 1B).
constexpr std::size_t kElfOsAbiOffset = 7;
constexpr std::uint8_t kElfAbiSystemV = 0x00;
constexpr std::uint8_t kElfAbiLinux   = 0x03;
constexpr std::uint8_t kElfAbiFreeBsd = 0x09;

Detection detectElf(const std::vector<std::uint8_t>& header) {
    const std::uint8_t abi =
        header.size() > kElfOsAbiOffset ? header[kElfOsAbiOffset] : kElfAbiSystemV;
    if (abi == kElfAbiFreeBsd) {
        return make(Platform::PS4, DetectionSource::Magic, "elf_orbis",
                    "ELF with FreeBSD OS/ABI (Orbis executable)", 90);
    }
    if (abi == kElfAbiSystemV || abi == kElfAbiLinux) {
        return make(Platform::Linux, DetectionSource::Magic, "elf",
                    "ELF executable", 95);
    }
    return make(Platform::Linux, DetectionSource::Magic, "elf",
                "ELF executable with unrecognised OS/ABI", 60);
}

// Signature table, checked in order. Everything here is a positive
// identification; ambiguous formats (ISO, ZIP) are handled after this runs.
Detection detectSignature(const std::vector<std::uint8_t>& header) {
    if (matchAt(header, 0, {0x7F, 0x45, 0x4C, 0x46}))  // \x7FELF
        return detectElf(header);

    if (matchAt(header, 0, {0x4D, 0x5A}))  // MZ
        return make(Platform::Windows, DetectionSource::Magic, "pe",
                    "DOS/PE executable header", 95);

    if (matchAt(header, 0, {0x7F, 0x43, 0x4E, 0x54}))  // \x7FCNT
        return make(Platform::PS4, DetectionSource::Magic, "ps4_pkg",
                    "PS4 package container", 98);

    if (matchAt(header, 0, {0x4F, 0x15, 0x3D, 0x1D}))
        return make(Platform::PS4, DetectionSource::Magic, "self",
                    "Signed ELF (PS4/PS5 SELF)", 85);

    if (matchAt(header, 0, {0x7F, 0x50, 0x4B, 0x47}))  // \x7FPKG
        return make(Platform::PS3, DetectionSource::Magic, "ps3_pkg",
                    "PS3 package container", 98);

    if (matchAscii(header, 0, "PFS0"))
        return make(Platform::Switch, DetectionSource::Magic, "nsp",
                    "Switch NSP (PFS0 partition)", 98);

    if (matchAscii(header, 0, "HFS0"))
        return make(Platform::Switch, DetectionSource::Magic, "xci",
                    "Switch XCI (HFS0 partition)", 98);

    // Wii and GameCube disc headers carry different magic at different offsets;
    // Wii is checked first because a Wii image also has bytes at 0x1C.
    if (matchAt(header, 0x18, {0x5D, 0x1C, 0x9E, 0xA3}))
        return make(Platform::Wii, DetectionSource::Magic, "wii_disc",
                    "Wii disc magic at 0x18", 98);

    if (matchAt(header, 0x1C, {0xC2, 0x33, 0x9F, 0x3D}))
        return make(Platform::GameCube, DetectionSource::Magic, "gc_disc",
                    "GameCube disc magic at 0x1C", 98);

    if (matchAt(header, 0, {0x4E, 0x45, 0x53, 0x1A}))  // NES\x1A
        return make(Platform::Retro, DetectionSource::Magic, "ines",
                    "iNES ROM header", 95);

    if (matchAt(header, 0, {0x80, 0x37, 0x12, 0x40}))
        return make(Platform::Retro, DetectionSource::Magic, "n64_rom",
                    "Nintendo 64 ROM (big-endian)", 90);

    // The Nintendo logo at 0x04 is checked by the GBA BIOS itself, so every
    // bootable cartridge image has it.
    if (matchAt(header, 0x04, {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21}))
        return make(Platform::GBA, DetectionSource::Magic, "gba_rom",
                    "GBA cartridge logo at 0x04", 95);

    return {};
}

// ISO 9660 images all share one signature, so the platform has to come from the
// volume contents. Anything still ambiguous is left Unknown for the folder hint.
Detection detectIso(const std::vector<std::uint8_t>& header) {
    if (!matchAscii(header, 0x8001, "CD001")) return {};

    if (containsAscii(header, "PLAYSTATION"))
        return make(Platform::PS2, DetectionSource::Magic, "iso9660",
                    "ISO 9660 with PlayStation volume identifier", 70);

    return make(Platform::Unknown, DetectionSource::Magic, "iso9660",
                "ISO 9660 image of an undetermined platform", 0);
}

// ZIP-based containers. The extension is what separates them, since an .opkg,
// an .apk and a plain archive are the same bytes at offset 0.
Detection detectZip(const std::vector<std::uint8_t>& header,
                    std::string_view extension) {
    const bool zip = matchAt(header, 0, {0x50, 0x4B, 0x03, 0x04}) ||
                     matchAt(header, 0, {0x50, 0x4B, 0x05, 0x06}) ||
                     matchAt(header, 0, {0x50, 0x4B, 0x07, 0x08});
    if (!zip) return {};

    if (extension == "opkg")
        return make(Platform::Unknown, DetectionSource::Magic, "opkg",
                    "OmniOS package (platform comes from its manifest)", 0);
    if (extension == "apk")
        return make(Platform::Android, DetectionSource::Magic, "apk",
                    "Android package", 90);
    return make(Platform::Unknown, DetectionSource::Magic, "zip",
                "ZIP archive; contents must be scanned", 0);
}

struct ExtensionRow {
    std::string_view extension;
    Platform         platform;
    std::string_view format;
};

// Extension fallback (OmniOS.md §8). Extensions shared between platforms
// (.iso, .bin, .zip, .pkg) are deliberately absent — those need the bytes.
const ExtensionRow kExtensions[] = {
    {"exe",  Platform::Windows,  "pe"},
    {"msi",  Platform::Windows,  "windows_installer"},
    {"nsp",  Platform::Switch,   "nsp"},
    {"xci",  Platform::Switch,   "xci"},
    {"nca",  Platform::Switch,   "nca"},
    {"nro",  Platform::Switch,   "nro"},  // homebrew
    {"apk",  Platform::Android,  "apk"},
    {"apks", Platform::Android,  "apk_split"},
    {"xapk", Platform::Android,  "apk_split"},
    {"gcm",  Platform::GameCube, "gc_disc"},
    {"dol",  Platform::GameCube, "dol"},
    {"rvz",  Platform::Wii,      "rvz"},
    {"wbfs", Platform::Wii,      "wbfs"},
    {"wad",  Platform::Wii,      "wad"},
    {"3ds",  Platform::N3DS,     "3ds_rom"},
    {"cia",  Platform::N3DS,     "cia"},
    {"cci",  Platform::N3DS,     "3ds_rom"},
    {"3dsx", Platform::N3DS,     "3ds_homebrew"},
    {"z3dsx", Platform::N3DS,    "3ds_homebrew"},
    {"zcci", Platform::N3DS,     "3ds_rom"},
    {"zcia", Platform::N3DS,     "cia"},
    {"cxi",  Platform::N3DS,     "ncch"},
    {"gba",  Platform::GBA,      "gba_rom"},
    {"gb",   Platform::GBA,      "gb_rom"},
    {"gbc",  Platform::GBA,      "gbc_rom"},
    {"nds",  Platform::GBA,      "nds_rom"},
    {"nes",  Platform::Retro,    "ines"},
    {"sfc",  Platform::Retro,    "snes_rom"},
    {"smc",  Platform::Retro,    "snes_rom"},
    {"n64",  Platform::Retro,    "n64_rom"},
    {"z64",  Platform::Retro,    "n64_rom"},
    {"v64",  Platform::Retro,    "n64_rom"},
    {"md",   Platform::Retro,    "genesis_rom"},
    {"gen",  Platform::Retro,    "genesis_rom"},
    {"smd",  Platform::Retro,    "genesis_rom"},
    {"sms",  Platform::Retro,    "sms_rom"},
    {"gg",   Platform::Retro,    "gg_rom"},
    {"pbp",  Platform::PS1,      "pbp"},
    {"chd",  Platform::PS1,      "chd"},
    {"cue",  Platform::PS1,      "cue"},
    {"self", Platform::PS4,      "self"},
    {"sprx", Platform::PS3,      "sprx"},
    {"appimage", Platform::Linux, "appimage"},
    {"sh",   Platform::Linux,    "shell_script"},
};

}  // namespace

std::string_view detectionSourceName(DetectionSource source) {
    switch (source) {
        case DetectionSource::None:      return "none";
        case DetectionSource::Magic:     return "magic";
        case DetectionSource::Extension: return "extension";
        case DetectionSource::Folder:    return "folder";
        case DetectionSource::Manifest:  return "manifest";
    }
    return "none";
}

std::string fileExtension(std::string_view filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 == filename.size()) return {};
    // A dot in a directory component is not an extension.
    if (filename.find_first_of("/\\", dot) != std::string_view::npos) return {};
    std::string extension(filename.substr(dot + 1));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

Platform platformFromExtension(std::string_view extension) {
    for (const ExtensionRow& row : kExtensions) {
        if (row.extension == extension) return row.platform;
    }
    return Platform::Unknown;
}

Detection detectHeader(const std::vector<std::uint8_t>& header,
                       std::string_view filename, Platform folderHint) {
    const std::string extension = fileExtension(filename);

    Detection detection = detectSignature(header);
    if (detection.recognised()) return detection;

    if (Detection zip = detectZip(header, extension); zip.source != DetectionSource::None) {
        // .opkg and plain archives are containers: the caller unpacks them and
        // detects again. Report the container rather than guessing a platform.
        if (zip.recognised() || zip.format == "opkg" || zip.format == "zip")
            return zip;
    }

    if (Detection iso = detectIso(header); iso.source != DetectionSource::None) {
        if (iso.recognised()) return iso;
        detection = iso;  // known container, unknown platform — keep refining
    }

    if (Platform byExtension = platformFromExtension(extension); byExtension != Platform::Unknown) {
        // A .dol is a GameCube or a Wii program alike; which one, only the
        // folder it was put in says. (Dolphin tells them apart by itself.)
        if (extension == "dol" && folderHint == Platform::Wii) byExtension = Platform::Wii;
        std::string format(detection.format);
        for (const ExtensionRow& row : kExtensions) {
            if (row.extension == extension) { format = std::string(row.format); break; }
        }
        return make(byExtension, DetectionSource::Extension, std::move(format),
                    "filename extension ." + extension, 55);
    }

    if (folderHint != Platform::Unknown) {
        return make(folderHint, DetectionSource::Folder,
                    detection.format.empty() ? "unknown" : detection.format,
                    "stored under ~/Games/" + std::string(platformFolder(folderHint)),
                    30);
    }

    if (detection.source != DetectionSource::None) return detection;

    return make(Platform::Unknown, DetectionSource::None, "unknown",
                "no signature, extension or folder matched", 0);
}

Detection detectFile(const fs::path& path, Platform folderHint) {
    std::error_code ec;

    if (fs::is_directory(path, ec)) {
        // An installed title is a directory. Look for the markers that name it.
        if (fs::exists(path / "manifest.json", ec))
            return make(Platform::Unknown, DetectionSource::Magic, "opkg_dir",
                        "installed .opkg (platform comes from its manifest)", 0);
        if (fs::exists(path / "eboot.bin", ec))
            return make(Platform::PS4, DetectionSource::Magic, "ps4_dir",
                        "directory containing eboot.bin", 80);
        if (fs::exists(path / "PS3_GAME", ec))
            return make(Platform::PS3, DetectionSource::Magic, "ps3_dir",
                        "directory containing PS3_GAME", 90);
        if (folderHint != Platform::Unknown)
            return make(folderHint, DetectionSource::Folder, "directory",
                        "directory under ~/Games/" +
                            std::string(platformFolder(folderHint)),
                        30);
        return make(Platform::Unknown, DetectionSource::None, "directory",
                    "directory with no recognised marker file", 0);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return make(Platform::Unknown, DetectionSource::None, "unknown",
                    "could not open " + path.filename().string(), 0);
    }

    std::vector<std::uint8_t> header(kHeaderSize);
    file.read(reinterpret_cast<char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(file.gcount()));

    return detectHeader(header, path.filename().string(), folderHint);
}

}  // namespace omnios
