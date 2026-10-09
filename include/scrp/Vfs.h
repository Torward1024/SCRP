#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace scrp::Vfs {
// Read-only resource layers. Later mounts override earlier mounts.
// SCRP v1 archives remain compatible with Scrapheart; extensions are arbitrary.
bool mountDir(const std::string& path);
bool mountPack(const std::string& path);
int mountPacksFrom(const std::string& dir, const std::string& ext = ".scrap");
void unmountAll();
int mountCount();
std::vector<std::string> mountNames();
bool exists(const std::string& path);
bool read(const std::string& path, std::vector<uint8_t>& out);
bool readText(const std::string& path, std::string& out);
bool readLayers(const std::string& path, std::vector<std::vector<uint8_t>>& out);
bool readTextLayers(const std::string& path, std::vector<std::string>& out);
std::vector<std::string> list(const std::string& prefix = {});
std::string describe(const std::string& path);
int missCount();
} // namespace scrp::Vfs
