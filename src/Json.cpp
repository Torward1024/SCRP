#include "scrp/Json.h"
#include "scrp/Vfs.h"
#include <cstdlib>
#include <cstdio>
#include <charconv>
#include <cmath>
#include <limits>
#include <functional>
#include <unordered_set>

namespace scrp {

const JsonValue& JsonValue::null() {
    static const JsonValue nullValue;
    return nullValue;
}

size_t JsonValue::size() const {
    if (type == Type::Array) return array.size();
    if (type == Type::Object) return object.size();
    return 0;
}

const JsonValue& JsonValue::operator[](const char* key) const {
    if (type != Type::Object) return null();
    for (const auto& kv : object) {
        if (kv.first == key) return kv.second;
    }
    return null();
}

const JsonValue& JsonValue::at(size_t index) const {
    if (type != Type::Array || index >= array.size()) return null();
    return array[index];
}

bool JsonValue::contains(const char* key) const {
    if (!isObject()) return false;
    for (const auto& field : object) if (field.first == key) return true;
    return false;
}
bool JsonValue::has(const char* key) const {
    return !(*this)[key].isNull();
}

double JsonValue::asNumber(double def) const {
    if (type == Type::Number) return number;
    if (type == Type::Bool) return boolean ? 1.0 : 0.0;
    return def;
}

int JsonValue::asInt(int def) const {
    if (type == Type::Number && std::isfinite(number) &&
        number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max())
        return static_cast<int>(number);
    if (type == Type::Bool) return boolean ? 1 : 0;
    return def;
}

bool JsonValue::asBool(bool def) const {
    if (type == Type::Bool) return boolean;
    if (type == Type::Number) return number != 0.0;
    return def;
}

std::string JsonValue::asString(const std::string& def) const {
    if (type == Type::String) return str;
    return def;
}

namespace {

class Parser {
public:
    Parser(const std::string& text) : s_(text) {}

    bool parse(JsonValue& out) {
        // Some editors add a UTF-8 BOM. Accept it at the parser boundary
        // so edited asset files remain valid.
        // Handle it here because text may come from a directory,
        // a package or a mod layer.
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF &&
            static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            pos_ = 3;
        }
        skipWhitespace();
        if (!parseValue(out)) return false;
        skipWhitespace();
        if (pos_ != s_.size()) return fail("trailing characters after root value");
        return true;
    }

    const std::string& error() const { return error_; }

private:
    const std::string& s_;
    size_t pos_ = 0;
    std::string error_;
    int depth_ = 0;

    bool fail(const char* what) {
        if (error_.empty()) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "position %zu: %s", pos_, what);
            error_ = buf;
        }
        return false;
    }

    bool eof() const { return pos_ >= s_.size(); }
    char peek() const { return s_[pos_]; }

    void skipWhitespace() {
        while (!eof()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool literal(const char* text) {
        size_t len = 0;
        while (text[len]) ++len;
        if (s_.compare(pos_, len, text) != 0) return false;
        pos_ += len;
        return true;
    }

    bool parseValue(JsonValue& out) {
        if (eof()) return fail("unexpected end of input");
        if (depth_ >= 128) return fail("JSON nesting limit exceeded");
        struct DepthGuard { int& value; ~DepthGuard() { --value; } } guard{depth_};
        ++depth_;
        switch (peek()) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                out.type = JsonValue::Type::String;
                return parseString(out.str);
            }
            case 't':
                if (!literal("true")) return fail("expected true");
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return true;
            case 'f':
                if (!literal("false")) return fail("expected false");
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return true;
            case 'n':
                if (!literal("null")) return fail("expected null");
                out.type = JsonValue::Type::Null;
                return true;
            default:
                return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        ++pos_; // '{'
        skipWhitespace();
        if (!eof() && peek() == '}') { ++pos_; return true; }

        for (;;) {
            skipWhitespace();
            if (eof() || peek() != '"') return fail("expected a quoted field name");

            std::string key;
            if (!parseString(key)) return false;
            for (const auto& field : out.object)
                if (field.first == key) return fail("Duplicate object key");

            skipWhitespace();
            if (eof() || peek() != ':') return fail("expected ':' after field name");
            ++pos_;

            skipWhitespace();
            out.object.emplace_back(std::move(key), JsonValue{});
            if (!parseValue(out.object.back().second)) return false;

            skipWhitespace();
            if (eof()) return fail("unterminated object");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        ++pos_; // '['
        skipWhitespace();
        if (!eof() && peek() == ']') { ++pos_; return true; }

        for (;;) {
            skipWhitespace();
            out.array.emplace_back();
            if (!parseValue(out.array.back())) return false;

            skipWhitespace();
            if (eof()) return fail("unterminated array");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    // Encode a Unicode code point as UTF-8; asset names may contain \uXXXX escapes.
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("non-hexadecimal digit in \\u escape");
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_; // opening quote
        out.clear();
        while (true) {
            if (eof()) return fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("Unescaped control character");
            if (c != '\\') { out += c; continue; }

            if (eof()) return fail("truncated escape sequence");
            char e = s_[pos_++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!parseHex4(cp)) return false;
                    // Surrogate pair
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 1 >= s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u')
                            return fail("Missing low surrogate");
                        pos_ += 2;
                        unsigned low = 0;
                        if (!parseHex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            return fail("Invalid low surrogate");
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("Unpaired low surrogate");
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("unknown escape sequence");
            }
        }
    }

    bool parseNumber(JsonValue& out) {
        size_t start = pos_;
        if (!eof() && peek() == '-') ++pos_;
        if (eof() || peek() < '0' || peek() > '9') return fail("Expected JSON number");
        if (peek() == '0') ++pos_;
        else while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        if (!eof() && peek() == '.') {
            ++pos_;
            const auto digits = pos_;
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
            if (pos_ == digits) return fail("Missing fraction digits");
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!eof() && (peek() == '-' || peek() == '+')) ++pos_;
            const auto digits = pos_;
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
            if (pos_ == digits) return fail("Missing exponent digits");
        }

        out.type = JsonValue::Type::Number;
        const auto result = std::from_chars(s_.data()+start, s_.data()+pos_, out.number);
        if (result.ec != std::errc{} || result.ptr != s_.data()+pos_ || !std::isfinite(out.number))
            return fail("Number out of range");
        return true;
    }
};

} // namespace

namespace Json {

bool parse(const std::string& text, JsonValue& out, std::string* error) {
    out = JsonValue{};
    Parser p(text);
    if (p.parse(out)) return true;
    if (error) *error = p.error();
    return false;
}

bool parseAsset(const std::string& path, JsonValue& out, std::string* error) {
    std::string text;
    if (!Vfs::readText(path, text)) {
        if (error) *error = "missing resource: " + path;
        return false;
    }

    if (!parse(text, out, error)) {
        if (error) *error = path + ": " + *error;
        return false;
    }
    return true;
}

JsonValue uint64Value(uint64_t value) {
    JsonValue result; result.type = JsonValue::Type::String;
    result.str = std::to_string(value); return result;
}
bool readUInt64(const JsonValue& value, uint64_t& out) {
    if (value.type == JsonValue::Type::Number) {
        if (!std::isfinite(value.number) || std::floor(value.number) != value.number ||
            value.number < 0 || value.number > 9007199254740991.0) return false;
        out = static_cast<uint64_t>(value.number); return true;
    }
    if (value.type != JsonValue::Type::String || value.str.empty()) return false;
    uint64_t next;
    const auto parsed = std::from_chars(value.str.data(), value.str.data() + value.str.size(), next);
    if (parsed.ec != std::errc{} || parsed.ptr != value.str.data() + value.str.size()) return false;
    out = next; return true;
}

bool stringify(const JsonValue& value, std::string& out, std::string* error) {
    std::string next, reason;
    auto quote = [&](const std::string& text) {
        constexpr char hex[] = "0123456789abcdef";
        next += '"';
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') { next += '\\'; next += char(c); }
            else if (c < 32) { next += "\\u00"; next += hex[c >> 4]; next += hex[c & 15]; }
            else next += char(c);
        }
        next += '"';
    };
    std::function<bool(const JsonValue&, int)> emit = [&](const JsonValue& node, int depth) {
        if (depth >= 128) { reason = "JSON nesting limit exceeded"; return false; }
        switch (node.type) {
        case JsonValue::Type::Null: next += "null"; break;
        case JsonValue::Type::Bool: next += node.boolean ? "true" : "false"; break;
        case JsonValue::Type::Number: {
            if (!std::isfinite(node.number)) { reason = "Non-finite JSON number"; return false; }
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof buffer, node.number,
                                              std::chars_format::general, std::numeric_limits<double>::max_digits10);
            if (result.ec != std::errc{}) { reason = "JSON number formatting failed"; return false; }
            next.append(buffer, result.ptr); break;
        }
        case JsonValue::Type::String: quote(node.str); break;
        case JsonValue::Type::Array:
            next += '[';
            for (size_t i = 0; i < node.array.size(); ++i) {
                if (i) next += ',';
                if (!emit(node.array[i], depth + 1)) return false;
            }
            next += ']'; break;
        case JsonValue::Type::Object: {
            next += '{'; std::unordered_set<std::string> names;
            for (size_t i = 0; i < node.object.size(); ++i) {
                const auto& field = node.object[i];
                if (!names.insert(field.first).second) { reason = "Duplicate JSON field"; return false; }
                if (i) next += ',';
                quote(field.first); next += ':';
                if (!emit(field.second, depth + 1)) return false;
            }
            next += '}'; break;
        }
        default: reason = "Invalid JSON value type"; return false;
        }
        return true;
    };
    if (!emit(value, 0)) { if (error) *error = reason; return false; }
    out = std::move(next); if (error) error->clear(); return true;
}

} // namespace Json

} // namespace scrp
