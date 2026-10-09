#pragma once
#include "Json.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scrp {
class Localization {
public:
    using Argument = std::pair<std::string, std::string>;
    // Configuration names catalogue paths; each catalogue supplies messages and plural rules.
    bool configure(const JsonValue& config, std::string* error = nullptr);
    bool load(const std::string& path, std::string* error = nullptr, const std::string& languageOverride = {});
    std::string text(const std::string& key, const std::vector<Argument>& args = {},
                     std::optional<uint64_t> count = std::nullopt) const;
    const std::string& language() const { return language_; }
private:
    struct Condition {
        uint64_t modulo = 0;
        std::vector<std::pair<uint64_t, uint64_t>> include, exclude;
        bool matches(uint64_t value) const;
    };
    struct Plural { std::string form; std::vector<Condition> conditions; };
    struct Catalogue { JsonValue messages; std::vector<Plural> plurals; };
    Catalogue current_, fallback_;
    std::string language_;
    static Catalogue parse(const JsonValue& value);
    static const std::string* lookup(const Catalogue& catalogue, const std::string& key,
                                     std::optional<uint64_t> count);
};
} // namespace scrp
