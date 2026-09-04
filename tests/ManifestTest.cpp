#include "Test.h"
#include "omnios/Manifest.h"

using namespace omnios;

namespace {

// The manifest from the design doc (OmniOS.md §8), used as the reference case.
constexpr const char* kGodOfWar = R"({
  "omni_version": "1.0",
  "id": "com.publisher.godofwar",
  "title": "God of War",
  "version": "1.0.0",
  "platform": "ps4",
  "developer": "Santa Monica Studio",
  "publisher": "Sony",
  "description": "Kratos and Atreus journey through Norse mythology.",
  "executable": "game/eboot.bin",
  "install_size_mb": 45000,
  "checksum": {
    "algorithm": "sha256",
    "value": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
  },
  "compatibility": {
    "tier": "api_layer",
    "engine": "shadps4",
    "notes": "Runs at ~90% native speed"
  },
  "tags": ["action", "adventure", "singleplayer"]
})";

bool hasSubstring(const std::vector<std::string>& lines, std::string_view text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST("manifest: reads every field of the reference package") {
    const ManifestResult result = parseManifest(kGodOfWar);

    CHECK(result.ok());
    CHECK(result.warnings.empty());

    const Manifest& manifest = result.manifest;
    CHECK_EQ(manifest.id, std::string("com.publisher.godofwar"));
    CHECK_EQ(manifest.title, std::string("God of War"));
    CHECK_EQ(std::string(platformId(manifest.platform)), std::string("ps4"));
    CHECK_EQ(manifest.executable, std::string("game/eboot.bin"));
    CHECK_EQ(manifest.installSizeMb, std::uint64_t(45000));
    CHECK_EQ(manifest.checksum.algorithm, std::string("sha256"));
    CHECK(manifest.compatibility.tier == Tier::ApiLayer);
    CHECK_EQ(manifest.compatibility.engine, std::string("shadps4"));
    CHECK_EQ(manifest.tags.size(), std::size_t(3));
}

TEST("manifest: title and platform are required") {
    const ManifestResult noTitle = parseManifest(R"({"platform": "ps4"})");
    CHECK(!noTitle.ok());
    CHECK(hasSubstring(noTitle.errors, "title"));

    const ManifestResult noPlatform = parseManifest(R"({"title": "Untitled"})");
    CHECK(!noPlatform.ok());
    CHECK(hasSubstring(noPlatform.errors, "platform"));
}

TEST("manifest: an unknown platform is an error, not a silent Unknown") {
    const ManifestResult result =
        parseManifest(R"({"title": "Halo", "platform": "xbox360"})");

    CHECK(!result.ok());
    CHECK(hasSubstring(result.errors, "xbox360"));
}

TEST("manifest: an executable escaping the package is rejected") {
    // Written as JSON string bodies: the Windows case needs its backslashes
    // escaped, or the manifest fails as malformed JSON before the path is read.
    for (const char* escape : {"../../etc/passwd", "/usr/bin/sh", "C:\\\\Windows\\\\cmd.exe",
                               "game/../../outside"}) {
        const std::string text = std::string(R"({"title":"t","platform":"linux","executable":")") +
                                 escape + R"("})";
        const ManifestResult result = parseManifest(text);
        CHECK(!result.ok());
        CHECK(hasSubstring(result.errors, "relative path"));
    }
}

TEST("manifest: a nested but contained executable path is accepted") {
    const ManifestResult result = parseManifest(
        R"({"title":"t","platform":"linux","executable":"game/bin/start.sh"})");
    CHECK(result.ok());
}

TEST("manifest: missing optional fields warn but still install") {
    const ManifestResult result = parseManifest(R"({"title":"Tetris","platform":"retro"})");

    CHECK(result.ok());
    CHECK(hasSubstring(result.warnings, "checksum"));
    CHECK(hasSubstring(result.warnings, "install_size_mb"));
    CHECK(hasSubstring(result.warnings, "id"));
    // Tier falls back to the platform's own tier.
    CHECK(result.manifest.compatibility.tier == Tier::Emulator);
}

TEST("manifest: a malformed checksum warns instead of blocking the install") {
    const ManifestResult result = parseManifest(
        R"({"title":"t","platform":"ps2","checksum":{"algorithm":"sha256","value":"abc"}})");

    CHECK(result.ok());
    CHECK(hasSubstring(result.warnings, "sha256 digest"));
}

TEST("manifest: a newer omni_version is read, with a warning") {
    const ManifestResult result =
        parseManifest(R"({"omni_version":"2.0","title":"t","platform":"ps2"})");

    CHECK(result.ok());
    CHECK(hasSubstring(result.warnings, "omni_version"));
}

TEST("manifest: invalid JSON reports the parse error") {
    const ManifestResult result = parseManifest(R"({"title": )");

    CHECK(!result.ok());
    CHECK(hasSubstring(result.errors, "not valid JSON"));
}

TEST("manifest: survives a write and read round trip") {
    const ManifestResult first = parseManifest(kGodOfWar);
    const ManifestResult second = parseManifest(first.manifest.toJson().dump(2));

    CHECK(second.ok());
    CHECK_EQ(second.manifest.title, first.manifest.title);
    CHECK_EQ(second.manifest.executable, first.manifest.executable);
    CHECK_EQ(second.manifest.installSizeMb, first.manifest.installSizeMb);
    CHECK_EQ(second.manifest.tags.size(), first.manifest.tags.size());
    CHECK(second.manifest.compatibility.tier == first.manifest.compatibility.tier);
}
