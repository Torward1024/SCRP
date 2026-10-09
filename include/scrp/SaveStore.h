#pragma once
#include "Json.h"
#include <string>
namespace scrp {
class SaveStore {
public:
    explicit SaveStore(std::string directory): directory_(std::move(directory)) {}
    const std::string& dir() const { return directory_; }
    std::string path(const std::string& name) const;
    bool exists(const std::string& name) const;
    bool read(const std::string& name,JsonValue& out,std::string* error=nullptr) const;
    bool writeAtomic(const std::string& name,const std::string& text) const;
private:
    std::string directory_;
};
}
