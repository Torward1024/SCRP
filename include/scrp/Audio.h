#pragma once
#include <string>
#include "scrp/Vec2.h"
#include "scrp/Json.h"

namespace scrp {
enum class Bus {
    Sfx,       // immediate sound events
    World,     // positional sounds audible within a radius
    Ambient,   // background loops
    Music,
    Count
};

namespace Audio {

void configure(const JsonValue& config);
bool init();
void shutdown();

void loadRegistry();

void update(float realDt);

void play(const std::string& id, float volume = 1.f);

void playAt(const std::string& id, const Vec2& worldPos, float volume = 1.f,
            float radius = 0.f);

void setListener(const Vec2& worldPos);

void setAmbient(const std::string& id);

void setMusic(const std::string& id);

void setBusVolume(Bus bus, float v);
float busVolume(Bus bus);
void setMasterVolume(float v);
float masterVolume();

int requestCount();      // sound requests during this session
int loadedCount();       // number of successfully loaded files
int activeVoices();

} // namespace Audio
} // namespace scrp
