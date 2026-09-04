// Minimal JSON value + recursive-descent parser.
//
// OmniOS reads manifests off removable media and writes its own library cache,
// so the parser is deliberately small and total: it never throws and never
// aborts on malformed input, it returns an error string. Anything a manifest
// needs is supported (objects, arrays, strings with escapes and \uXXXX,
// numbers, bool, null); nothing else is.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace omnios {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    explicit Json(bool value) : type_(Type::Bool), bool_(value) {}
    explicit Json(double value) : type_(Type::Number), number_(value) {}
    explicit Json(std::string value) : type_(Type::String), string_(std::move(value)) {}
    explicit Json(const char* value) : type_(Type::String), string_(value) {}

    static Json array(std::vector<Json> items);
    static Json object(std::map<std::string, Json> members);

    // Parses `text`. On failure returns a Null value and fills `error`.
    static Json parse(std::string_view text, std::string& error);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBool() const { return type_ == Type::Bool; }

    // Typed accessors. Each returns `fallback` when this value is absent or of
    // the wrong type, so callers can read a whole manifest without a single
    // type check and still get sane defaults from a partial file.
    bool        asBool(bool fallback = false) const;
    double      asNumber(double fallback = 0.0) const;
    std::string asString(std::string fallback = {}) const;

    // Object member lookup; returns a Null value if absent or not an object.
    const Json& operator[](std::string_view key) const;
    bool        contains(std::string_view key) const;

    // Array access; empty when this is not an array.
    const std::vector<Json>& items() const;

    void set(std::string key, Json value);
    void push(Json value);

    // Serialises. indent < 0 emits compact output, otherwise pretty-prints.
    std::string dump(int indent = -1) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type                       type_ = Type::Null;
    bool                       bool_ = false;
    double                     number_ = 0.0;
    std::string                string_;
    std::vector<Json>          array_;
    std::map<std::string, Json> object_;
};

}  // namespace omnios
