#include "json_parser.hpp"

#include <cctype>
#include <cmath>

#include "text_utils.hpp"

namespace logamuffin {

namespace {

// Maximum JSON nesting depth to reject deeply nested input.
constexpr int kMaxJsonNestingDepth = 64;

} // namespace

JsonParser::JsonParser(std::string text) : text_(std::move(text)) {
    if (text_.size() >= 3 &&
        static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF) {
        pos_ = 3;
    }
}

bool JsonParser::parse(JsonValue& out, JsonParseError& err) {
    if (!skipIgnored(err))
        return false;
    if (!parseValue(out, err, 0))
        return false;
    if (!skipIgnored(err))
        return false;
    if (pos_ != text_.size()) {
        err = {"unexpected trailing characters", pos_};
        return false;
    }
    return true;
}


bool JsonParser::fail(JsonParseError& err, const std::string& msg) {
    err = {msg, pos_};
    return false;
}

bool JsonParser::skipIgnored(JsonParseError& err) {
    while (true) {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
        if (pos_ + 1 < text_.size() && text_[pos_] == '/' &&
            text_[pos_ + 1] == '/') {
            while (pos_ < text_.size() && text_[pos_] != '\n')
                ++pos_;
            continue;
        }
        if (pos_ + 1 < text_.size() && text_[pos_] == '/' &&
            text_[pos_ + 1] == '*') {
            const std::size_t end = text_.find("*/", pos_ + 2);
            if (end == std::string::npos)
                return fail(err, "unterminated comment");
            pos_ = end + 2;
            continue;
        }
        break;
    }
    return true;
}

bool JsonParser::parseValue(JsonValue& out, JsonParseError& err, int depth) {
    if (depth > kMaxJsonNestingDepth)
        return fail(err, "JSON nesting too deep");
    if (pos_ >= text_.size())
        return fail(err, "unexpected end of JSON input");
    const char c = text_[pos_];
    if (c == '{') return parseObject(out, err, depth);
    if (c == '[') return parseArray(out, err, depth);
    if (c == '"') {
        out.type = JsonValue::Type::String;
        return parseString(out.stringValue, err);
    }
    if (c == 't') return parseLiteral("true", true, out, err);
    if (c == 'f') return parseLiteral("false", false, out, err);
    if (c == 'n') return parseLiteral("null", false, out, err);
    if (c == '-' || (c >= '0' && c <= '9'))
        return parseNumber(out, err);
    return fail(err, std::string("unexpected character '") + c + "'");
}

bool JsonParser::parseLiteral(const std::string& lit, bool boolVal, JsonValue& out,
                  JsonParseError& err) {
    if (text_.compare(pos_, lit.size(), lit) != 0)
        return fail(err, "invalid literal");
    pos_ += lit.size();
    if (lit == "null") {
        out.type = JsonValue::Type::Null;
    } else {
        out.type = JsonValue::Type::Bool;
        out.boolValue = boolVal;
    }
    return true;
}

bool JsonParser::parseNumber(JsonValue& out, JsonParseError& err) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-')
        ++pos_;
    if (pos_ >= text_.size() ||
        !std::isdigit(static_cast<unsigned char>(text_[pos_])))
        return fail(err, "invalid number");
    if (text_[pos_] == '0') {
        ++pos_;
    } else {
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
        ++pos_;
        if (pos_ >= text_.size() ||
            !std::isdigit(static_cast<unsigned char>(text_[pos_])))
            return fail(err, "invalid number");
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }
    if (pos_ < text_.size() &&
        (text_[pos_] == 'e' || text_[pos_] == 'E')) {
        ++pos_;
        if (pos_ < text_.size() &&
            (text_[pos_] == '+' || text_[pos_] == '-'))
            ++pos_;
        if (pos_ >= text_.size() ||
            !std::isdigit(static_cast<unsigned char>(text_[pos_])))
            return fail(err, "invalid number");
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }
    try {
        out.numberValue =
            std::stod(text_.substr(start, pos_ - start));
    } catch (...) {
        return fail(err, "invalid number");
    }
    out.type = JsonValue::Type::Number;
    return true;
}

void JsonParser::appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool JsonParser::parseHex4(JsonParseError& err, unsigned int& value) {
    if (pos_ + 4 > text_.size())
        return fail(err, "invalid unicode escape");
    value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = text_[pos_++];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value += static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value += static_cast<unsigned int>(10 + (c - 'a'));
        else if (c >= 'A' && c <= 'F')
            value += static_cast<unsigned int>(10 + (c - 'A'));
        else
            return fail(err, "invalid unicode escape");
    }
    return true;
}

bool JsonParser::parseString(std::string& out, JsonParseError& err) {
    // Assumes opening quote at pos_.
    ++pos_;
    out.clear();
    while (true) {
        if (pos_ >= text_.size())
            return fail(err, "unterminated string");
        const char c = text_[pos_++];
        if (c == '"')
            return true;
        if (static_cast<unsigned char>(c) < 0x20)
            return fail(err, "unescaped control character in string");
        if (c != '\\') {
            out += c;
            continue;
        }
        if (pos_ >= text_.size())
            return fail(err, "unterminated escape in string");
        const char e = text_[pos_++];
        switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
            unsigned int cp = 0;
            if (!parseHex4(err, cp))
                return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (pos_ + 1 < text_.size() &&
                    text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                    pos_ += 2;
                    unsigned int low = 0;
                    if (!parseHex4(err, low))
                        return false;
                    if (low < 0xDC00 || low > 0xDFFF) {
                        return fail(err,
                                    "invalid low surrogate in string");
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) +
                         (low - 0xDC00);
                } else {
                    return fail(err, "missing low surrogate in string");
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return fail(err, "unexpected low surrogate in string");
            }
            appendUtf8(out, cp);
            break;
        }
        default:
            return fail(err, "invalid escape in string");
        }
    }
}

bool JsonParser::parseObject(JsonValue& out, JsonParseError& err, int depth) {
    out.type = JsonValue::Type::Object;
    out.objectValue.clear();
    ++pos_; // '{'
    if (!skipIgnored(err))
        return false;
    if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return true;
    }
    while (true) {
        if (!skipIgnored(err))
            return false;
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return fail(err, "expected string key in object");
        std::string key;
        if (!parseString(key, err))
            return false;
        if (!skipIgnored(err))
            return false;
        if (pos_ >= text_.size() || text_[pos_] != ':')
            return fail(err, "expected ':' in object");
        ++pos_;
        if (!skipIgnored(err))
            return false;
        JsonValue value;
        if (!parseValue(value, err, depth + 1))
            return false;
        out.objectValue.emplace_back(std::move(key),
                                     std::move(value));
        if (!skipIgnored(err))
            return false;
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        if (pos_ < text_.size() && text_[pos_] == ',') {
            ++pos_;
            if (!skipIgnored(err))
                return false;
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true; // tolerate trailing comma
            }
            continue;
        }
        return fail(err, "expected ',' or '}' in object");
    }
}

bool JsonParser::parseArray(JsonValue& out, JsonParseError& err, int depth) {
    out.type = JsonValue::Type::Array;
    out.arrayValue.clear();
    ++pos_; // '['
    if (!skipIgnored(err))
        return false;
    if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return true;
    }
    while (true) {
        if (!skipIgnored(err))
            return false;
        JsonValue value;
        if (!parseValue(value, err, depth + 1))
            return false;
        out.arrayValue.push_back(std::move(value));
        if (!skipIgnored(err))
            return false;
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        if (pos_ < text_.size() && text_[pos_] == ',') {
            ++pos_;
            if (!skipIgnored(err))
                return false;
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true; // tolerate trailing comma
            }
            continue;
        }
        return fail(err, "expected ',' or ']' in array");
    }
}

// ---------------------------------------------------------------------------
// Location + scalar helpers
// ---------------------------------------------------------------------------

std::string json_location(const std::string& text, std::size_t pos) {
    std::size_t line = 1;
    std::size_t col = 1;
    for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    return "line " + std::to_string(line) + ", column " +
           std::to_string(col);
}

bool json_number_to_int(const JsonValue& value, int& out) {
    if (value.type != JsonValue::Type::Number)
        return false;
    const double d = value.numberValue;
    if (std::floor(d) != d || d < -kLevelValueLimit || d > kLevelValueLimit)
        return false;
    out = static_cast<int>(d);
    return true;
}

bool json_scalar_to_int(const JsonValue& value, int& out) {
    if (value.type == JsonValue::Type::Number)
        return json_number_to_int(value, out);
    if (value.type == JsonValue::Type::String)
        return parse_int_strict(value.stringValue, out);
    return false;
}

} // namespace logamuffin
