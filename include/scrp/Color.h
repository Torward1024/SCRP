#pragma once
#include <SDL.h>
#include "Json.h"
#include <cstdlib>
#include <cctype>
namespace scrp {
namespace Col {
inline constexpr SDL_Color White{255,255,255,255}, Black{0,0,0,255};
inline constexpr SDL_Color withAlpha(SDL_Color c, Uint8 a) { return {c.r,c.g,c.b,a}; }
}
inline SDL_Color readColor(const JsonValue& value, SDL_Color fallback=Col::White) {
    if(value.isArray() && (value.size()==3 || value.size()==4)) {
        SDL_Color result=fallback; Uint8* components[]={&result.r,&result.g,&result.b,&result.a};
        for(size_t i=0;i<value.size();++i) {
            if(value.at(i).type!=JsonValue::Type::Number || value.at(i).asNumber()<0 || value.at(i).asNumber()>255) return fallback;
            *components[i]=static_cast<Uint8>(value.at(i).asInt());
        }
        return result;
    }
    const auto text=value.asString();
    if((text.size()!=7 && text.size()!=9) || text[0]!='#') return fallback;
    for(size_t i=1;i<text.size();++i) if(!std::isxdigit(static_cast<unsigned char>(text[i]))) return fallback;
    return {static_cast<Uint8>(std::strtoul(text.substr(1,2).c_str(),nullptr,16)),
            static_cast<Uint8>(std::strtoul(text.substr(3,2).c_str(),nullptr,16)),
            static_cast<Uint8>(std::strtoul(text.substr(5,2).c_str(),nullptr,16)),
            static_cast<Uint8>(text.size()==9?std::strtoul(text.substr(7,2).c_str(),nullptr,16):255)};
}
}
