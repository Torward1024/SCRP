#include "scrp/Audio.h"
#include "scrp/Json.h"
#include "scrp/Rng.h"
#include "scrp/Vfs.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef SCRP_USE_SDL_MIXER
#include <SDL_mixer.h>
#endif

namespace scrp {
namespace {

int kChannels = 24;
JsonValue g_config;

float kDefaultAudible = 0.f;

struct SoundDef {
    std::string id;
    std::vector<std::string> files;
    Bus bus = Bus::Sfx;
    float volume = 1.f;
    int priority = 1;
    float cooldown = 0.f;
    bool loop = false;

#ifdef SCRP_USE_SDL_MIXER
    std::vector<Mix_Chunk*> chunks;
#endif
    float lastPlayed = -1000.f;
};

std::unordered_map<std::string, SoundDef> g_sounds;
float g_busVolume[static_cast<int>(Bus::Count)] = { 1.f, 1.f, 1.f, 1.f };
float g_master = 1.f;
Vec2 g_listener{ 0.f, 0.f };
float g_clock = 0.f;
int g_requests = 0;
int g_loaded = 0;
bool g_ready = false;
Rng g_pick{ 0xA0D10u };

std::string g_ambientId, g_musicId;

#ifdef SCRP_USE_SDL_MIXER
std::vector<int> g_channelPriority;
std::vector<uint8_t> g_musicBytes;
int g_ambientChannel = -1;
Mix_Music* g_music = nullptr;

Mix_Chunk* loadChunk(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!Vfs::read(path, bytes) || bytes.empty()) return nullptr;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return nullptr;
    return Mix_LoadWAV_RW(rw, 1);   // the loader takes ownership of RWops
}
#endif

Bus parseBus(const std::string& s) {
    if (s == "world") return Bus::World;
    if (s == "ambient") return Bus::Ambient;
    if (s == "music") return Bus::Music;
    return Bus::Sfx;
}

bool isCommentKey(const std::string& key) {
    return !key.empty() && key[0] == '_';
}

SoundDef* find(const std::string& id) {
    auto it = g_sounds.find(id);
    return it == g_sounds.end() ? nullptr : &it->second;
}

} // namespace

namespace Audio {
void configure(const JsonValue& config) {
    g_config=config;
    const int channels=std::clamp(config["channels"].asInt(24),1,256);
#ifdef SCRP_USE_SDL_MIXER
    if(g_ready && channels!=kChannels) {
        Mix_HaltChannel(-1);Mix_AllocateChannels(channels);
        g_channelPriority.assign(channels,0);g_ambientChannel=-1;g_ambientId.clear();
    }
#endif
    kChannels=channels;
    kDefaultAudible=std::max(0.001f,static_cast<float>(config["audible_radius"].asNumber()));
}

bool init() {
#ifdef SCRP_USE_SDL_MIXER
    if (Mix_OpenAudio(g_config["sample_rate"].asInt(22050), MIX_DEFAULT_FORMAT, 2, g_config["buffer_size"].asInt(512)) < 0) {
        std::printf("[audio] cannot open audio device: %s; audio disabled\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(kChannels);
    g_channelPriority.assign(kChannels,0);
    g_ready = true;
    return true;
#else
    std::printf("[audio] built without SDL_mixer; audio disabled\n");
    return false;
#endif
}

void shutdown() {
#ifdef SCRP_USE_SDL_MIXER
    if (g_ready) { Mix_HaltChannel(-1); Mix_HaltMusic(); }
    if (g_music) { Mix_FreeMusic(g_music); g_music = nullptr; }
    for (auto& kv : g_sounds) {
        for (Mix_Chunk* c : kv.second.chunks) if (c) Mix_FreeChunk(c);
        kv.second.chunks.clear();
    }
    g_musicBytes.clear();
    if(g_ready) Mix_CloseAudio();
    g_ready = false;
#endif
    g_sounds.clear();
#ifdef SCRP_USE_SDL_MIXER
    g_ambientChannel=-1; g_channelPriority.clear();
#endif
    g_ambientId.clear(); g_musicId.clear();
    g_clock=0.f; g_requests=0; g_loaded=0;
}

void loadRegistry() {
#ifdef SCRP_USE_SDL_MIXER
    if(g_ready) { Mix_HaltChannel(-1); Mix_HaltMusic(); }
    if(g_music) {Mix_FreeMusic(g_music);g_music=nullptr;}
    g_musicBytes.clear(); g_ambientChannel=-1;
    for(auto& entry:g_sounds) for(auto* chunk:entry.second.chunks) if(chunk) Mix_FreeChunk(chunk);
#endif
    g_ambientId.clear(); g_musicId.clear();
    g_sounds.clear();
    g_loaded = 0;

    std::vector<std::string> layers;
    if (!Vfs::readTextLayers(g_config["registry"].asString(), layers)) {
        std::printf("[audio] missing registry: %s\n",g_config["registry"].asString().c_str());
        return;
    }

    for (const std::string& text : layers) {
        JsonValue root;
        std::string error;
        if (!Json::parse(text, root, &error)) {
            std::printf("[audio] sounds.json: %s\n", error.c_str());
            continue;
        }
        if (!root.isObject()) continue;

        for (const auto& kv : root.object) {
            if (isCommentKey(kv.first)) continue;
            const JsonValue& n = kv.second;
            if (!n.isObject()) continue;

            SoundDef d;
            auto prev = g_sounds.find(kv.first);
            if (prev != g_sounds.end()) d = prev->second;
            d.id = kv.first;

            const JsonValue& files = n["files"];
            if (files.isArray()) {
                d.files.clear();
                for (size_t i = 0; i < files.size(); ++i) d.files.push_back(files.at(i).asString());
            } else if (n.has("file")) {
                d.files.assign(1, n["file"].asString());
            }

            d.bus = parseBus(n["bus"].asString("sfx"));
            d.volume = static_cast<float>(n["volume"].asNumber(d.volume));
            d.priority = n["priority"].asInt(d.priority);
            d.cooldown = static_cast<float>(n["cooldown"].asNumber(d.cooldown));
            d.loop = n["loop"].asBool(d.loop);

#ifdef SCRP_USE_SDL_MIXER
            if (g_ready) {
                for (Mix_Chunk* c : d.chunks) if (c) Mix_FreeChunk(c);
                d.chunks.clear();
                for (const std::string& f : d.files) {
                    Mix_Chunk* c = loadChunk(f);
                    if (c) ++g_loaded;
                    d.chunks.push_back(c);
                }
            }
#endif
            g_sounds[d.id] = std::move(d);
        }
    }

    std::printf("[audio] %zu entries, %d files loaded\n", g_sounds.size(), g_loaded);
}

void update(float realDt) {
    g_clock += realDt;
}

void setListener(const Vec2& worldPos) { g_listener = worldPos; }

void setBusVolume(Bus bus, float v) {
    const int i = static_cast<int>(bus);
    if (i >= 0 && i < static_cast<int>(Bus::Count)) g_busVolume[i] = std::max(0.f, std::min(1.f, v));
}
float busVolume(Bus bus) {
    const int i = static_cast<int>(bus);
    return (i >= 0 && i < static_cast<int>(Bus::Count)) ? g_busVolume[i] : 1.f;
}
void setMasterVolume(float v) { g_master = std::max(0.f, std::min(1.f, v)); }
float masterVolume() { return g_master; }

int requestCount() { return g_requests; }
int loadedCount() { return g_loaded; }

int activeVoices() {
#ifdef SCRP_USE_SDL_MIXER
    if (!g_ready) return 0;
    int n = 0;
    for (int i = 0; i < kChannels; ++i) if (Mix_Playing(i)) ++n;
    return n;
#else
    return 0;
#endif
}

namespace {

void emit(const std::string& id, const Vec2* at, float volume, float radius) {
    ++g_requests;

    SoundDef* d = find(id);
    if (!d || d->files.empty()) return;

    if (d->cooldown > 0.f && g_clock - d->lastPlayed < d->cooldown) return;

    float gain = volume * d->volume * g_master * g_busVolume[static_cast<int>(d->bus)];
    int pan = 0;

    if (at) {
        const float audible = radius > 0.f ? radius : kDefaultAudible;
        const Vec2 diff = *at - g_listener;
        const float dist = diff.length();
        if (dist >= audible) return;

        const float k = 1.f - dist / audible;
        gain *= k * k;

        pan = static_cast<int>(std::max(-1.f, std::min(1.f, diff.x / audible)) * 110.f);
    }

    if (gain <= 0.01f) return;
    d->lastPlayed = g_clock;

#ifdef SCRP_USE_SDL_MIXER
    if (!g_ready) return;

    Mix_Chunk* chunk = d->chunks.empty() ? nullptr
                     : d->chunks[g_pick.next() % d->chunks.size()];
    if (!chunk) return;

    int channel = Mix_PlayChannel(-1, chunk, 0);
    if (channel < 0) {
        int victim = -1, worst = d->priority;
        for (int i = 0; i < kChannels; ++i) {
            if (Mix_Playing(i) && g_channelPriority[i] < worst) {
                worst = g_channelPriority[i];
                victim = i;
            }
        }
        if (victim < 0) return;   // every active voice has higher priority
        Mix_HaltChannel(victim);
        channel = Mix_PlayChannel(victim, chunk, 0);
        if (channel < 0) return;
    }

    g_channelPriority[channel] = d->priority;
    Mix_Volume(channel, static_cast<int>(gain * MIX_MAX_VOLUME));
    if (pan != 0) {
        const Uint8 right = static_cast<Uint8>(std::min(255, 127 + pan));
        Mix_SetPanning(channel, static_cast<Uint8>(254 - right), right);
    } else {
        Mix_SetPanning(channel, 255, 255);
    }
#else
    (void)pan;
#endif
}

} // namespace

void play(const std::string& id, float volume) { emit(id, nullptr, volume, 0.f); }

void playAt(const std::string& id, const Vec2& worldPos, float volume, float radius) {
    emit(id, &worldPos, volume, radius);
}

void setAmbient(const std::string& id) {
    if (id == g_ambientId) return;
#ifdef SCRP_USE_SDL_MIXER
    if (!g_ready) return;
    g_ambientId = id;
    if (g_ambientChannel >= 0) {
        Mix_FadeOutChannel(g_ambientChannel, g_config["ambient_fade_out_ms"].asInt());
        g_ambientChannel = -1;
    }
    SoundDef* d = find(id);
    if (!d || d->chunks.empty() || !d->chunks[0]) return;

    g_ambientChannel = Mix_FadeInChannel(-1, d->chunks[0], -1, g_config["ambient_fade_in_ms"].asInt());
    if (g_ambientChannel >= 0) {
        g_channelPriority[g_ambientChannel] = 1000;   // ambient loops cannot be displaced
        Mix_Volume(g_ambientChannel,
                   static_cast<int>(d->volume * g_master *
                                    g_busVolume[static_cast<int>(Bus::Ambient)] * MIX_MAX_VOLUME));
    }
#endif
}

void setMusic(const std::string& id) {
    if (id == g_musicId) return;
#ifdef SCRP_USE_SDL_MIXER
    if (!g_ready) return;
    g_musicId = id;

    if (id.empty()) { Mix_FadeOutMusic(g_config["music_fade_out_ms"].asInt()); return; }

    SoundDef* d = find(id);
    if (!d || d->files.empty()) return;

    std::vector<uint8_t> bytes;
    if (!Vfs::read(d->files[0], bytes) || bytes.empty()) return;

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return;

    Mix_Music* next = Mix_LoadMUS_RW(rw, 1);
    if (!next) return;

    if (g_music) { Mix_HaltMusic(); Mix_FreeMusic(g_music); }
    g_musicBytes=std::move(bytes);
    g_music = next;
    Mix_VolumeMusic(static_cast<int>(d->volume * g_master *
                                     g_busVolume[static_cast<int>(Bus::Music)] * MIX_MAX_VOLUME));
    Mix_FadeInMusic(g_music, -1, g_config["music_fade_in_ms"].asInt());
#endif
}

} // namespace Audio
} // namespace scrp
