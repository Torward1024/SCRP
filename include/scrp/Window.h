#pragma once
#include <SDL.h>
#include "Json.h"
namespace scrp {
// Owns its window/renderer; SDL subsystem ownership stays with the application.
class Window {
public:
    Window()=default;
    ~Window() { shutdown(); }
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
    bool open(const char* title,const JsonValue& config,int scaleOverride=0,bool vsync=true);
    void shutdown();
    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
private:
    SDL_Window* window_=nullptr;
    SDL_Renderer* renderer_=nullptr;
};
}
