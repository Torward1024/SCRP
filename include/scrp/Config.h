#pragma once
#include "Json.h"
#include <string>
namespace scrp {
// Object fields merge recursively; arrays and scalar values replace the earlier layer.
JsonValue mergeConfig(const JsonValue& base, const JsonValue& overlay);
bool loadConfig(const std::string& asset, JsonValue& out, std::string* error = nullptr);
bool loadManifest(const std::string& filename, JsonValue& out, std::string* error = nullptr);
bool mountResources(const JsonValue& mounts, std::string* error = nullptr);
}
