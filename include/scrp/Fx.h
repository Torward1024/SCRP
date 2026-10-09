#pragma once
#include <SDL.h>
#include "scrp/Color.h"
#include <string>
#include <vector>
#include "Assets.h"
namespace scrp {
struct FxEmitterDef {
    int count = 8;
    SDL_Color color{ 255, 255, 255, 255 };
    float speedMin = 20.f, speedMax = 80.f;
    float lifeMin = 0.2f, lifeMax = 0.5f;
    float size = 1.f;
    float spread = 6.2832f;   // radians; defaults to emission in all directions
    bool directed = false;    // emit along the supplied direction

    bool useTint = false;
};

struct FxDef {
    std::string id;
    std::vector<FxEmitterDef> emitters;

    SpriteId sprite;                     // invalid handle means no sprite
    int spriteFrames = 1;
    float spriteFps = 12.f;
    float spriteSize = 16.f;
    bool spriteOverlay = false;          // render in the overlay pass

    float shake = 0.f;                    // camera shake strength
    SDL_Color flashColor{ 0, 0, 0, 0 };   // full-screen flash
    float flashTime = 0.f;

    float lightRadius = 0.f;              // zero disables the effect light
    SDL_Color lightColor{ 255, 210, 150, 255 };
    float lightIntensity = 1.f;

    float hitStop = 0.f;

    float cameraFocus = 0.f;

    std::string sound;                    // sound event id interpreted by the application
};

}
