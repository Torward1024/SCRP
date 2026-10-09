#include "scrp/Json.h"
#include "scrp/Localization.h"
#include "scrp/Utf8.h"
#include "scrp/Vfs.h"
#include "scrp/IndexedImage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

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

void serializationTests() {
    scrp::JsonValue nullable;
    CHECK(scrp::Json::parse("{\"limit\":null}", nullable));
    CHECK(nullable.contains("limit") && !nullable.has("limit"));
    CHECK(!nullable.contains("missing"));
    uint64_t count = 0;
    const auto maximum = scrp::Json::uint64Value(std::numeric_limits<uint64_t>::max());
    CHECK(scrp::Json::readUInt64(maximum, count));
    CHECK(count == std::numeric_limits<uint64_t>::max());
    auto overflow = maximum; overflow.str += "0"; CHECK(!scrp::Json::readUInt64(overflow, count));
    overflow.str = "-1"; CHECK(!scrp::Json::readUInt64(overflow, count));
    overflow.type = scrp::JsonValue::Type::Number; overflow.number = 9007199254740992.0;
    CHECK(!scrp::Json::readUInt64(overflow, count));
    scrp::JsonValue value, again; std::string output = "unchanged", error;
    CHECK(scrp::Json::parse(R"({"name":"a\"b\\c\n\u0000\ud83d\ude00","items":[null,true,-0.5,1e30]})", value, &error));
    CHECK(scrp::Json::stringify(value, output, &error));
    CHECK(scrp::Json::parse(output, again, &error));
    CHECK(again["name"].str == value["name"].str);
    CHECK(again["items"].at(3).number == value["items"].at(3).number);
    value.type = scrp::JsonValue::Type::Number; value.number = std::numeric_limits<double>::infinity();
    output = "unchanged"; CHECK(!scrp::Json::stringify(value, output, &error)); CHECK(output == "unchanged");
    value.type = scrp::JsonValue::Type::Object; value.object = {{"x", {}}, {"x", {}}};
    CHECK(!scrp::Json::stringify(value, output, &error));
    value = {}; for (int i = 0; i < 130; ++i) { scrp::JsonValue next; next.type = scrp::JsonValue::Type::Array; next.array.push_back(std::move(value)); value = std::move(next); }
    CHECK(!scrp::Json::stringify(value, output, &error));
}
void utf8Tests() {
    std::string name = u8"Rome \u0416\U0001f30d";
    CHECK(scrp::Utf8::valid(name)); CHECK(scrp::Utf8::popBack(name)); CHECK(name == u8"Rome \u0416");
    CHECK(scrp::Utf8::popBack(name)); CHECK(name == "Rome ");
    CHECK(!scrp::Utf8::valid("\xc0\xaf")); CHECK(!scrp::Utf8::valid("\xed\xa0\x80"));
    CHECK(!scrp::Utf8::valid("\xf4\x90\x80\x80")); CHECK(!scrp::Utf8::valid("\xe2\x82"));
    size_t pos = 0; uint32_t value = 7; CHECK(!scrp::Utf8::next("\x80",pos,value)); CHECK(pos == 0 && value == 7);
}
void localizationTests() {
    const auto root = fs::temp_directory_path() / ("scrp-locales-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "base"); fs::create_directories(root / "mod");
    struct Cleanup { fs::path root; ~Cleanup() { scrp::Vfs::unmountAll(); std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    std::ofstream(root / "base/en.json") << R"({"schema_version":1,"plural_rules":[{"form":"one","conditions":[{"in":[[1,1]]}]}],"messages":{"hello":"Hello {name}","cities":{"one":"{count} city","other":"{count} cities"},"fallback":{"one":"English one","other":"English other"},"braces":"{{literal}} {name} {unknown}","blank":""}})";
    std::ofstream(root / "base/ru.json") << R"({"schema_version":1,"plural_rules":[{"form":"one","conditions":[{"mod":10,"in":[[1,1]]},{"mod":100,"not_in":[[11,11]]}]},{"form":"few","conditions":[{"mod":10,"in":[[2,4]]},{"mod":100,"not_in":[[12,14]]}]},{"form":"many","conditions":[{"mod":10,"in":[[0,0],[5,9]]}]},{"form":"many","conditions":[{"mod":100,"in":[[11,14]]}]}],"messages":{"hello":"\u041f\u0440\u0438\u0432\u0435\u0442 {name}","cities":{"one":"one {count}","few":"few {count}","many":"many {count}","other":"other {count}"}}})";
    // A category can have multiple alternative rules; first matching condition set wins.
    CHECK(scrp::Vfs::mountDir((root / "base").u8string()));
    scrp::JsonValue config; std::string error;
    CHECK(scrp::Json::parse(R"({"schema_version":1,"language":"ru-RU","fallback":"en","catalogues":{"en":"en.json","ru":"ru.json"}})", config));
    scrp::Localization locale;
    CHECK(locale.text("missing") == "missing");
    CHECK(locale.configure(config, &error)); CHECK(locale.language() == "ru");
    CHECK(locale.text("hello", {{"name", "Alex"}}) == u8"\u041f\u0440\u0438\u0432\u0435\u0442 Alex");
    CHECK(locale.text("cities", {}, 1) == "one 1"); CHECK(locale.text("cities", {}, 21) == "one 21");
    CHECK(locale.text("cities", {}, 2) == "few 2"); CHECK(locale.text("cities", {}, 24) == "few 24");
    CHECK(locale.text("cities", {}, 0) == "many 0"); CHECK(locale.text("cities", {}, 5) == "many 5");
    CHECK(locale.text("cities", {}, 11) == "many 11"); CHECK(locale.text("cities", {}, 14) == "many 14");
    CHECK(locale.text("fallback", {}, 21) == "English other");
    CHECK(locale.text("braces", {{"name", "{count}"}}, 12) == "{literal} {count} {unknown}");
    CHECK(locale.text("blank").empty());
    std::ofstream(root / "mod/ru.json") << R"({"messages":{"hello":"Mod {name}"}})";
    CHECK(scrp::Vfs::mountDir((root / "mod").u8string())); CHECK(locale.configure(config));
    CHECK(locale.text("hello", {{"name", "Alex"}}) == "Mod Alex");
    std::ofstream(root / "mod/ru.json") << R"({"plural_rules":[{"form":"one","conditions":[{"mod":0,"in":[[1,1]]}]}]})";
    CHECK(!locale.configure(config, &error)); CHECK(locale.text("hello", {{"name", "Alex"}}) == "Mod Alex");
    for (auto& field : config.object) if (field.first == "language") field.second.str = "fr-CA";
    CHECK(locale.configure(config)); CHECK(locale.language() == "en");
    CHECK(locale.text("cities", {}, 1) == "1 city"); CHECK(locale.text("cities", {}, UINT64_MAX) == "18446744073709551615 cities");
    std::ofstream(root / "base/config.json") << R"({"schema_version":1,"language":"ru","fallback":"en","catalogues":{"en":"en.json","ru":"ru.json"}})";
    CHECK(locale.load("config.json", &error, "en-GB")); CHECK(locale.language() == "en");
    CHECK(!locale.load("missing.json", &error)); CHECK(locale.language() == "en");
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
    try { jsonTests(); serializationTests(); vfsTests(); utf8Tests(); localizationTests(); std::cout << "SCRP: " << checks << " checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "SCRP test failure: " << e.what() << '\n'; return 1; }
}
