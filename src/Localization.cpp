#include "scrp/Localization.h"
#include "scrp/Config.h"
#include "scrp/Utf8.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace scrp {
namespace {
std::string languageTag(std::string value) {
    if (value.empty()) throw std::runtime_error("Empty language tag");
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '_') c = '-';
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-')
            throw std::runtime_error("Invalid language tag");
    }
    if (value.front() == '-' || value.back() == '-' || value.find("--") != std::string::npos)
        throw std::runtime_error("Invalid language tag");
    return value;
}
uint64_t unsignedValue(const JsonValue& value) {
    uint64_t result;
    if (!Json::readUInt64(value, result)) throw std::runtime_error("Invalid plural-rule integer");
    return result;
}
}
bool Localization::Condition::matches(uint64_t value) const {
    if (modulo) value %= modulo;
    auto inRange = [&](const auto& ranges) {
        return std::any_of(ranges.begin(), ranges.end(), [&](const auto& r) { return value >= r.first && value <= r.second; });
    };
    return (include.empty() || inRange(include)) && !inRange(exclude);
}
Localization::Catalogue Localization::parse(const JsonValue& value) {
    if (!value.isObject() || value["schema_version"].type != JsonValue::Type::Number ||
        value["schema_version"].number != 1 || !value["messages"].isObject())
        throw std::runtime_error("Invalid localization catalogue");
    Catalogue next; next.messages = value["messages"];
    for (const auto& field : next.messages.object) {
        if (field.first.empty()) throw std::runtime_error("Empty message key");
        if (field.second.type == JsonValue::Type::String) {
            if (!Utf8::valid(field.second.str)) throw std::runtime_error("Invalid UTF-8 message: " + field.first);
            continue;
        }
        if (!field.second.isObject() || field.second["other"].type != JsonValue::Type::String)
            throw std::runtime_error("Plural message requires an other form: " + field.first);
        for (const auto& form : field.second.object)
            if (form.first.empty() || form.second.type != JsonValue::Type::String || !Utf8::valid(form.second.str))
                throw std::runtime_error("Invalid plural message: " + field.first);
    }
    if (value.contains("plural_rules") && !value["plural_rules"].isArray())
        throw std::runtime_error("Invalid plural rules");
    for (const auto& rule : value["plural_rules"].array) {
        Plural plural; plural.form = rule["form"].asString();
        if (plural.form.empty() || !rule["conditions"].isArray() || rule["conditions"].array.empty())
            throw std::runtime_error("Invalid plural form/conditions");
        for (const auto& node : rule["conditions"].array) {
            if (!node.isObject()) throw std::runtime_error("Invalid plural condition");
            Condition condition;
            if (node.contains("mod")) {
                condition.modulo = unsignedValue(node["mod"]);
                if (!condition.modulo) throw std::runtime_error("Plural modulus must be positive");
            }
            auto ranges = [&](const char* key, auto& out) {
                if (!node.contains(key)) return;
                if (!node[key].isArray() || node[key].array.empty()) throw std::runtime_error("Invalid plural ranges");
                for (const auto& range : node[key].array) {
                    if (!range.isArray() || range.size() != 2) throw std::runtime_error("Invalid plural range");
                    const auto first = unsignedValue(range.at(0)), last = unsignedValue(range.at(1));
                    if (first > last) throw std::runtime_error("Reversed plural range");
                    out.emplace_back(first, last);
                }
            };
            ranges("in", condition.include); ranges("not_in", condition.exclude);
            if (condition.include.empty() && condition.exclude.empty()) throw std::runtime_error("Empty plural condition");
            plural.conditions.push_back(std::move(condition));
        }
        next.plurals.push_back(std::move(plural));
    }
    return next;
}
bool Localization::configure(const JsonValue& config, std::string* error) {
    try {
        if (!config.isObject() || config["schema_version"].type != JsonValue::Type::Number || config["schema_version"].number != 1 ||
            config["language"].type != JsonValue::Type::String || config["fallback"].type != JsonValue::Type::String || !config["catalogues"].isObject())
            throw std::runtime_error("Invalid localization configuration");
        const auto requested = languageTag(config["language"].str), fallback = languageTag(config["fallback"].str);
        std::vector<std::pair<std::string, std::string>> paths;
        std::unordered_set<std::string> tags;
        for (const auto& entry : config["catalogues"].object) {
            const auto tag = languageTag(entry.first);
            if (!tags.insert(tag).second || entry.second.type != JsonValue::Type::String || entry.second.str.empty())
                throw std::runtime_error("Invalid localization catalogue path");
            paths.emplace_back(tag, entry.second.str);
        }
        auto resolve = [&](std::string tag) -> const std::pair<std::string, std::string>* {
            while (true) {
                for (const auto& path : paths) if (path.first == tag) return &path;
                const auto dash = tag.rfind('-'); if (dash == std::string::npos) return nullptr;
                tag.resize(dash);
            }
        };
        const auto* fallbackPath = resolve(fallback);
        if (!fallbackPath) throw std::runtime_error("Missing fallback localization catalogue");
        const auto* currentPath = resolve(requested); if (!currentPath) currentPath = fallbackPath;
        auto read = [&](const std::string& path) {
            JsonValue value; std::string reason;
            if (!loadConfig(path, value, &reason)) throw std::runtime_error(reason);
            return parse(value);
        };
        Localization next; next.fallback_ = read(fallbackPath->second);
        next.current_ = currentPath == fallbackPath ? next.fallback_ : read(currentPath->second);
        next.language_ = currentPath->first;
        *this = std::move(next); if (error) error->clear(); return true;
    } catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}
bool Localization::load(const std::string& path, std::string* error, const std::string& languageOverride) {
    JsonValue config;
    if (!loadConfig(path, config, error)) return false;
    if (!languageOverride.empty()) for (auto& field : config.object)
        if (field.first == "language") field.second.str = languageOverride;
    return configure(config, error);
}
const std::string* Localization::lookup(const Catalogue& catalogue, const std::string& key, std::optional<uint64_t> count) {
    const auto& value = catalogue.messages[key.c_str()];
    if (value.type == JsonValue::Type::String) return &value.str;
    if (!value.isObject()) return nullptr;
    std::string form = "other";
    if (count) for (const auto& plural : catalogue.plurals) {
        if (std::all_of(plural.conditions.begin(), plural.conditions.end(), [&](const Condition& c) { return c.matches(*count); })) {
            form = plural.form; break;
        }
    }
    const auto& chosen = value[form.c_str()];
    return chosen.type == JsonValue::Type::String ? &chosen.str : &value["other"].str;
}
std::string Localization::text(const std::string& key, const std::vector<Argument>& args, std::optional<uint64_t> count) const {
    const std::string* format = lookup(current_, key, count);
    if (!format) format = lookup(fallback_, key, count);
    if (!format) return key;
    std::string output;
    for (size_t i = 0; i < format->size();) {
        if ((*format)[i] == '{' && i + 1 < format->size() && (*format)[i + 1] == '{') { output += '{'; i += 2; continue; }
        if ((*format)[i] == '}' && i + 1 < format->size() && (*format)[i + 1] == '}') { output += '}'; i += 2; continue; }
        if ((*format)[i] == '{') {
            const auto end = format->find('}', i + 1);
            if (end != std::string::npos) {
                const auto name = format->substr(i + 1, end - i - 1);
                const auto arg = std::find_if(args.begin(), args.end(), [&](const Argument& a) { return a.first == name; });
                if (arg != args.end()) output += arg->second;
                else if (name == "count" && count) output += std::to_string(*count);
                else output += format->substr(i, end - i + 1);
                i = end + 1; continue;
            }
        }
        output += (*format)[i++];
    }
    return output;
}
} // namespace scrp
