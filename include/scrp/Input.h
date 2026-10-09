#pragma once
#include <SDL.h>
#include <array>
#include <string>
#include <vector>
#include "Json.h"
namespace scrp {
template<class Action> class KeyBindings {
public:
    void bind(Action action,SDL_Scancode key) {
        if(key<=SDL_SCANCODE_UNKNOWN || key>=SDL_NUM_SCANCODES) return;
        auto& keys=keys_.at(index(action));
        for(auto prior:keys) if(prior==key) return;
        keys.push_back(key);
    }
    void clear(Action action) { keys_.at(index(action)).clear(); }
    const std::vector<SDL_Scancode>& keysFor(Action action) const { return keys_.at(index(action)); }
    bool isBound(Action action,SDL_Scancode key) const {
        for(auto prior:keysFor(action)) if(prior==key) return true;
        return false;
    }
    bool down(Action action) const { return isDown_.at(index(action)); }
    bool pressed(Action action) const { return down(action) && !wasDown_.at(index(action)); }
    void beginFrame() { int count=0; const auto* state=SDL_GetKeyboardState(&count); sample(state,count); }
    void sample(const Uint8* state,int count) {
        wasDown_=isDown_;
        for(size_t i=0;i<keys_.size();++i) {
            isDown_[i]=false;
            for(auto key:keys_[i]) if(state && key>=0 && key<count && state[key]) { isDown_[i]=true; break; }
        }
    }
    template<class Name> bool load(const JsonValue& bindings,Name name,std::string* error=nullptr) {
        if(!bindings.isObject()) {if(error)*error="bindings must be an object"; return false;}
        KeyBindings next;
        for(const auto& field:bindings.object) {
            bool known=false;
            for(size_t i=0;i<keys_.size();++i) if(field.first==name(static_cast<Action>(i))) {
                known=true;
                if(!field.second.isArray()) {if(error)*error="binding must be an array: "+field.first; return false;}
                for(const auto& value:field.second.array) {
                    auto key=keyFromName(value.asString());
                    if(key==SDL_SCANCODE_UNKNOWN) {if(error)*error="unknown key: "+value.asString(); return false;}
                    next.bind(static_cast<Action>(i),key);
                }
                break;
            }
            if(!known) {if(error)*error="unknown action: "+field.first; return false;}
        }
        *this=std::move(next); return true;
    }
    static const char* keyName(SDL_Scancode key) { return SDL_GetScancodeName(key); }
    static SDL_Scancode keyFromName(const std::string& name) { return SDL_GetScancodeFromName(name.c_str()); }
private:
    static size_t index(Action action) { return static_cast<size_t>(action); }
    std::array<std::vector<SDL_Scancode>,static_cast<size_t>(Action::Count)> keys_;
    std::array<bool,static_cast<size_t>(Action::Count)> wasDown_{},isDown_{};
};
}
