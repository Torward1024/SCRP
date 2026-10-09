#pragma once
#include <SDL.h>
#include "scrp/Json.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scrp {
class SpriteId {
public:
    SpriteId() = default;

    static SpriteId none() { return SpriteId(); }
    static SpriteId fromIndex(int index) {
        SpriteId id;
        id.index_ = index;
        return id;
    }

    bool valid() const { return index_ >= 0; }
    explicit operator bool() const { return index_ >= 0; }
    int index() const { return index_; }

    bool operator==(SpriteId other) const { return index_ == other.index_; }
    bool operator!=(SpriteId other) const { return index_ != other.index_; }

private:
    int index_ = -1;
};

struct SpriteDef {
    std::string name;     // registry id used in diagnostics
    std::string file;     // logical resource path; the file may be absent
    int frameW = 16;      // dimensions of a single frame
    int frameH = 16;
    int columns = 1;      // frames per sheet row, or tiles per tileset row
    int pivotX = 0;       // anchor point inside the frame
    int pivotY = 0;
    SDL_Color fallback{ 255, 0, 255, 255 };   // placeholder colour when the resource is missing
};

struct SpriteSet {
    std::vector<std::pair<std::string, SpriteId>> roles;

    void set(const std::string& role, SpriteId id);

    SpriteId get(const char* role) const;

    SpriteId world() const { return get("world"); }
    bool empty() const { return roles.empty(); }
};

class Assets {
public:
    Assets() = default;
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;
    bool init(SDL_Renderer* renderer);
    void shutdown();

    bool loadRegistry();

    SpriteId find(const std::string& name) const;

    const SpriteDef& def(SpriteId id) const;

    SDL_Texture* texture(SpriteId id);

    bool hasTexture(SpriteId id);

    void reload();

    int missingCount() const { return missing_; }
    int spriteCount() const { return static_cast<int>(sprites_.size()); }
    const std::vector<SpriteDef>& all() const { return sprites_; }

    void configure(const JsonValue& config);
    SpriteId role(const std::string& name) const;
private:
    SDL_Renderer* renderer_ = nullptr;

    std::vector<SpriteDef> sprites_;
    std::unordered_map<std::string, int> byName_;
    std::string registry_;
    JsonValue roles_;
    SDL_Color colorKey_{255,0,255,255};
    bool useColorKey_=true;

    std::vector<SDL_Texture*> textures_;
    std::vector<char> attempted_;   // byte flags; vector<bool> uses packed bits
    int missing_ = 0;

    void dropTextures();
    SDL_Texture* load(const SpriteDef& def);
};
} // namespace scrp
