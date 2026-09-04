#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace omnios {
namespace {

const Json kNull;
const std::vector<Json> kNoItems;

class Parser {
public:
    Parser(std::string_view text, std::string& error) : text_(text), error_(error) {}

    Json run() {
        skipWhitespace();
        Json value = parseValue(0);
        if (!error_.empty()) return {};
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("trailing characters after top-level value");
            return {};
        }
        return value;
    }

private:
    // Guards against a hostile manifest blowing the stack with nested arrays.
    static constexpr int kMaxDepth = 64;

    void fail(const std::string& message) {
        if (error_.empty())
            error_ = "at byte " + std::to_string(pos_) + ": " + message;
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool consume(char expected) {
        if (peek() != expected) return false;
        ++pos_;
        return true;
    }

    bool consumeLiteral(std::string_view literal) {
        if (text_.compare(pos_, literal.size(), literal) != 0) return false;
        pos_ += literal.size();
        return true;
    }

    Json parseValue(int depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
            return {};
        }
        if (atEnd()) {
            fail("unexpected end of input");
            return {};
        }
        switch (peek()) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': {
                std::string out;
                if (!parseString(out)) return {};
                return Json(std::move(out));
            }
            case 't':
                if (consumeLiteral("true")) return Json(true);
                fail("invalid literal");
                return {};
            case 'f':
                if (consumeLiteral("false")) return Json(false);
                fail("invalid literal");
                return {};
            case 'n':
                if (consumeLiteral("null")) return {};
                fail("invalid literal");
                return {};
            default: return parseNumber();
        }
    }

    Json parseObject(int depth) {
        ++pos_;  // '{'
        std::map<std::string, Json> members;
        skipWhitespace();
        if (consume('}')) return Json::object(std::move(members));
        for (;;) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) {
                fail("expected object key");
                return {};
            }
            skipWhitespace();
            if (!consume(':')) {
                fail("expected ':' after object key");
                return {};
            }
            skipWhitespace();
            Json value = parseValue(depth + 1);
            if (!error_.empty()) return {};
            members.insert_or_assign(std::move(key), std::move(value));
            skipWhitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            fail("expected ',' or '}' in object");
            return {};
        }
        return Json::object(std::move(members));
    }

    Json parseArray(int depth) {
        ++pos_;  // '['
        std::vector<Json> items;
        skipWhitespace();
        if (consume(']')) return Json::array(std::move(items));
        for (;;) {
            skipWhitespace();
            Json value = parseValue(depth + 1);
            if (!error_.empty()) return {};
            items.push_back(std::move(value));
            skipWhitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            fail("expected ',' or ']' in array");
            return {};
        }
        return Json::array(std::move(items));
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            fail("expected opening quote");
            return false;
        }
        out.clear();
        while (!atEnd()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (atEnd()) break;
            const char esc = text_[pos_++];
            switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned int code = 0;
                    if (!parseHex4(code)) return false;
                    // A high surrogate must be followed by its low half; pair
                    // them so non-BMP characters in a title survive.
                    if (code >= 0xD800 && code <= 0xDBFF &&
                        text_.compare(pos_, 2, "\\u") == 0) {
                        const std::size_t mark = pos_;
                        pos_ += 2;
                        unsigned int low = 0;
                        if (!parseHex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = mark;  // not a pair; leave it for the next round
                        }
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    fail("invalid escape sequence");
                    return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parseHex4(unsigned int& out) {
        if (pos_ + 4 > text_.size()) {
            fail("truncated unicode escape");
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9')      out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else {
                fail("invalid hex digit in unicode escape");
                return false;
            }
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    Json parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (!atEnd()) {
            const char c = text_[pos_];
            const bool part = (c >= '0' && c <= '9') || c == '.' || c == 'e' ||
                              c == 'E' || c == '+' || c == '-';
            if (!part) break;
            ++pos_;
        }
        if (pos_ == start) {
            fail("unexpected character");
            return {};
        }
        const std::string literal(text_.substr(start, pos_ - start));
        char* end = nullptr;
        const double value = std::strtod(literal.c_str(), &end);
        if (end != literal.c_str() + literal.size() || !std::isfinite(value)) {
            fail("invalid number");
            return {};
        }
        return Json(value);
    }

    std::string_view text_;
    std::string&     error_;
    std::size_t      pos_ = 0;
};

void escapeTo(std::string& out, const std::string& value) {
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void numberTo(std::string& out, double value) {
    // Integers are the common case in a manifest (sizes, versions); print them
    // without a decimal tail so a round-tripped cache file stays readable.
    if (value == static_cast<double>(static_cast<long long>(value)) &&
        std::fabs(value) < 9.0e15) {
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}

}  // namespace

Json Json::array(std::vector<Json> items) {
    Json value;
    value.type_ = Type::Array;
    value.array_ = std::move(items);
    return value;
}

Json Json::object(std::map<std::string, Json> members) {
    Json value;
    value.type_ = Type::Object;
    value.object_ = std::move(members);
    return value;
}

Json Json::parse(std::string_view text, std::string& error) {
    error.clear();
    Parser parser(text, error);
    return parser.run();
}

bool Json::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Json::asNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

std::string Json::asString(std::string fallback) const {
    return type_ == Type::String ? string_ : std::move(fallback);
}

const Json& Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) return kNull;
    const auto it = object_.find(std::string(key));
    return it == object_.end() ? kNull : it->second;
}

bool Json::contains(std::string_view key) const {
    return type_ == Type::Object && object_.count(std::string(key)) != 0;
}

const std::vector<Json>& Json::items() const {
    return type_ == Type::Array ? array_ : kNoItems;
}

void Json::set(std::string key, Json value) {
    type_ = Type::Object;
    object_.insert_or_assign(std::move(key), std::move(value));
}

void Json::push(Json value) {
    type_ = Type::Array;
    array_.push_back(std::move(value));
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    const std::size_t width = pretty ? static_cast<std::size_t>(indent) : 0;
    const std::string pad(width * static_cast<std::size_t>(depth + 1), ' ');
    const std::string closePad(width * static_cast<std::size_t>(depth), ' ');

    switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += bool_ ? "true" : "false"; break;
        case Type::Number: numberTo(out, number_); break;
        case Type::String: escapeTo(out, string_); break;
        case Type::Array: {
            if (array_.empty()) { out += "[]"; break; }
            out.push_back('[');
            bool first = true;
            for (const Json& item : array_) {
                if (!first) out.push_back(',');
                first = false;
                if (pretty) { out.push_back('\n'); out += pad; }
                item.dumpTo(out, indent, depth + 1);
            }
            if (pretty) { out.push_back('\n'); out += closePad; }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (object_.empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& entry : object_) {
                if (!first) out.push_back(',');
                first = false;
                if (pretty) { out.push_back('\n'); out += pad; }
                escapeTo(out, entry.first);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                entry.second.dumpTo(out, indent, depth + 1);
            }
            if (pretty) { out.push_back('\n'); out += closePad; }
            out.push_back('}');
            break;
        }
    }
}

}  // namespace omnios
