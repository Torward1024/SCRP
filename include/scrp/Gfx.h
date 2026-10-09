#pragma once
#include <SDL.h>
#include "scrp/Vec2.h"
#include "scrp/Assets.h"
#include "scrp/Color.h"
#include "scrp/Rng.h"

namespace scrp {
class Gfx {
public:
    Gfx() = default;
    Gfx(const Gfx&) = delete;
    Gfx& operator=(const Gfx&) = delete;
    void configure(const JsonValue& config);
    void setRng(Rng& rng) { rng_=&rng; }
    bool init(SDL_Renderer* renderer, int worldW, int worldH, int artScale = 1);
    void shutdown();

    Assets& assets() { return assets_; }
    SDL_Renderer* raw() { return renderer_; }

    int width()  const { return logicalW_; }
    int height() const { return logicalH_; }

    int artScale() const { return artScale_; }
    int deviceWidth()  const { return logicalW_ * artScale_; }
    int deviceHeight() const { return logicalH_ * artScale_; }

    void beginFrame(SDL_Color clear = Col::Black);
    void endFrame();

    void setCamera(const Vec2& worldPos) { camera_ = worldPos; }
    Vec2 camera() const { return camera_; }
    void centerCameraOn(const Vec2& target, int levelPxW, int levelPxH);

    void focusOn(const Vec2& point, float time);
    void updateFocus(float dt);
    bool focusing() const { return focusTime_ > 0.f; }

    void addShake(float strength);
    void updateShake(float dt);

    void addFlash(SDL_Color color, float time);
    void updateFlash(float dt);
    void renderFlash();

    Vec2 renderOrigin() const;

    void drawSprite(SpriteId id, const Vec2& worldPos, int col = 0, int row = 0,
                    bool flipX = false, SDL_Color tint = Col::White,
                    float fallbackSize = 0.f);
    void drawTile(SpriteId sheet, int tileIndex, int worldX, int worldY, int tileSize);
    void fillRectWorld(const Vec2& center, float w, float h, SDL_Color c);
    void fillPixelWorld(const Vec2& pos, float size, SDL_Color c);
    void drawLineWorld(const Vec2& a, const Vec2& b, SDL_Color c);
    void drawRectWorld(const Vec2& center, float w, float h, SDL_Color c);

    bool visibleWorld(const Vec2& center, float w, float h) const;

    SDL_Texture* createTargetTexture(int w, int h);
    void beginTargetDraw(SDL_Texture* target);
    void endTargetDraw();
    void blitTargetWorld(SDL_Texture* target, int texW, int texH);

    void fillRectScreen(int x, int y, int w, int h, SDL_Color c);
    void drawRectScreen(int x, int y, int w, int h, SDL_Color c);
    void drawLineScreen(int x1, int y1, int x2, int y2, SDL_Color c);
    void drawSpriteScreen(SpriteId id, int x, int y, int col = 0, int row = 0,
                          SDL_Color tint = Col::White);

private:
    SDL_Renderer* renderer_ = nullptr;
    Assets assets_;
    Rng fallbackRng_;
    Rng* rng_=&fallbackRng_;
    float shakeDecay_=0.f, shakeMax_=0.f, focusTravel_=0.3f;
    std::vector<SDL_Color> tileColors_;

    int logicalW_ = 0;    // world units, independent of device pixels
    int logicalH_ = 0;
    int artScale_ = 1;

    Vec2 camera_;
    Vec2 shakeOffset_;
    float shakeAmount_ = 0.f;
    Vec2 focusPoint_;
    float focusTime_ = 0.f;
    float focusMaxTime_ = 0.f;

    SDL_Color flashColor_{ 0, 0, 0, 0 };
    float flashTime_ = 0.f;
    float flashMaxTime_ = 0.f;

    void setColor(SDL_Color c);
};
} // namespace scrp
