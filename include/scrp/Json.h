#pragma once
#include <string>
#include <vector>
#include <utility>

// JSON value/parser extracted from Scrapheart. No SDL or game dependencies.
namespace scrp {
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull()   const { return type == Type::Null; }
    bool isArray()  const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // Количество элементов массива (или полей объекта)
    size_t size() const;

    // Доступ к полю объекта. Отсутствующее поле возвращает Null-значение,
    // а не бросает — чтобы разбор карт не превращался в лес проверок.
    const JsonValue& operator[](const char* key) const;

    // Для элементов массива — только at(): operator[] с числовым литералом
    // неоднозначен (0 подходит и под const char*, и под size_t).
    const JsonValue& at(size_t index) const;

    bool has(const char* key) const;

    double      asNumber(double def = 0.0) const;
    int         asInt(int def = 0) const;
    bool        asBool(bool def = false) const;
    std::string asString(const std::string& def = std::string()) const;

    static const JsonValue& null();
};

namespace Json {
// Обе функции возвращают false и заполняют error при ошибке разбора.
bool parse(const std::string& text, JsonValue& out, std::string* error = nullptr);

// Читает через Vfs: путь логический ("data/weapons.json"), а откуда он
// придёт — из пака, из DLC или из папки разработчика — решает монтирование.
bool parseAsset(const std::string& path, JsonValue& out, std::string* error = nullptr);
}

} // namespace scrp
