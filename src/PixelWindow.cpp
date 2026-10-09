#include "scrp/PixelWindow.h"

namespace scrp {
PixelWindow::~PixelWindow() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (initialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
bool PixelWindow::open(const std::string& title, int width, int height, int scale, std::string& error) {
    if (initialized_ || width < 1 || height < 1 || width > 4096 || height > 4096 || scale < 1 || scale > 8) {
        error = "Invalid window configuration or window already open"; return false;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { error = SDL_GetError(); return false; }
    initialized_ = true;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width*scale, height*scale, SDL_WINDOW_RESIZABLE);
    if (!window_) { error = SDL_GetError(); return false; }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_ || SDL_RenderSetLogicalSize(renderer_, width, height) != 0 ||
        SDL_RenderSetIntegerScale(renderer_, SDL_TRUE) != 0) { error = SDL_GetError(); return false; }
    return true;
}
bool PixelWindow::upload(const IndexedImage& image, std::string& error) {
    if (!renderer_ || !image.valid()) { error = "Invalid image or unopened window"; return false; }
    if (texture_) { SDL_DestroyTexture(texture_); texture_ = nullptr; }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                 image.width, image.height);
    if (!texture_) { error = SDL_GetError(); return false; }
    const auto bytes = image.rgba();
    if (SDL_UpdateTexture(texture_, nullptr, bytes.data(), image.width*4) != 0) {
        error = SDL_GetError(); return false;
    }
    imageWidth_ = image.width; imageHeight_ = image.height;
    return true;
}
bool PixelWindow::present(std::string& error) {
    if (!renderer_ || !texture_) { error = "No uploaded image"; return false; }
    int w, h;
    SDL_RenderGetLogicalSize(renderer_, &w, &h);
    SDL_Rect dst{(w-imageWidth_)/2, (h-imageHeight_)/2, imageWidth_, imageHeight_};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer_) != 0 || SDL_RenderCopy(renderer_, texture_, nullptr, &dst) != 0) {
        error = SDL_GetError(); return false;
    }
    SDL_RenderPresent(renderer_);
    return true;
}
void PixelWindow::setTitle(const std::string& title) { if (window_) SDL_SetWindowTitle(window_, title.c_str()); }
} // namespace scrp
