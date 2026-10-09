#include "scrp/Window.h"
#include <algorithm>
#include <limits>
namespace scrp {
bool Window::open(const char* title,const JsonValue& config,int scaleOverride,bool vsync) {
    shutdown();
    const int width=config["width"].asInt(),height=config["height"].asInt(),art=config["art_scale"].asInt(1);
    if(width<=0||height<=0||art<=0||width>16384/art||height>16384/art) {SDL_SetError("invalid window dimensions");return false;}
    const int deviceW=width*art,deviceH=height*art;
    int scale=scaleOverride>0?scaleOverride:config["scale"].asInt();
    if(scale<=0) {
        SDL_DisplayMode mode{};
        scale=config["fallback_scale"].asInt(1);
        if(SDL_GetCurrentDisplayMode(0,&mode)==0) {
            const double fraction=std::clamp(config["display_fraction"].asNumber(0.9),0.1,1.0);
            scale=std::max(1,std::min(static_cast<int>(mode.w*fraction/deviceW),static_cast<int>(mode.h*fraction/deviceH)));
        }
    }
    if(scale<=0||scale>std::numeric_limits<int>::max()/std::max(deviceW,deviceH)) {SDL_SetError("invalid window scale");return false;}
    Uint32 flags=config["hidden"].asBool()?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN;
    if(config["resizable"].asBool(true)) flags|=SDL_WINDOW_RESIZABLE;
    window_=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,deviceW*scale,deviceH*scale,flags);
    if(!window_) return false;
    Uint32 rendererFlags=config["software"].asBool()?SDL_RENDERER_SOFTWARE:SDL_RENDERER_ACCELERATED;
    if(vsync) rendererFlags|=SDL_RENDERER_PRESENTVSYNC;
    renderer_=SDL_CreateRenderer(window_,-1,rendererFlags);
    if(!renderer_) {shutdown();return false;}
    return true;
}
void Window::shutdown() {
    if(renderer_) SDL_DestroyRenderer(renderer_);
    if(window_) SDL_DestroyWindow(window_);
    renderer_=nullptr;window_=nullptr;
}
}
