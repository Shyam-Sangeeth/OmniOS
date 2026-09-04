#include "Test.h"
#include "omnios/Json.h"

using omnios::Json;

TEST("json: parses a manifest-shaped document") {
    std::string error;
    const Json root = Json::parse(R"({
        "title": "God of War",
        "install_size_mb": 45000,
        "compatibility": { "tier": "api_layer", "engine": "shadps4" },
        "tags": ["action", "adventure"],
        "verified": true,
        "notes": null
    })", error);

    CHECK_EQ(error, std::string());
    CHECK(root.isObject());
    CHECK_EQ(root["title"].asString(), std::string("God of War"));
    CHECK_EQ(root["install_size_mb"].asNumber(), 45000.0);
    CHECK_EQ(root["compatibility"]["engine"].asString(), std::string("shadps4"));
    CHECK_EQ(root["tags"].items().size(), std::size_t(2));
    CHECK_EQ(root["tags"].items()[1].asString(), std::string("adventure"));
    CHECK(root["verified"].asBool());
    CHECK(root["notes"].isNull());
}

TEST("json: missing members read as their fallback rather than throwing") {
    std::string error;
    const Json root = Json::parse(R"({"title": "Bloodborne"})", error);

    CHECK_EQ(error, std::string());
    CHECK_EQ(root["nope"].asString("fallback"), std::string("fallback"));
    CHECK_EQ(root["nope"]["deeper"].asNumber(7.0), 7.0);
    CHECK(root["title"].asNumber(-1.0) == -1.0);  // wrong type, not a crash
    CHECK(!root.contains("nope"));
}

TEST("json: reports malformed input instead of half-parsing it") {
    const char* const broken[] = {
        "{",
        R"({"a": })",
        R"({"a": 1,})",
        R"({"a" 1})",
        R"(["unterminated)",
        R"({"a": 1} trailing)",
        R"({"a": 0x10})",
    };
    for (const char* text : broken) {
        std::string error;
        const Json value = Json::parse(text, error);
        CHECK(!error.empty());
        CHECK(value.isNull());
    }
}

TEST("json: decodes escapes and surrogate pairs") {
    std::string error;
    const Json root = Json::parse(R"({"t": "a\"b\\c\ndé🎮"})", error);

    CHECK_EQ(error, std::string());
    const std::string value = root["t"].asString();
    CHECK_EQ(value, std::string("a\"b\\c\nd\xc3\xa9\xf0\x9f\x8e\xae"));
}

TEST("json: round-trips through dump and parse") {
    Json original;
    original.set("title", Json("Zelda: Tears of the Kingdom"));
    original.set("size", Json(16000.0));
    original.set("nested", Json::object({{"tier", Json("jit")}}));
    Json tags;
    tags.push(Json("adventure"));
    original.set("tags", std::move(tags));

    std::string error;
    const Json reparsed = Json::parse(original.dump(2), error);

    CHECK_EQ(error, std::string());
    CHECK_EQ(reparsed["title"].asString(), std::string("Zelda: Tears of the Kingdom"));
    CHECK_EQ(reparsed["size"].asNumber(), 16000.0);
    CHECK_EQ(reparsed["nested"]["tier"].asString(), std::string("jit"));
    CHECK_EQ(reparsed["tags"].items().size(), std::size_t(1));
}

TEST("json: whole numbers dump without a decimal tail") {
    Json value;
    value.set("size", Json(45000.0));
    CHECK_EQ(value.dump(), std::string(R"({"size":45000})"));
}

TEST("json: refuses input nested past the depth limit") {
    std::string text(200, '[');
    std::string error;
    const Json value = Json::parse(text, error);
    CHECK(!error.empty());
    CHECK(value.isNull());
}
