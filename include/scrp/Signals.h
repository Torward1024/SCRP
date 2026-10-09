#pragma once
#include <cstdio>
#include <set>
#include <string>

namespace scrp {
class Signals {
public:
    void clear() {
        raised_.clear();
        justRaised_.clear();
    }

    void emit(const std::string& name) {
        if (name.empty()) return;
        if (raised_.insert(name).second) {
            std::printf("[signal] %s\n", name.c_str());
        }
        justRaised_.insert(name);
    }

    bool raised(const std::string& name) const {
        return !name.empty() && raised_.count(name) > 0;
    }

    bool justRaised(const std::string& name) const {
        return !name.empty() && justRaised_.count(name) > 0;
    }

    const std::set<std::string>& justRaisedAll() const { return justRaised_; }

    void endStep() { justRaised_.clear(); }

    size_t count() const { return raised_.size(); }
    const std::set<std::string>& all() const { return raised_; }

private:
    std::set<std::string> raised_;
    std::set<std::string> justRaised_;
};
} // namespace scrp
