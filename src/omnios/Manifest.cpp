#include "Manifest.h"

#include <algorithm>
#include <cctype>

namespace omnios {
namespace {

Tier tierFromId(std::string_view id, bool& known) {
    known = true;
    if (id == "native")    return Tier::Native;
    if (id == "api_layer") return Tier::ApiLayer;
    if (id == "jit")       return Tier::Jit;
    if (id == "emulator")  return Tier::Emulator;
    known = false;
    return Tier::Native;
}

bool isHex(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

// An executable path inside a package must stay inside the package. Reject
// absolute paths and any ".." component so a crafted manifest cannot make the
// installer point at a file outside the extracted tree.
bool isSafeRelativePath(std::string_view path) {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() > 1 && path[1] == ':') return false;  // C:\...

    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string_view part =
            path.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                             : end - start);
        if (part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

}  // namespace

Json Manifest::toJson() const {
    Json out;
    out.set("omni_version", Json(omniVersion.empty() ? std::string(kOmniManifestVersion)
                                                     : omniVersion));
    out.set("id", Json(id));
    out.set("title", Json(title));
    out.set("version", Json(version));
    // Prefer the string the package actually carried, so an unrecognised
    // platform survives a read/write round trip instead of being erased.
    out.set("platform", Json(platformId.empty()
                                 ? std::string(omnios::platformId(platform))
                                 : platformId));
    if (!developer.empty())   out.set("developer", Json(developer));
    if (!publisher.empty())   out.set("publisher", Json(publisher));
    if (!description.empty()) out.set("description", Json(description));
    if (!executable.empty())  out.set("executable", Json(executable));
    out.set("install_size_mb", Json(static_cast<double>(installSizeMb)));

    if (!checksum.empty()) {
        Json sum;
        sum.set("algorithm", Json(checksum.algorithm));
        sum.set("value", Json(checksum.value));
        out.set("checksum", std::move(sum));
    }

    Json compat;
    compat.set("tier", Json(std::string(tierName(compatibility.tier))));
    if (!compatibility.engine.empty()) compat.set("engine", Json(compatibility.engine));
    if (!compatibility.notes.empty())  compat.set("notes", Json(compatibility.notes));
    out.set("compatibility", std::move(compat));

    if (!tags.empty()) {
        Json list;
        for (const std::string& tag : tags) list.push(Json(tag));
        out.set("tags", std::move(list));
    }
    return out;
}

ManifestResult parseManifest(std::string_view text) {
    ManifestResult result;

    std::string error;
    const Json root = Json::parse(text, error);
    if (!error.empty()) {
        result.errors.push_back("manifest.json is not valid JSON (" + error + ")");
        return result;
    }
    if (!root.isObject()) {
        result.errors.push_back("manifest.json must contain a JSON object");
        return result;
    }

    Manifest& manifest = result.manifest;

    manifest.omniVersion = root["omni_version"].asString();
    if (manifest.omniVersion.empty()) {
        result.warnings.push_back("omni_version missing; assuming " +
                                  std::string(kOmniManifestVersion));
        manifest.omniVersion = std::string(kOmniManifestVersion);
    } else if (manifest.omniVersion != kOmniManifestVersion) {
        // Forward compatibility: read what we understand and say what we did.
        result.warnings.push_back("manifest declares omni_version " +
                                  manifest.omniVersion + "; this build reads " +
                                  std::string(kOmniManifestVersion));
    }

    manifest.title = root["title"].asString();
    if (manifest.title.empty()) result.errors.push_back("title is required");

    manifest.id = root["id"].asString();
    if (manifest.id.empty()) {
        result.warnings.push_back("id missing; one will be derived from the title");
    }

    manifest.platformId = root["platform"].asString();
    if (manifest.platformId.empty()) {
        result.errors.push_back("platform is required");
    } else {
        manifest.platform = platformFromId(manifest.platformId);
        if (manifest.platform == Platform::Unknown)
            result.errors.push_back("unknown platform \"" + manifest.platformId + "\"");
    }

    manifest.version     = root["version"].asString();
    manifest.developer   = root["developer"].asString();
    manifest.publisher   = root["publisher"].asString();
    manifest.description = root["description"].asString();

    manifest.executable = root["executable"].asString();
    if (!manifest.executable.empty() && !isSafeRelativePath(manifest.executable))
        result.errors.push_back("executable must be a relative path inside the package");

    const double sizeMb = root["install_size_mb"].asNumber(-1.0);
    if (sizeMb < 0.0) {
        result.warnings.push_back("install_size_mb missing; the installer will "
                                  "measure the extracted files instead");
    } else {
        manifest.installSizeMb = static_cast<std::uint64_t>(sizeMb);
    }

    const Json& checksum = root["checksum"];
    if (checksum.isObject()) {
        manifest.checksum.algorithm = checksum["algorithm"].asString("sha256");
        manifest.checksum.value     = checksum["value"].asString();
        if (manifest.checksum.algorithm != "sha256") {
            result.warnings.push_back("unsupported checksum algorithm \"" +
                                      manifest.checksum.algorithm +
                                      "\"; integrity will not be verified");
        } else if (manifest.checksum.value.size() != 64 ||
                   !isHex(manifest.checksum.value)) {
            result.warnings.push_back("checksum value is not a 64-character sha256 "
                                      "digest; integrity will not be verified");
        }
    } else {
        result.warnings.push_back("no checksum; package integrity cannot be verified");
    }

    const Json& compat = root["compatibility"];
    if (compat.isObject()) {
        const std::string tierId = compat["tier"].asString();
        if (!tierId.empty()) {
            bool known = false;
            const Tier tier = tierFromId(tierId, known);
            if (known) {
                manifest.compatibility.tier = tier;
            } else {
                result.warnings.push_back("unknown compatibility tier \"" + tierId +
                                          "\"; the router will choose one");
            }
        }
        manifest.compatibility.engine = compat["engine"].asString();
        manifest.compatibility.notes  = compat["notes"].asString();
    }
    if (!compat.isObject() || compat["tier"].asString().empty()) {
        // Fall back to the platform's own tier so the field is always usable.
        if (manifest.platform != Platform::Unknown)
            manifest.compatibility.tier = platformTier(manifest.platform);
    }

    for (const Json& tag : root["tags"].items()) {
        if (tag.isString()) manifest.tags.push_back(tag.asString());
    }

    return result;
}

}  // namespace omnios
