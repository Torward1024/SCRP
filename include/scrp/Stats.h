#pragma once
#include <array>
#include <algorithm>
namespace scrp {
enum class StatOp {Add, Mul, Set};
template<class Stat, class EffectDef> class StatBlock {
public:
    StatBlock() { reset(); }
    void reset() {
        base_.fill(0.f);
        add_.fill(0.f);
        mul_.fill(1.f);
        set_.fill(0.f);
        hasSet_.fill(false);
    }

    void setBase(Stat stat, float value) { base_[index(stat)] = value; }
    float base(Stat stat) const { return base_[index(stat)]; }

    void apply(const EffectDef& effect) {
        size_t i = index(effect.stat);
        switch (effect.op) {
            case StatOp::Add: add_[i] += effect.value; break;
            case StatOp::Mul: mul_[i] *= effect.value; break;
            case StatOp::Set: set_[i] = effect.value; hasSet_[i] = true; break;
        }
    }

    template <typename Container>
    void applyAll(const Container& effects) {
        for (const EffectDef& effect : effects) apply(effect);
    }

    float get(Stat stat) const {
        size_t i = index(stat);
        if (hasSet_[i]) return set_[i];
        return (base_[i] + add_[i]) * mul_[i];
    }

    int getInt(Stat stat, int minimum = 0) const {
        return std::max(minimum, static_cast<int>(get(stat) + 0.5f));
    }

private:
    static constexpr size_t kCount = static_cast<size_t>(Stat::Count);
    static size_t index(Stat stat) { return static_cast<size_t>(stat); }

    std::array<float, kCount> base_{};
    std::array<float, kCount> add_{};
    std::array<float, kCount> mul_{};
    std::array<float, kCount> set_{};
    std::array<bool, kCount> hasSet_{};
};
}
