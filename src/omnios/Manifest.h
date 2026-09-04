// .opkg manifest — Phase 6 (OmniOS.md §8, §19).
//
// A manifest arrives from outside the system, so parsing is defensive: every
// field is validated, and the result carries a list of problems rather than a
// single pass/fail. Warnings still install; errors do not.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"
#include "Platform.h"

namespace omnios {

struct Checksum {
    std::string algorithm;  // currently only "sha256"
    std::string value;

    bool empty() const { return value.empty(); }
};

struct Compatibility {
    Tier        tier = Tier::Native;
    std::string engine;  // engine id the packager recommends, may be empty
    std::string notes;
};

struct Manifest {
    std::string   omniVersion;
    std::string   id;
    std::string   title;
    std::string   version;
    Platform      platform = Platform::Unknown;
    std::string   platformId;  // as written, even when unrecognised
    std::string   developer;
    std::string   publisher;
    std::string   description;
    std::string   executable;
    std::uint64_t installSizeMb = 0;
    Checksum      checksum;
    Compatibility compatibility;
    std::vector<std::string> tags;

    Json toJson() const;
};

struct ManifestResult {
    Manifest                 manifest;
    std::vector<std::string> errors;    // block installation
    std::vector<std::string> warnings;  // install proceeds

    bool ok() const { return errors.empty(); }
};

// Parses manifest text. Never throws; a malformed document comes back with the
// parse error in `errors`.
ManifestResult parseManifest(std::string_view text);

// The manifest format version this build writes and knows how to read.
inline constexpr std::string_view kOmniManifestVersion = "1.0";

}  // namespace omnios
