#include "scrp/SaveStore.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace scrp {
std::string SaveStore::path(const std::string& name) const {
    // Saves are individual files in a configured directory, never paths from content.
    if(directory_.empty() || name.empty() || name=="." || name==".." ||
       name.find_first_of("/\\:\0",0,4)!=std::string::npos) return {};
    return (fs::u8path(directory_)/fs::u8path(name)).u8string();
}
bool SaveStore::exists(const std::string& name) const {
    const auto target=path(name); if(target.empty()) return false;
    std::error_code ec; return fs::is_regular_file(fs::u8path(target),ec);
}
bool SaveStore::read(const std::string& name,JsonValue& out,std::string* error) const {
    const auto target=path(name); if(target.empty()) {if(error)*error="invalid save name"; return false;}
    std::ifstream file(fs::u8path(target),std::ios::binary);
    if(!file) {if(error)*error="missing save"; return false;}
    std::ostringstream text; text<<file.rdbuf(); return Json::parse(text.str(),out,error);
}
bool SaveStore::writeAtomic(const std::string& name,const std::string& text) const {
    const auto target=path(name); if(target.empty()) return false;
    std::error_code ec; fs::create_directories(fs::u8path(directory_),ec); if(ec) return false;
    auto temporary=fs::u8path(target+".tmp");
    { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
      if(!file) return false;
      file.write(text.data(),static_cast<std::streamsize>(text.size()));
      file.flush(); if(!file) return false; file.close(); if(!file) return false; }
#ifdef _WIN32
    if(MoveFileExW(temporary.c_str(),fs::u8path(target).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
#else
    fs::rename(temporary,fs::u8path(target),ec); if(!ec) return true;
#endif
    fs::remove(temporary,ec); return false;
}
}
