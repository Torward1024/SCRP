#include "scrp/Assets.h"
#include "scrp/Json.h"
#include "scrp/Color.h"
#include "scrp/Vfs.h"
#include <cstdio>
#include <cstdlib>

#ifdef SCRP_USE_SDL_IMAGE
#include <SDL_image.h>
#endif

namespace scrp {
namespace {

const SpriteDef kBadSprite{ "<bad-sprite-id>", "", 16, 16, 1, 8, 8, { 255, 0, 255, 255 } };

bool endsWith(const std::string& s, const char* suffix) {
    std::string suf(suffix);
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

SDL_Color parseColor(const std::string& text, SDL_Color def) {
    if (text.size() < 7 || text[0] != '#') return def;
    auto hex = [&](size_t i) -> int {
        return static_cast<int>(std::strtol(text.substr(i, 2).c_str(), nullptr, 16));
    };
    SDL_Color c;
    c.r = static_cast<Uint8>(hex(1));
    c.g = static_cast<Uint8>(hex(3));
    c.b = static_cast<Uint8>(hex(5));
    c.a = text.size() >= 9 ? static_cast<Uint8>(hex(7)) : 255;
    return c;
}

bool isCommentKey(const std::string& key) {
    return !key.empty() && key[0] == '_';
}

void readPair(const JsonValue& node, int& x, int& y) {
    if (node.isArray() && node.size() >= 2) {
        x = node.at(0).asInt(x);
        y = node.at(1).asInt(y);
    } else if (node.type == JsonValue::Type::Number) {
        x = y = node.asInt(x);
    }
}

} // namespace

void SpriteSet::set(const std::string& role, SpriteId id) {
    for (auto& kv : roles) {
        if (kv.first == role) { kv.second = id; return; }
    }
    roles.emplace_back(role, id);
}

SpriteId SpriteSet::get(const char* role) const {
    for (const auto& kv : roles) {
        if (kv.first == role) return kv.second;
    }
    if (std::string(role) != "world") {
        for (const auto& kv : roles) {
            if (kv.first == "world") return kv.second;
        }
    }
    return SpriteId::none();
}

bool Assets::loadRegistry() {
    dropTextures();
    sprites_.clear();
    byName_.clear();

    std::vector<std::string> layers;
    if (!Vfs::readTextLayers(registry_, layers)) {
        std::printf("[assets] missing registry: %s\n",registry_.c_str());
        return false;
    }

    int overrides = 0;
    for (const std::string& text : layers) {
        JsonValue root;
        std::string error;
        if (!Json::parse(text, root, &error)) {
            std::printf("[assets] sprites.json: %s\n", error.c_str());
            continue;
        }
        if (!root.isObject()) {
            std::printf("[assets] sprites.json: expected an object mapping ids to entries\n");
            continue;
        }

        for (const auto& kv : root.object) {
            if (isCommentKey(kv.first)) continue;
            const JsonValue& n = kv.second;
            if (!n.isObject()) continue;

            SpriteDef d;
            d.name = kv.first;

            auto found = byName_.find(kv.first);
            if (found != byName_.end()) {
                d = sprites_[found->second];   // apply the layer over the previous definition
                ++overrides;
            }

            d.file = n["file"].asString(d.file);
            readPair(n["frame"], d.frameW, d.frameH);
            d.columns = n["columns"].asInt(d.columns);
            readPair(n["pivot"], d.pivotX, d.pivotY);
            d.fallback = parseColor(n["fallback"].asString(), d.fallback);
            if (d.columns < 1) d.columns = 1;

            if (found != byName_.end()) {
                sprites_[found->second] = d;
            } else {
                byName_[kv.first] = static_cast<int>(sprites_.size());
                sprites_.push_back(d);
            }
        }
    }

    dropTextures();
    textures_.assign(sprites_.size(), nullptr);
    attempted_.assign(sprites_.size(), 0);

    std::printf("[assets] %zu sprites, %d overrides\n", sprites_.size(), overrides);
    return !sprites_.empty();
}

SpriteId Assets::find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? SpriteId::none() : SpriteId::fromIndex(it->second);
}

const SpriteDef& Assets::def(SpriteId id) const {
    int index = id.index();
    if (index < 0 || index >= static_cast<int>(sprites_.size())) return kBadSprite;
    return sprites_[index];
}

bool Assets::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
#ifdef SCRP_USE_SDL_IMAGE
    int flags = IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) != flags) {
        std::printf("[assets] SDL_image initialization failed: %s\n", IMG_GetError());
    }
#endif
    return renderer_ != nullptr;
}

void Assets::dropTextures() {
    for (SDL_Texture* tex : textures_) {
        if (tex) SDL_DestroyTexture(tex);
    }
    textures_.assign(textures_.size(), nullptr);
    attempted_.assign(attempted_.size(), 0);
    missing_ = 0;
}

void Assets::shutdown() {
    dropTextures();
    textures_.clear();
    attempted_.clear();
    sprites_.clear();
    byName_.clear();
#ifdef SCRP_USE_SDL_IMAGE
    IMG_Quit();
#endif
}

void Assets::reload() {
    loadRegistry();
}

SDL_Texture* Assets::load(const SpriteDef& spriteDef) {
    std::string path = spriteDef.file;
    if (path.empty()) return nullptr;

    std::vector<uint8_t> bytes;
    if (!Vfs::read(path, bytes)) {
#ifndef SCRP_USE_SDL_IMAGE
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return nullptr;
        path = path.substr(0, dot) + ".bmp";
        if (!Vfs::read(path, bytes)) return nullptr;
#else
        return nullptr;
#endif
    }
    if (bytes.empty()) return nullptr;

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return nullptr;

    SDL_Surface* surface = nullptr;
    if (endsWith(path, ".bmp")) {
        surface = SDL_LoadBMP_RW(rw, 1);   // the loader takes ownership of RWops
    } else {
#ifdef SCRP_USE_SDL_IMAGE
        surface = IMG_Load_RW(rw, 1);
#else
        SDL_RWclose(rw);
#endif
    }

    if (!surface) return nullptr;

    if(useColorKey_) SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, colorKey_.r, colorKey_.g, colorKey_.b));
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);

    if (texture) {
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    }
    return texture;
}

SDL_Texture* Assets::texture(SpriteId id) {
    int index = id.index();
    if (index < 0 || index >= static_cast<int>(textures_.size())) return nullptr;

    if (attempted_[index]) return textures_[index];

    attempted_[index] = 1;
    textures_[index] = load(sprites_[index]);
    if (!textures_[index]) {
        ++missing_;
        std::printf("[assets] missing file '%s' for sprite '%s'; using a placeholder\n",
                    sprites_[index].file.c_str(), sprites_[index].name.c_str());
    }
    return textures_[index];
}

bool Assets::hasTexture(SpriteId id) {
    return texture(id) != nullptr;
}

void Assets::configure(const JsonValue& config) {
    registry_=config["registry"].asString(); roles_=config["roles"];
    useColorKey_=!config["color_key"].isNull(); colorKey_=readColor(config["color_key"]);
}
SpriteId Assets::role(const std::string& name) const { return find(roles_[name.c_str()].asString()); }
} // namespace scrp
