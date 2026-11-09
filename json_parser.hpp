#pragma once

// Minimal JSON parser for configuration files (no external dependencies).
//
// Self-contained value model + recursive-descent parser supporting
// objects, arrays, strings (with unicode escapes), numbers, booleans,
// null, comments, BOM, and trailing commas. Config parsing lives in
// config.cpp; this module only produces a JsonValue tree.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace logamuffin {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    bool isObject() const { return type == Type::Object; }
    const JsonValue* find(const std::string& key) const {
        if (type != Type::Object)
            return nullptr;
        for (const auto& kv : objectValue)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
};

struct JsonParseError {
    std::string message;
    std::size_t pos = 0;
};

class JsonParser {
public:
    explicit JsonParser(std::string text);

    bool parse(JsonValue& out, JsonParseError& err);

private:
    enum class SeparatorResult { Continue, Done, Error };

    std::string text_;
    std::size_t pos_ = 0;

    bool fail(JsonParseError& err, const std::string& msg);
    bool atEnd() const;
    char peek() const;
    char consume();
    bool skipIgnored(JsonParseError& err);
    bool parseValue(JsonValue& out, JsonParseError& err, int depth);
    bool parseLiteral(const std::string& lit, bool boolVal, JsonValue& out,
                      JsonParseError& err);
    bool parseNumber(JsonValue& out, JsonParseError& err);
    bool skipNumberSign();
    bool parseIntegerPart(JsonParseError& err);
    bool parseFractionPart(JsonParseError& err);
    bool parseExponentPart(JsonParseError& err);
    static void appendUtf8(std::string& out, unsigned int cp);
    bool parseHex4(JsonParseError& err, unsigned int& value);
    bool parseString(std::string& out, JsonParseError& err);
    bool parseEscape(std::string& out, JsonParseError& err);
    bool parseUnicodeEscape(std::string& out, JsonParseError& err);
    bool parseSurrogatePair(unsigned int high, std::string& out,
                            JsonParseError& err);
    bool tryConsumeEmptyContainer(char closeChar);
    SeparatorResult consumeElementSeparator(char closeChar,
                                            JsonParseError& err);
    bool parseObject(JsonValue& out, JsonParseError& err, int depth);
    bool parseArray(JsonValue& out, JsonParseError& err, int depth);
};

std::string json_location(const std::string& text, std::size_t pos);

bool json_number_to_int(const JsonValue& value, int& out);
bool json_scalar_to_int(const JsonValue& value, int& out);

} // namespace logamuffin
