#pragma once
#include "scrp/Vec2.h"

namespace scrp {
enum class Facing8 {
    S = 0, SW, W, NW, N, NE, E, SE,
    Count
};

Facing8 facingFromVec(const Vec2& dir, Facing8 fallback = Facing8::S);
Vec2 vecFromFacing(Facing8 f);

struct AnimClip {
    int firstFrame = 0;     // индекс первого кадра в строке
    int frameCount = 1;
    float fps = 8.f;
    bool loop = true;
};

template<class State> struct AnimSet {
    AnimClip clips[static_cast<int>(State::Count)];
};

template<class State> class Animator {
public:
    Facing8 facing = Facing8::S;

    void setSet(const AnimSet<State>* set) { set_ = set; }

    void setState(State s, bool restart = false);
    State state() const { return state_; }

    void update(float dt);
    void faceDirection(const Vec2& dir);

    int col() const;
    int row() const;
    bool flipX() const { return false; }   // зеркальные листы включим здесь, если понадобятся

    bool finished() const { return finished_; }

    float progress() const;

private:
    const AnimSet<State>* set_ = nullptr;
    State state_ = static_cast<State>(0);
    int frame_ = 0;
    float timer_ = 0.f;
    bool finished_ = false;

    const AnimClip& clip() const;
};
} // namespace scrp

namespace scrp {
template<class State>
const AnimClip& Animator<State>::clip() const {
    static const AnimClip fallback{};
    if (!set_) return fallback;
    return set_->clips[static_cast<int>(state_)];
}

template<class State>
void Animator<State>::setState(State s, bool restart) {
    if (state_ == s && !restart) return;
    state_ = s;
    frame_ = 0;
    timer_ = 0.f;
    finished_ = false;
}

template<class State>
void Animator<State>::faceDirection(const Vec2& dir) {
    facing = facingFromVec(dir, facing);
}

template<class State>
void Animator<State>::update(float dt) {
    const AnimClip& c = clip();
    if (c.frameCount <= 1 || c.fps <= 0.f) {
        finished_ = !c.loop;
        return;
    }

    timer_ += dt;
    float frameTime = 1.f / c.fps;
    while (timer_ >= frameTime) {
        timer_ -= frameTime;
        ++frame_;
        if (frame_ >= c.frameCount) {
            if (c.loop) {
                frame_ = 0;
            } else {
                frame_ = c.frameCount - 1;
                finished_ = true;
                break;
            }
        }
    }
}

template<class State>
int Animator<State>::col() const {
    const AnimClip& c = clip();
    return c.firstFrame + frame_;
}

template<class State>
int Animator<State>::row() const {
    return static_cast<int>(facing);
}

template<class State>
float Animator<State>::progress() const {
    const AnimClip& c = clip();
    if (c.frameCount <= 0) return 1.f;
    return static_cast<float>(frame_) / static_cast<float>(c.frameCount);
}
}
