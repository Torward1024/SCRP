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
    float spread = 6.2832f;   // радианы; по умолчанию во все стороны
    bool directed = false;    // true — лететь вдоль направления вызова

    bool useTint = false;
};

struct FxDef {
    std::string id;
    std::vector<FxEmitterDef> emitters;

    SpriteId sprite;                     // пустая = спрайта нет
    int spriteFrames = 1;
    float spriteFps = 12.f;
    float spriteSize = 16.f;
    bool spriteOverlay = false;          // рисовать над сущностями, а не под

    float shake = 0.f;                    // тряска экрана
    SDL_Color flashColor{ 0, 0, 0, 0 };   // вспышка на весь экран
    float flashTime = 0.f;

    float lightRadius = 0.f;              // 0 = эффект не светит
    SDL_Color lightColor{ 255, 210, 150, 255 };
    float lightIntensity = 1.f;

    float hitStop = 0.f;

    float cameraFocus = 0.f;

    std::string sound;                    // имя события звука; пока заглушка
};

}
