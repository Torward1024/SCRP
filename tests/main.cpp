#include "scrp/Json.h"
#include "scrp/Vfs.h"
#include "scrp/IndexedImage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
int checks = 0;
void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) throw std::runtime_error(std::string("Line ")+std::to_string(line)+": "+expression);
}
#define CHECK(x) check((x), #x, __LINE__)

void jsonTests() {
    scrp::JsonValue value; std::string error;
    CHECK(scrp::Json::parse("\xEF\xBB\xBF{\"name\":\"\\u0421\\u0421\\u0421\\u0420\",\"values\":[true,null,-2.5e2]}", value, &error));
    CHECK(value["name"].str == u8"\u0421\u0421\u0421\u0420");
    CHECK(value["values"].at(2).asInt() == -250);
    CHECK(value["missing"].asInt(42) == 42);
    CHECK(scrp::Json::parse("\"\\ud83d\\ude00\"", value, &error));
    CHECK(value.str == "\xF0\x9F\x98\x80");
    for (const auto* input : {"+1", "01", "1.", "1e", "1e+", "1e999", "NaN",
                              "{\"x\":1,\"x\":2}", "[1,]", "{}garbage", "\"\\ud800\"", "\"\\udc00\"", "\"a\nb\""})
        CHECK(!scrp::Json::parse(input, value, &error));
    CHECK(!scrp::Json::parse(std::string(200, '[')+"0"+std::string(200, ']'), value, &error));
    CHECK(scrp::Json::parse("1e30", value, &error));
    CHECK(value.asInt(7) == 7);
}

void vfsTests() {
    const fs::path root = fs::temp_directory_path()/
        ("scrp-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"base"); fs::create_directories(root/"mod");
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            scrp::Vfs::unmountAll();
            std::error_code ec;
            for (const auto* name : {"base/data.json", "mod/data.json", "fixture.pack", "bad.pack", "image.bmp"})
                fs::remove(root/name, ec);
            fs::remove(root/"base", ec); fs::remove(root/"mod", ec); fs::remove(root, ec);
        }
    } cleanup{root};
    std::ofstream(root/"base/data.json") << "{\"value\":1}";
    std::ofstream(root/"mod/data.json") << "{\"value\":2}";
    scrp::Vfs::unmountAll();
    CHECK(scrp::Vfs::mountDir((root/"base").u8string()));
    CHECK(scrp::Vfs::mountDir((root/"mod").u8string()));
    scrp::JsonValue value;
    CHECK(scrp::Json::parseAsset("./data.json", value));
    CHECK(value["value"].asInt() == 2);
    std::vector<std::string> layers;
    CHECK(scrp::Vfs::readTextLayers("data.json", layers));
    CHECK(layers.size() == 2 && layers.front().find('1') != std::string::npos);
    CHECK(scrp::Vfs::list().size() == 1);
    std::vector<uint8_t> bytes{1};
    for (const auto* bad : {"../mod/data.json", "/data.json", "C:/data.json", "base/../../data.json"}) {
        CHECK(!scrp::Vfs::read(bad, bytes)); CHECK(bytes.empty());
    }
    // Build a SCRP v1 fixture independently, including a known CRC32 for "abc".
    auto pack = [&](const fs::path& path, uint32_t crc) {
        std::ofstream f(path, std::ios::binary);
        auto le = [&f](uint64_t v, int count) { for (int i = 0; i < count; ++i) f.put(char((v>>(i*8))&255)); };
        f.write("SCRP",4); le(1,4); le(23,8); le(1,4); f.write("abc",3);
        le(8,2); f.write("test.bin",8); le(20,8); le(3,4); le(crc,4);
    };
    pack(root/"fixture.pack", 0x352441c2u);
    CHECK(scrp::Vfs::mountPack((root/"fixture.pack").u8string()));
    CHECK(scrp::Vfs::read("test.bin", bytes));
    CHECK(std::string(bytes.begin(),bytes.end()) == "abc");
    pack(root/"bad.pack", 0);
    CHECK(scrp::Vfs::mountPack((root/"bad.pack").u8string()));
    CHECK(!scrp::Vfs::read("test.bin", bytes));
    CHECK(bytes.empty());
    std::vector<std::vector<uint8_t>> brokenLayers;
    CHECK(!scrp::Vfs::readLayers("test.bin", brokenLayers));
    CHECK(brokenLayers.empty());
    scrp::IndexedImage image; image.width = 1; image.height = 1; image.pixels = {6};
    CHECK(image.valid());
    CHECK(image.rgba() == std::vector<uint8_t>({170,85,0,255}));
    std::string error;
    CHECK(image.writeBmp((root/"image.bmp").u8string(), error));
    CHECK(fs::file_size(root/"image.bmp") == 58);
}
}
int main() {
    try { jsonTests(); vfsTests(); std::cout << "SCRP: " << checks << " checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "SCRP test failure: " << e.what() << '\n'; return 1; }
}
