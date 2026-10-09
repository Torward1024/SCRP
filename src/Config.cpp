#include "scrp/Config.h"
#include "scrp/Vfs.h"
#include <fstream>
#include <sstream>
namespace scrp {
JsonValue mergeConfig(const JsonValue& base, const JsonValue& overlay) {
    if (!base.isObject() || !overlay.isObject()) return overlay;
    JsonValue result = base;
    for (const auto& field : overlay.object) {
        bool found = false;
        for (auto& prior : result.object) if (prior.first == field.first) {
            prior.second = mergeConfig(prior.second, field.second); found = true; break;
        }
        if (!found) result.object.push_back(field);
    }
    return result;
}
bool loadConfig(const std::string& asset, JsonValue& out, std::string* error) {
    std::vector<std::string> layers;
    if (!Vfs::readTextLayers(asset, layers)) { if(error) *error="missing configuration: "+asset; return false; }
    JsonValue merged; merged.type = JsonValue::Type::Object;
    for (const auto& text : layers) {
        JsonValue layer;
        if (!Json::parse(text, layer, error)) return false;
        if (!layer.isObject()) { if(error) *error="configuration must be an object"; return false; }
        merged = mergeConfig(merged, layer);
    }
    out = std::move(merged); return true;
}
bool loadManifest(const std::string& filename, JsonValue& out, std::string* error) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) { if(error) *error="missing manifest: "+filename; return false; }
    std::ostringstream text; text << file.rdbuf();
    JsonValue result;
    if (!Json::parse(text.str(), result, error)) return false;
    if (!result.isObject()) { if(error) *error="manifest must be an object"; return false; }
    out=std::move(result); return true;
}
bool mountResources(const JsonValue& mounts, std::string* error) {
    if (!mounts.isArray()) { if(error) *error="mounts must be an array"; return false; }
    // Validate the complete list before changing the mount stack.
    for (const auto& entry : mounts.array) {
        const auto kind=entry["type"].asString();
        if (entry["path"].asString().empty() || (kind!="directory" && kind!="pack" && kind!="packs")) {
            if(error) *error="invalid resource mount";
            return false;
        }
    }
    for (const auto& entry : mounts.array) {
        const auto kind=entry["type"].asString(), path=entry["path"].asString();
        if (kind=="directory") Vfs::mountDir(path);
        else if (kind=="pack") Vfs::mountPack(path);
        else Vfs::mountPacksFrom(path);
    }
    if (!Vfs::mountCount()) { if(error) *error="no resource sources available"; return false; }
    return true;
}
}
