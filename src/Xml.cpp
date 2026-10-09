#include "scrp/Xml.h"
#include "scrp/Vfs.h"
#include <cstdio>
#include <cstdlib>

namespace scrp {
std::string XmlNode::attr(const char* key, const std::string& def) const {
    auto it = attrs.find(key);
    return it != attrs.end() ? it->second : def;
}

int XmlNode::attrInt(const char* key, int def) const {
    auto it = attrs.find(key);
    if (it == attrs.end() || it->second.empty()) return def;
    return std::atoi(it->second.c_str());
}

float XmlNode::attrFloat(const char* key, float def) const {
    auto it = attrs.find(key);
    if (it == attrs.end() || it->second.empty()) return def;
    return static_cast<float>(std::atof(it->second.c_str()));
}

const XmlNode* XmlNode::child(const char* childName) const {
    for (const XmlNode& c : children) {
        if (c.name == childName) return &c;
    }
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(const char* childName) const {
    std::vector<const XmlNode*> found;
    for (const XmlNode& c : children) {
        if (c.name == childName) found.push_back(&c);
    }
    return found;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parse(XmlNode& out) {
        skipProlog();
        if (!parseElement(out)) return false;
        return true;
    }

    const std::string& error() const { return error_; }

private:
    const std::string& s_;
    size_t pos_ = 0;
    std::string error_;

    bool fail(const char* what) {
        if (error_.empty()) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "позиция %zu: %s", pos_, what);
            error_ = buf;
        }
        return false;
    }

    bool eof() const { return pos_ >= s_.size(); }
    char peek() const { return s_[pos_]; }

    void skipSpace() {
        while (!eof() && (s_[pos_] == ' ' || s_[pos_] == '\t' ||
                          s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool startsWith(const char* text) const {
        return s_.compare(pos_, std::char_traits<char>::length(text), text) == 0;
    }

    void skipUntil(const char* text) {
        size_t at = s_.find(text, pos_);
        pos_ = (at == std::string::npos) ? s_.size()
                                         : at + std::char_traits<char>::length(text);
    }

    void skipProlog() {
        for (;;) {
            skipSpace();
            if (startsWith("<?")) { skipUntil("?>"); continue; }
            if (startsWith("<!--")) { skipUntil("-->"); continue; }
            if (startsWith("<!")) { skipUntil(">"); continue; }
            break;
        }
    }

    static bool isNameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
    }

    std::string readName() {
        size_t start = pos_;
        while (!eof() && isNameChar(s_[pos_])) ++pos_;
        return s_.substr(start, pos_ - start);
    }

    static void appendEntity(std::string& out, const std::string& entity) {
        if (entity == "amp") out += '&';
        else if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (entity.size() > 1 && entity[0] == '#') {
            int code = (entity[1] == 'x') ? static_cast<int>(std::strtol(entity.c_str() + 2, nullptr, 16))
                                          : std::atoi(entity.c_str() + 1);
            if (code > 0 && code < 128) out += static_cast<char>(code);
        }
    }

    std::string decode(const std::string& raw) {
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') { out += raw[i]; continue; }
            size_t end = raw.find(';', i);
            if (end == std::string::npos) { out += raw[i]; continue; }
            appendEntity(out, raw.substr(i + 1, end - i - 1));
            i = end;
        }
        return out;
    }

    bool parseAttributes(XmlNode& node) {
        for (;;) {
            skipSpace();
            if (eof()) return fail("тег не закрыт");
            if (peek() == '>' || peek() == '/') return true;

            std::string key = readName();
            if (key.empty()) return fail("ожидалось имя атрибута");

            skipSpace();
            if (eof() || peek() != '=') return fail("ожидалось '=' после атрибута");
            ++pos_;
            skipSpace();

            if (eof() || (peek() != '"' && peek() != '\'')) return fail("значение без кавычек");
            char quote = s_[pos_++];
            size_t start = pos_;
            while (!eof() && s_[pos_] != quote) ++pos_;
            if (eof()) return fail("незакрытое значение атрибута");

            node.attrs[key] = decode(s_.substr(start, pos_ - start));
            ++pos_;
        }
    }

    bool parseElement(XmlNode& node) {
        skipSpace();
        if (eof() || peek() != '<') return fail("ожидался элемент");
        ++pos_;

        node.name = readName();
        if (node.name.empty()) return fail("ожидалось имя элемента");

        if (!parseAttributes(node)) return false;

        if (peek() == '/') {          // самозакрытый тег
            ++pos_;
            if (eof() || peek() != '>') return fail("ожидалось '>' после '/'");
            ++pos_;
            return true;
        }
        ++pos_;                        // '>'

        std::string content;
        for (;;) {
            if (eof()) return fail("элемент не закрыт");

            if (startsWith("<!--")) { skipUntil("-->"); continue; }

            if (startsWith("</")) {
                pos_ += 2;
                std::string closing = readName();
                if (closing != node.name) return fail("несовпадающий закрывающий тег");
                skipSpace();
                if (eof() || peek() != '>') return fail("ожидалось '>'");
                ++pos_;
                node.text = decode(content);
                return true;
            }

            if (peek() == '<') {
                node.children.emplace_back();
                if (!parseElement(node.children.back())) return false;
                continue;
            }

            content += s_[pos_++];
        }
    }
};

} // namespace

namespace Xml {

bool parse(const std::string& text, XmlNode& out, std::string* error) {
    out = XmlNode{};
    Parser p(text);
    if (p.parse(out)) return true;
    if (error) *error = p.error();
    return false;
}

bool parseAsset(const std::string& path, XmlNode& out, std::string* error) {
    std::string text;
    if (!Vfs::readText(path, text)) {
        if (error) *error = "нет ресурса: " + path;
        return false;
    }
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!parse(text, out, error)) {
        if (error) *error = path + ": " + *error;
        return false;
    }
    return true;
}

} // namespace Xml
} // namespace scrp
