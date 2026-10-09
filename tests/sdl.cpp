#include "scrp/BitmapFont.h"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "scrp/Assets.h"
#include "scrp/Audio.h"
#include "scrp/Decals.h"
#include "scrp/Gfx.h"
#include "scrp/Input.h"
#include "scrp/Lighting.h"
#include "scrp/Particles.h"
#include "scrp/Vfs.h"
#include "scrp/Window.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <array>
using namespace scrp;
int checks=0;
#define CHECK(expr) do {++checks;if(!(expr)) throw std::runtime_error(#expr);} while(false)
enum class Action {Accept,Other,Count};
const char* actionName(Action value){return value==Action::Accept?"accept":"other";}
JsonValue parse(const char* text){JsonValue value;std::string error;if(!Json::parse(text,value,&error))throw std::runtime_error(error);return value;}
int main() {
 SDL_SetMainReady();
 try {
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0);
    KeyBindings<Action> keys;CHECK(keys.load(parse(R"({"accept":["A"],"other":["B"]})"),actionName));
    std::array<Uint8,SDL_NUM_SCANCODES> state{};state[SDL_SCANCODE_A]=1;keys.sample(state.data(),state.size());
    CHECK(keys.down(Action::Accept)&&keys.pressed(Action::Accept));keys.sample(state.data(),state.size());CHECK(!keys.pressed(Action::Accept));
    state[SDL_SCANCODE_A]=0;keys.sample(state.data(),state.size());CHECK(!keys.down(Action::Accept));
    CHECK(!keys.load(parse(R"({"accept":["No Such Key"]})"),actionName));CHECK(keys.isBound(Action::Accept,SDL_SCANCODE_A));
    CHECK(!keys.load(parse(R"({"missing":["A"]})"),actionName));
    Window window;CHECK(window.open("test",parse(R"({"width":32,"height":24,"art_scale":1,"scale":1,"software":true,"hidden":true})"),0,false));
    Gfx gfx;gfx.configure(parse(R"({"shake_max":4,"shake_decay":7,"assets":{"registry":"sprites.json","roles":{"hero":"synthetic"}},"tile_colors":[[1,2,3,255]]})"));
    CHECK(gfx.init(window.renderer(),32,24));
    const auto temporary=std::filesystem::temp_directory_path()/std::filesystem::u8path("scrp-sdl-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    std::ofstream(temporary/"sprites.json")<<R"({"synthetic":{"frame":[2,2],"fallback":"#112233"}})";
    Vfs::mountDir(temporary.u8string());CHECK(gfx.assets().loadRegistry());CHECK(gfx.assets().role("hero")==gfx.assets().find("synthetic"));
    CHECK(!gfx.assets().role("unknown"));
    gfx.beginFrame();gfx.drawSprite(gfx.assets().role("hero"),{4,4});gfx.drawTile({},0,0,0,8);
    std::array<Uint8,32*24*4> pixels{};CHECK(SDL_RenderReadPixels(window.renderer(),nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),32*4)==0);
    CHECK(pixels[0]==1&&pixels[1]==2&&pixels[2]==3);
    IndexedImage decoded; decoded.width=2; decoded.height=2; decoded.pixels={0,1,1,0};
    decoded.palette[0]={255,0,0,255}; decoded.palette[1]={0,255,0,255};
    const auto injected=gfx.assets().find("synthetic");
    CHECK(gfx.assets().setIndexedImage(injected,decoded,0));
    CHECK(!gfx.assets().setIndexedImage({},decoded));
    gfx.beginFrame({0,0,255,255});
    CHECK(gfx.drawRegionScreen(injected,{0,0,2,2},{0,0,2,2}));
    CHECK(!gfx.drawRegionScreen(injected,{1,0,2,2},{0,0,2,2}));
    CHECK(SDL_RenderReadPixels(window.renderer(),nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),32*4)==0);
    CHECK(pixels[0]==0&&pixels[2]==255&&pixels[4]==0&&pixels[5]==255);
    CHECK(!gfx.assets().setIndexedImage(injected,IndexedImage{}));
    CHECK(gfx.assets().hasTexture(injected));
    CHECK(gfx.captureBmp((temporary/"capture.bmp").u8string()));
    CHECK(std::filesystem::file_size(temporary/"capture.bmp")>32*24);
    BitmapFont font;
    CHECK(font.configure(injected, 3, {{'A', {{1,0,1,1},2}}, {'?', {{1,0,1,1},1}}, {0x0416, {{1,0,1,1},3}}}));
    CHECK(font.textWidth("AA\nA") == 4);
    CHECK(font.textWidth(u8"\u0416A") == 5);
    CHECK(font.textWidth("Z") == 1);
    CHECK(!font.configure({}, 3, {{'A', {{1,0,1,1},2}}}));
    gfx.beginFrame({0,0,255,255}); CHECK(font.draw(gfx,0,0,u8"A\u0416",{255,255,255,255},2));
    CHECK(SDL_RenderReadPixels(window.renderer(),nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),32*4)==0);
    CHECK(pixels[1]==255 && pixels[2*4+2]==255);
    Decals decals;decals.configure(parse(R"({"max_dimension":64,"max_fading":2,"recipes":{"mark":{"rects":[{"x":0,"y":0,"w":2,"h":2,"color":[255,0,0,255]}]}}})"));
    CHECK(decals.init(gfx,32,24));decals.stampRecipe("mark",{10,10},Col::White);CHECK(decals.stampCount()==1);
    for(int i=0;i<4;++i) {decals.addFadingFootprint({4,4},{1,0},Col::White,.1f);}
    CHECK(decals.fadingCount()==2);decals.update(.2f);CHECK(decals.fadingCount()==0);
    Particles particles(2);particles.emitRecipe(parse(R"({"count":3,"speed":[0,0],"life":[0.1,0.1],"size":1})"),{1,1},{0,0},Col::White,"mark");
    CHECK(particles.count()==2);int deaths=0;particles.update(.2f,[&](const Particles::Particle& p){CHECK(p.deathTag=="mark");++deaths;});CHECK(deaths==2&&particles.count()==0);
    Particles empty(0);empty.emitRecipe(parse(R"({"life":[1,1]})"),{0,0},{0,0});CHECK(empty.count()==0);
    Lighting lights;lights.configure(parse(R"({"glow_size":16,"sigma":0.42})"));CHECK(lights.init(gfx));lights.setAmbient({0,0,0,255});
    lights.add({{10,10},8,Col::White,1});CHECK(lights.brightnessAt({10,10})>.99f);CHECK(lights.brightnessAt({30,20})==0);lights.render(gfx);
    Audio::configure(parse(R"({"registry":"sounds.json","channels":4,"sample_rate":22050,"buffer_size":512,"audible_radius":30})"));
#ifdef SCRP_TEST_WITH_MIXER
    CHECK(Audio::init());
#else
    Audio::init();
#endif
    // Independently constructed short PCM WAV, no original content required.
    std::vector<unsigned char> wav={'R','I','F','F',0xA4,0x08,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x22,0x56,0,0,0x44,0xAC,0,0,2,0,16,0,'d','a','t','a',0x80,0x08,0,0};
    wav.resize(44+2176,0);std::ofstream sound(temporary/"tone.wav",std::ios::binary);sound.write(reinterpret_cast<char*>(wav.data()),wav.size());sound.close();
    std::ofstream(temporary/"sounds.json")<<R"({"tone":{"file":"tone.wav"},"music":{"file":"tone.wav","bus":"music"}})";
    Audio::loadRegistry();
#ifdef SCRP_TEST_WITH_MIXER
    CHECK(Audio::loadedCount()==2);
#endif
    Audio::play("tone");Audio::setMusic("music");Audio::loadRegistry();Audio::play("tone");Audio::setMusic("music");
    CHECK(Audio::requestCount()==2);Audio::shutdown();CHECK(Audio::loadedCount()==0&&Audio::requestCount()==0);Audio::shutdown();
    lights.shutdown();decals.shutdown();gfx.shutdown();window.shutdown();Vfs::unmountAll();SDL_Quit();std::filesystem::remove_all(temporary);
    std::cout<<checks<<" SDL checks passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<"SDL failure: "<<error.what()<<' '<<SDL_GetError()<<'\n';return 1;}
}
