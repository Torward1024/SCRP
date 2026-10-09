#pragma once
#include <functional>
#include <vector>
#include <utility>
namespace scrp {
template<class Event> class EventBus {
public:
    using Handler=std::function<void(const Event&)>;
    void subscribe(Handler handler) { handlers_.push_back(std::move(handler)); }
    void emit(const Event& event) { pending_.push_back(event); }
    void dispatch() {
        if (dispatching_ || pending_.empty()) return;
        auto batch=std::move(pending_); pending_.clear();
        const auto listeners=handlers_; const auto epoch=epoch_;
        dispatching_=true;
        try {
            for (const auto& event : batch) {
                for (const auto& handler : listeners) {
                    if(epoch_!=epoch) break;
                    handler(event);
                }
                if(epoch_!=epoch) break;
                ++delivered_;
            }
        } catch (...) { dispatching_=false; throw; }
        dispatching_=false;
    }
    void reset() { ++epoch_; handlers_.clear(); pending_.clear(); delivered_=0; }
    int deliveredCount() const { return delivered_; }
    int subscriberCount() const { return static_cast<int>(handlers_.size()); }
private:
    std::vector<Handler> handlers_;
    std::vector<Event> pending_;
    unsigned epoch_=0;
    int delivered_=0;
    bool dispatching_=false;
};
}
