#pragma once
#include <string>
#include <vector>
#include <utility>

// JSON value/parser extracted from Scrapheart. No SDL or game dependencies.
namespace scrp {
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull()   const { return type == Type::Null; }
    bool isArray()  const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // Number of array elements or object fields.
    size_t size() const;

    // Object lookup returns a null value for a missing field
    // rather than throwing an exception.
    const JsonValue& operator[](const char* key) const;

    // Use at() for array elements: a literal zero would make operator[]
    // ambiguous between const char* and size_t.
    const JsonValue& at(size_t index) const;

    bool has(const char* key) const;

    double      asNumber(double def = 0.0) const;
    int         asInt(int def = 0) const;
    bool        asBool(bool def = false) const;
    std::string asString(const std::string& def = std::string()) const;

    static const JsonValue& null();
};

namespace Json {
// Both functions return false and populate error on parse failure.
bool parse(const std::string& text, JsonValue& out, std::string* error = nullptr);

// Reads a logical asset path through VFS. Mount ordering selects the
// underlying package, content layer or development directory.
bool parseAsset(const std::string& path, JsonValue& out, std::string* error = nullptr);
}

} // namespace scrp
