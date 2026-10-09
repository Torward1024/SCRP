#include "scrp/Anim.h"
#include "scrp/Config.h"
#include "scrp/Events.h"
#include "scrp/FlowField.h"
#include "scrp/FrameStepper.h"
#include "scrp/PathFinder.h"
#include "scrp/Rng.h"
#include "scrp/SaveStore.h"
#include "scrp/Signals.h"
#include "scrp/Stats.h"
#include "scrp/Vfs.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

using namespace scrp;
int checks=0;
#define CHECK(expr) do {++checks;if(!(expr)) throw std::runtime_error(#expr);} while(false)
enum class State {Idle,Once,Count};
enum class Stat {A,B,Count};
struct Effect {Stat stat;StatOp op;float value;};
struct TestGrid : Grid {
    int cell=8; unsigned revision=0;
    bool walls[49]{};
    int widthTiles() const override {return 7;}
    int heightTiles() const override {return 7;}
    int tileSize() const override {return cell;}
    unsigned solidRevision() const override {return revision;}
    bool isWall(int x,int y) const override {return x<0||y<0||x>=7||y>=7||walls[y*7+x];}
    void wall(int x,int y,bool value=true) {walls[y*7+x]=value;++revision;}
};
int main() {
 try {
    Rng gameplay(5),same(5),cosmetic(9);
    for(int i=0;i<30;++i) {cosmetic.next();CHECK(gameplay.next()==same.next());}
    CHECK(Rng(0).next()!=0);
    CHECK(facingFromVec({1,0})==Facing8::E);
    AnimSet<State> clips; clips.clips[1]={4,3,10,false};
    Animator<State> animator;animator.setSet(&clips);animator.setState(State::Once);
    animator.update(.21f);CHECK(animator.col()==6);animator.update(.11f);CHECK(animator.finished());
    animator.setState(State::Once,true);CHECK(animator.col()==4&&!animator.finished());
    StatBlock<Stat,Effect> stats;stats.setBase(Stat::A,10);stats.apply({Stat::A,StatOp::Add,2});stats.apply({Stat::A,StatOp::Mul,3});
    CHECK(stats.get(Stat::A)==36);stats.apply({Stat::A,StatOp::Set,7});CHECK(stats.getInt(Stat::A)==7);
    stats.reset();CHECK(stats.get(Stat::A)==0);
    Signals signals;signals.emit("gate");CHECK(signals.raised("gate")&&signals.justRaised("gate"));
    signals.endStep();CHECK(signals.raised("gate")&&!signals.justRaised("gate"));signals.clear();CHECK(!signals.raised("gate"));
    EventBus<int> bus;std::vector<int> received;
    bus.subscribe([&](int event){received.push_back(event);if(event==1){bus.emit(2);bus.subscribe([&](int n){received.push_back(n*10);});bus.dispatch();}});
    bus.emit(1);bus.dispatch();CHECK((received==std::vector<int>{1}));bus.dispatch();CHECK((received==std::vector<int>{1,2,20}));
    CHECK(bus.deliveredCount()==2);bus.reset();bus.subscribe([&](int){bus.reset();});bus.emit(3);bus.emit(4);bus.dispatch();CHECK(bus.deliveredCount()==0);
    FrameStepper step;step.configure(.02,.1);step.accumulate(.055);int ticks=0;while(step.next())++ticks;
    CHECK(ticks==2);CHECK(std::abs(step.alpha()-.75f)<.00001f);CHECK(step.clamp(10)==.1);step.reset();CHECK(!step.next());
    bool rejected=false;try{step.configure(0,.1);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
    TestGrid grid;grid.wall(3,2);grid.wall(3,3);grid.wall(3,4);
    CHECK(!grid.lineOfSight({12,28},{44,28}));CHECK(grid.lineOfSight({12,12},{44,12}));
    CHECK(grid.collides({28,28},1));CHECK(!grid.collides({12,28},1));CHECK(grid.collides({-1,4},0));
    Vec2 position{20,28};CHECK(grid.moveWithCollision(position,{8,0},1));CHECK(position.x==20);
    Vec2 free;CHECK(grid.findFreeSpotNear({28,28},1,2,free));CHECK(!grid.collides(free,1));
    std::vector<Vec2> route;CHECK(PathFinder::find(grid,{12,28},{44,28},route,100));CHECK(!route.empty());
    Vec2 anchor{12,28};for(auto waypoint:route){CHECK(grid.lineOfSight(anchor,waypoint));anchor=waypoint;}
    CHECK(!PathFinder::find(grid,{12,28},{44,28},route,1));
    FlowField flow;CHECK(flow.update(grid,{44,28}));CHECK(!flow.update(grid,{44,28}));CHECK(flow.distanceAt(grid,{12,28})>0);
    grid.wall(3,3,false);CHECK(flow.update(grid,{44,28}));CHECK(flow.distanceAt(grid,{12,28})==4);
    CHECK(flow.distanceAt(grid,{-1,4})==-1);
    TestGrid corner;corner.wall(1,0);CHECK(!corner.lineOfSight({4,4},{12,12}));
    CHECK(!corner.lineOfSight({-1,4},{4,4}));CHECK(!corner.lineOfSight({12,4},{12,4}));
    TestGrid second;second.revision=grid.revision;CHECK(flow.update(second,{44,28}));
    const auto temporary=std::filesystem::temp_directory_path()/std::filesystem::u8path("scrp-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    const auto root=temporary.u8string();SaveStore saves(root);
    CHECK(!saves.writeAtomic("../escape.json","{}"));CHECK(!saves.writeAtomic("bad:name","{}"));
    CHECK(saves.writeAtomic("profile.json","{\"value\":1}"));CHECK(saves.writeAtomic("profile.json","{\"value\":2}"));
    JsonValue value;CHECK(saves.read("profile.json",value));CHECK(value["value"].asInt()==2);
    // A replacement failure must preserve an existing target.
    std::filesystem::create_directory(temporary/"occupied.json");std::ofstream(temporary/"occupied.json"/"keep")<<"keep";
    CHECK(!saves.writeAtomic("occupied.json","{}"));CHECK(std::filesystem::exists(temporary/"occupied.json"/"keep"));
    const auto base=temporary/"base",mod=temporary/"mod";std::filesystem::create_directory(base);std::filesystem::create_directory(mod);
    std::ofstream(base/"config.json")<<R"({"window":{"width":32,"height":16},"keys":["A"]})";
    std::ofstream(mod/"config.json")<<R"({"window":{"width":64},"keys":["B"]})";
    Vfs::mountDir(base.u8string());Vfs::mountDir(mod.u8string());CHECK(loadConfig("config.json",value));
    CHECK(value["window"]["width"].asInt()==64&&value["window"]["height"].asInt()==16);CHECK(value["keys"].at(0).asString()=="B");
    std::ofstream(mod/"config.json")<<"{";CHECK(!loadConfig("config.json",value));CHECK(value["window"]["width"].asInt()==64);
    Vfs::unmountAll();std::filesystem::remove_all(temporary);
    std::cout<<checks<<" runtime checks passed\n";return 0;
 } catch(const std::exception& error) {std::cerr<<"Runtime failure: "<<error.what()<<'\n';return 1;}
}
