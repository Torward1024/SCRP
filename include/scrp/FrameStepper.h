#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace scrp {
class FrameStepper {
public:
    void configure(double step,double maxFrame) {
        if(!std::isfinite(step)||!std::isfinite(maxFrame)||step<=0||maxFrame<step) throw std::invalid_argument("invalid frame timing");
        step_=step; maxFrame_=maxFrame; reset();
    }
    double clamp(double elapsed) const { return std::isfinite(elapsed)?std::clamp(elapsed,0.0,maxFrame_):0.0; }
    void accumulate(double elapsed) { accumulator_+=clamp(elapsed); }
    bool next() { if(accumulator_+1e-10<step_) return false; accumulator_=std::max(0.0,accumulator_-step_); return true; }
    float step() const { return static_cast<float>(step_); }
    float alpha() const { return static_cast<float>(std::clamp(accumulator_/step_,0.0,1.0)); }
    void reset() { accumulator_=0; }
private:
    double step_=1.0/60.0,maxFrame_=0.25,accumulator_=0;
};
}
