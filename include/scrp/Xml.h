#pragma once
#include <map>
#include <string>
#include <vector>

namespace scrp {
struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
    std::string text;

    bool has(const char* key) const { return attrs.count(key) > 0; }

    std::string attr(const char* key, const std::string& def = std::string()) const;
    int   attrInt(const char* key, int def = 0) const;
    float attrFloat(const char* key, float def = 0.f) const;

    const XmlNode* child(const char* childName) const;

    std::vector<const XmlNode*> childrenNamed(const char* childName) const;
};

namespace Xml {
bool parse(const std::string& text, XmlNode& out, std::string* error = nullptr);

bool parseAsset(const std::string& path, XmlNode& out, std::string* error = nullptr);
}
} // namespace scrp
