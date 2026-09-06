// Binary detector — Phase 9.1 (OmniOS.md §11).
//
// Answers one question: given a file on disk, what platform is this?
// Magic bytes decide it wherever a format has them, because a user who renames
// a dump or drops it in the wrong folder should still get a working tile. The
// filename extension and the containing ~/Games/ folder are fallbacks only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Platform.h"

namespace omnios {

// How the detector reached its answer. Reported in the UI when a title fails to
// launch, so a user can see whether OmniOS actually recognised the file or just
// guessed from the folder it was sitting in.
enum class DetectionSource : std::uint8_t {
    None = 0,
    Magic,      // format signature in the file header — trustworthy
    Extension,  // filename only
    Folder,     // position under ~/Games/ only — weakest
    // The platform's own records, which is as good as it gets: Steam knows
    // what it installed, its app id and its real title, so nothing here had to
    // be inferred. Last in the list so the stored numbers of the others do not
    // move under an existing library cache.
    Manifest,
};

std::string_view detectionSourceName(DetectionSource source);

struct Detection {
    Platform        platform = Platform::Unknown;
    DetectionSource source   = DetectionSource::None;
    // Container/executable format id, e.g. "ps4_pkg", "nsp", "pe", "elf".
    std::string     format;
    // Human-readable reason, shown in the launcher's error path.
    std::string     evidence;
    // 0-100. Magic hits score high, a bare folder guess scores low.
    int             confidence = 0;

    bool recognised() const { return platform != Platform::Unknown; }
};

// Bytes of a file header the detector needs. Covers the ISO 9660 volume
// descriptor at 0x8001, which is the deepest signature we look for.
inline constexpr std::size_t kHeaderSize = 0x9000;

// Detects from an in-memory header. `filename` supplies the extension fallback
// and may be empty. `folderHint` is the platform implied by the file's location
// under ~/Games/, used only to break ties the bytes cannot.
Detection detectHeader(const std::vector<std::uint8_t>& header,
                       std::string_view filename,
                       Platform folderHint = Platform::Unknown);

// Reads up to kHeaderSize bytes from `path` and detects. A directory is
// inspected for the marker files that identify an installed title
// (manifest.json, eboot.bin, a PS3 PS3_GAME tree, ...).
Detection detectFile(const std::filesystem::path& path,
                     Platform folderHint = Platform::Unknown);

// Lowercased extension without the dot; empty when there is none.
std::string fileExtension(std::string_view filename);

// Extension-only mapping, exposed for the installer's fallback table
// (OmniOS.md §8). Returns Unknown for extensions shared across platforms.
Platform platformFromExtension(std::string_view extension);

}  // namespace omnios
