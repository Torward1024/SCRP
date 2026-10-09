#pragma once
#include "IndexedImage.h"
#include <SDL.h>

namespace scrp {
// SDL belongs to the presentation layer. IndexedImage and core stay SDL-free.
class PixelWindow {
public:
    ~PixelWindow();
    PixelWindow() = default;
    PixelWindow(const PixelWindow&) = delete;
    PixelWindow& operator=(const PixelWindow&) = delete;
    bool open(const std::string& title, int width, int height, int scale, std::string& error);
    bool upload(const IndexedImage& image, std::string& error);
    bool present(std::string& error);
    void setTitle(const std::string& title);
private:
    bool initialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int imageWidth_ = 0, imageHeight_ = 0;
};
} // namespace scrp
