#include <string>
#include <vector>

#include "Test.h"
#include "omnios/Detector.h"

using namespace omnios;

namespace {

// Builds a header of kHeaderSize bytes with `bytes` written at `offset`, which
// is how real dumps look to the detector: a signature inside a large file.
std::vector<std::uint8_t> headerWith(std::size_t offset,
                                     std::initializer_list<std::uint8_t> bytes) {
    std::vector<std::uint8_t> header(kHeaderSize, 0);
    std::size_t i = offset;
    for (const std::uint8_t byte : bytes) header[i++] = byte;
    return header;
}

std::string idOf(const Detection& detection) {
    return std::string(platformId(detection.platform));
}

}  // namespace

TEST("detector: PS4 package is identified by its container magic") {
    const Detection detection =
        detectHeader(headerWith(0, {0x7F, 'C', 'N', 'T'}), "god-of-war.pkg");

    CHECK_EQ(idOf(detection), std::string("ps4"));
    CHECK_EQ(detection.format, std::string("ps4_pkg"));
    CHECK(detection.source == DetectionSource::Magic);
}

TEST("detector: PS3 package is not confused with the PS4 one") {
    const Detection detection =
        detectHeader(headerWith(0, {0x7F, 'P', 'K', 'G'}), "the-last-of-us.pkg");

    CHECK_EQ(idOf(detection), std::string("ps3"));
    CHECK_EQ(detection.format, std::string("ps3_pkg"));
}

TEST("detector: ELF OS/ABI separates a PS4 executable from a Linux one") {
    auto elf = headerWith(0, {0x7F, 'E', 'L', 'F', 2, 1, 1});

    elf[7] = 0x00;  // System V
    CHECK_EQ(idOf(detectHeader(elf, "game")), std::string("linux"));

    elf[7] = 0x09;  // FreeBSD — the PS4 toolchain
    const Detection orbis = detectHeader(elf, "eboot.bin");
    CHECK_EQ(idOf(orbis), std::string("ps4"));
    CHECK_EQ(orbis.format, std::string("elf_orbis"));
}

TEST("detector: Windows executable is identified by the MZ header") {
    const Detection detection = detectHeader(headerWith(0, {'M', 'Z'}), "setup.exe");
    CHECK_EQ(idOf(detection), std::string("windows"));
    CHECK_EQ(detection.format, std::string("pe"));
}

TEST("detector: Switch NSP and XCI carry different partition magic") {
    CHECK_EQ(idOf(detectHeader(headerWith(0, {'P', 'F', 'S', '0'}), "zelda.nsp")),
             std::string("switch"));
    CHECK_EQ(detectHeader(headerWith(0, {'P', 'F', 'S', '0'}), "zelda.nsp").format,
             std::string("nsp"));
    CHECK_EQ(detectHeader(headerWith(0, {'H', 'F', 'S', '0'}), "mario.xci").format,
             std::string("xci"));
}

TEST("detector: Wii is checked before GameCube so a Wii disc is not misread") {
    // A Wii image has its magic at 0x18; the GameCube slot at 0x1C is data.
    auto wii = headerWith(0x18, {0x5D, 0x1C, 0x9E, 0xA3});
    wii[0x1C] = 0xC2;  // plausible noise in the GameCube magic position
    CHECK_EQ(idOf(detectHeader(wii, "game.iso")), std::string("wii"));

    const auto gamecube = headerWith(0x1C, {0xC2, 0x33, 0x9F, 0x3D});
    CHECK_EQ(idOf(detectHeader(gamecube, "melee.iso")), std::string("gamecube"));
}

TEST("detector: an .opkg is reported as a container, not a platform") {
    const Detection detection =
        detectHeader(headerWith(0, {'P', 'K', 0x03, 0x04}), "god-of-war.opkg");

    CHECK(!detection.recognised());
    CHECK_EQ(detection.format, std::string("opkg"));
}

TEST("detector: an APK is a ZIP that the extension resolves") {
    const Detection detection =
        detectHeader(headerWith(0, {'P', 'K', 0x03, 0x04}), "genshin.apk");

    CHECK_EQ(idOf(detection), std::string("android"));
    CHECK_EQ(detection.format, std::string("apk"));
}

TEST("detector: a PlayStation ISO is recognised from its volume identifier") {
    auto iso = headerWith(0x8001, {'C', 'D', '0', '0', '1'});
    const std::string marker = "PLAYSTATION";
    for (std::size_t i = 0; i < marker.size(); ++i)
        iso[0x8300 + i] = static_cast<std::uint8_t>(marker[i]);

    CHECK_EQ(idOf(detectHeader(iso, "shadow-of-the-colossus.iso")), std::string("ps2"));
}

TEST("detector: an anonymous ISO falls back to the folder it sits in") {
    const auto iso = headerWith(0x8001, {'C', 'D', '0', '0', '1'});
    const Detection detection = detectHeader(iso, "disc.iso", Platform::GameCube);

    CHECK_EQ(idOf(detection), std::string("gamecube"));
    CHECK(detection.source == DetectionSource::Folder);
    CHECK(detection.confidence < 50);
}

TEST("detector: extension beats the folder hint") {
    const std::vector<std::uint8_t> nothing(64, 0);
    const Detection detection = detectHeader(nothing, "mario-kart.xci", Platform::PS2);

    CHECK_EQ(idOf(detection), std::string("switch"));
    CHECK(detection.source == DetectionSource::Extension);
}

TEST("detector: a .dol is a Wii program in the wii folder, a GameCube one elsewhere") {
    const std::vector<std::uint8_t> nothing(64, 0);
    CHECK_EQ(idOf(detectHeader(nothing, "boot.dol", Platform::Wii)), std::string("wii"));
    CHECK_EQ(idOf(detectHeader(nothing, "boot.dol", Platform::GameCube)), std::string("gamecube"));
    CHECK_EQ(idOf(detectHeader(nothing, "boot.dol")), std::string("gamecube"));
}

TEST("detector: Switch homebrew (.nro) is a Switch game") {
    const std::vector<std::uint8_t> nothing(64, 0);
    CHECK_EQ(idOf(detectHeader(nothing, "SuperHaxagon.nro")), std::string("switch"));
}

TEST("detector: an unidentifiable file is reported, not guessed") {
    const std::vector<std::uint8_t> nothing(64, 0);
    const Detection detection = detectHeader(nothing, "notes");

    CHECK(!detection.recognised());
    CHECK_EQ(detection.confidence, 0);
    CHECK(!detection.evidence.empty());
}

TEST("detector: extensions are lowercased and directories are not extensions") {
    CHECK_EQ(fileExtension("GAME.ISO"), std::string("iso"));
    CHECK_EQ(fileExtension("archive.tar.gz"), std::string("gz"));
    CHECK_EQ(fileExtension("noext"), std::string());
    CHECK_EQ(fileExtension("trailing."), std::string());
    CHECK_EQ(fileExtension("some.dir/file"), std::string());
}
