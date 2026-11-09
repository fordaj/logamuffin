#include "json_parser.hpp"

#include <cctype>
#include <cmath>

#include "text_utils.hpp"

namespace logamuffin {

namespace {

// Maximum JSON nesting depth to reject deeply nested input.
constexpr int kMaxJsonNestingDepth = 64;

// UTF-8 BOM bytes skipped at the start of the document.
constexpr unsigned char kBomByte0 = 0xEF;
constexpr unsigned char kBomByte1 = 0xBB;
constexpr unsigned char kBomByte2 = 0xBF;
constexpr std::size_t kBomLength = 3;

// Hex escape helpers: \uXXXX decodes 4 hex digits into a code point.
constexpr int kHexDigitsPerEscape = 4;
constexpr int kHexShift = 4; // value <<= 4 per digit.

// Unicode surrogate ranges (UTF-16): high 0xD800-0xDBFF, low 0xDC00-0xDFFF.
// A high surrogate must be followed by a low surrogate; combined as
// 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00).
constexpr unsigned int kSurrogateHighMin = 0xD800;
constexpr unsigned int kSurrogateHighMax = 0xDBFF;
constexpr unsigned int kSurrogateLowMin = 0xDC00;
constexpr unsigned int kSurrogateLowMax = 0xDFFF;
constexpr unsigned int kSupplementaryPlaneBase = 0x10000;
constexpr int kSurrogateShift = 10;

// UTF-8 encoding thresholds and continuation mask (0b10xxxxxx).
constexpr unsigned int kUtf8TwoByteLimit = 0x800;
constexpr unsigned int kUtf8ThreeByteLimit = 0x10000;
constexpr unsigned int kUtf8ContinuationMask = 0x3F;
constexpr unsigned int kUtf8ContinuationBase = 0x80;
constexpr unsigned int kUtf8TwoByteLead = 0xC0;
constexpr unsigned int kUtf8ThreeByteLead = 0xE0;
constexpr unsigned int kUtf8FourByteLead = 0xF0;

} // namespace

JsonParser::JsonParser(std::string text) : text_(std::move(text)) {
    if (text_.size() >= kBomLength &&
        static_cast<unsigned char>(text_[0]) == kBomByte0 &&
        static_cast<unsigned char>(text_[1]) == kBomByte1 &&
        static_cast<unsigned char>(text_[2]) == kBomByte2) {
        pos_ = kBomLength;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Position helpers
// ---------------------------------------------------------------------------

bool JsonParser::fail(JsonParseError& err, const std::string& msg) {
    err = {msg, pos_};
    return false;
}

bool JsonParser::atEnd() const {
    return pos_ >= text_.size();
}

// Caller must ensure !atEnd().
char JsonParser::peek() const {
    return text_[pos_];
}

// Caller must ensure !atEnd().
char JsonParser::consume() {
    return text_[pos_++];
}

bool JsonParser::skipIgnored(JsonParseError& err) {
    while (true) {
        while (!atEnd() &&
               std::isspace(static_cast<unsigned char>(peek())))
            consume();
        if (pos_ + 1 < text_.size() && text_[pos_] == '/' &&
            text_[pos_ + 1] == '/') {
            while (!atEnd() && peek() != '\n')
                consume();
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

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

bool JsonParser::parseValue(JsonValue& out, JsonParseError& err, int depth) {
    if (depth > kMaxJsonNestingDepth)
        return fail(err, "JSON nesting too deep");
    if (atEnd())
        return fail(err, "unexpected end of JSON input");
    const char c = peek();
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

// ---------------------------------------------------------------------------
// Numbers: sign | int | frac | exp | convert
// ---------------------------------------------------------------------------

bool JsonParser::parseNumber(JsonValue& out, JsonParseError& err) {
    const std::size_t start = pos_;
    // sign
    if (!atEnd() && peek() == '-')
        consume();
    // integer part (must have at least one digit)
    if (atEnd() ||
        !std::isdigit(static_cast<unsigned char>(peek())))
        return fail(err, "invalid number");
    if (peek() == '0') {
        consume();
    } else {
        while (!atEnd() &&
               std::isdigit(static_cast<unsigned char>(peek())))
            consume();
    }
    // fraction part (optional ".digits")
    if (!atEnd() && peek() == '.') {
        consume(); // '.'
        if (atEnd() ||
            !std::isdigit(static_cast<unsigned char>(peek())))
            return fail(err, "invalid number");
        while (!atEnd() &&
               std::isdigit(static_cast<unsigned char>(peek())))
            consume();
    }
    // exponent part (optional "e[+-]digits")
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
        consume(); // 'e'/'E'
        if (!atEnd() && (peek() == '+' || peek() == '-'))
            consume();
        if (atEnd() ||
            !std::isdigit(static_cast<unsigned char>(peek())))
            return fail(err, "invalid number");
        while (!atEnd() &&
               std::isdigit(static_cast<unsigned char>(peek())))
            consume();
    }
    // convert
    try {
        out.numberValue =
            std::stod(text_.substr(start, pos_ - start));
    } catch (...) {
        return fail(err, "invalid number");
    }
    out.type = JsonValue::Type::Number;
    return true;
}

// ---------------------------------------------------------------------------
// Unicode helpers
// ---------------------------------------------------------------------------

void JsonParser::appendUtf8(std::string& out, unsigned int cp) {
    // ASCII (1 byte).
    if (cp < kUtf8TwoByteLead) {
        out += static_cast<char>(cp);
        return;
    }
    // 2-byte sequence (11 bits).
    if (cp < kUtf8TwoByteLimit) {
        out += static_cast<char>(kUtf8TwoByteLead | (cp >> 6));
        out += static_cast<char>(
            kUtf8ContinuationBase | (cp & kUtf8ContinuationMask));
        return;
    }
    // 3-byte sequence (16 bits).
    if (cp < kUtf8ThreeByteLimit) {
        out += static_cast<char>(kUtf8ThreeByteLead | (cp >> 12));
        out += static_cast<char>(kUtf8ContinuationBase |
                                 ((cp >> 6) & kUtf8ContinuationMask));
        out += static_cast<char>(
            kUtf8ContinuationBase | (cp & kUtf8ContinuationMask));
        return;
    }
    // 4-byte sequence (21 bits).
    out += static_cast<char>(kUtf8FourByteLead | (cp >> 18));
    out += static_cast<char>(kUtf8ContinuationBase |
                             ((cp >> 12) & kUtf8ContinuationMask));
    out += static_cast<char>(kUtf8ContinuationBase |
                             ((cp >> 6) & kUtf8ContinuationMask));
    out += static_cast<char>(
        kUtf8ContinuationBase | (cp & kUtf8ContinuationMask));
}

bool JsonParser::parseHex4(JsonParseError& err, unsigned int& value) {
    if (pos_ + static_cast<std::size_t>(kHexDigitsPerEscape) > text_.size())
        return fail(err, "invalid unicode escape");
    value = 0;
    for (int i = 0; i < kHexDigitsPerEscape; ++i) {
        const char c = text_[pos_++];
        value <<= kHexShift;
        const int digit = hex_digit_value(c);
        if (digit >= 0)
            value += static_cast<unsigned int>(digit);
        else
            return fail(err, "invalid unicode escape");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Strings: main loop + escape / unicode / surrogate helpers
// ---------------------------------------------------------------------------

bool JsonParser::parseEscape(std::string& out, JsonParseError& err) {
    if (atEnd())
        return fail(err, "unterminated escape in string");
    const char e = consume();
    switch (e) {
    case '"': out += '"'; return true;
    case '\\': out += '\\'; return true;
    case '/': out += '/'; return true;
    case 'b': out += '\b'; return true;
    case 'f': out += '\f'; return true;
    case 'n': out += '\n'; return true;
    case 'r': out += '\r'; return true;
    case 't': out += '\t'; return true;
    case 'u': return parseUnicodeEscape(out, err);
    default: return fail(err, "invalid escape in string");
    }
}

bool JsonParser::parseSurrogatePair(unsigned int high, std::string& out,
                                    JsonParseError& err) {
    // A high surrogate must be immediately followed by "\uXXXX" low.
    if (pos_ + 1 >= text_.size() ||
        text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
        return fail(err, "missing low surrogate in string");
    }
    pos_ += 2;
    unsigned int low = 0;
    if (!parseHex4(err, low))
        return false;
    if (low < kSurrogateLowMin || low > kSurrogateLowMax)
        return fail(err, "invalid low surrogate in string");
    const unsigned int cp = kSupplementaryPlaneBase +
                            ((high - kSurrogateHighMin) << kSurrogateShift) +
                            (low - kSurrogateLowMin);
    appendUtf8(out, cp);
    return true;
}

bool JsonParser::parseUnicodeEscape(std::string& out, JsonParseError& err) {
    unsigned int cp = 0;
    if (!parseHex4(err, cp))
        return false;
    if (cp >= kSurrogateHighMin && cp <= kSurrogateHighMax)
        return parseSurrogatePair(cp, out, err);
    if (cp >= kSurrogateLowMin && cp <= kSurrogateLowMax)
        return fail(err, "unexpected low surrogate in string");
    appendUtf8(out, cp);
    return true;
}

bool JsonParser::parseString(std::string& out, JsonParseError& err) {
    // Assumes opening quote at pos_.
    consume(); // opening quote
    out.clear();
    while (true) {
        if (atEnd())
            return fail(err, "unterminated string");
        const char c = consume();
        if (c == '"')
            return true;
        if (static_cast<unsigned char>(c) < 0x20)
            return fail(err, "unescaped control character in string");
        if (c != '\\') {
            out += c;
            continue;
        }
        // Backslash consumed: dispatch to escape helper.
        if (!parseEscape(out, err))
            return false;
    }
}

// ---------------------------------------------------------------------------
// Containers: shared empty + separator helpers
// ---------------------------------------------------------------------------

// True when the next token is the closing bracket; consumes it.
bool JsonParser::tryConsumeEmptyContainer(char closeChar) {
    if (!atEnd() && peek() == closeChar) {
        consume();
        return true;
    }
    return false;
}

// After an element: skips ignored text, then closes (incl. trailing
// comma) or consumes a comma and continues. Error only when neither
// shape matches (or ignored-ws fails).
JsonParser::SeparatorResult JsonParser::consumeElementSeparator(
    char closeChar, JsonParseError& err) {
    if (!skipIgnored(err))
        return SeparatorResult::Error;
    if (tryConsumeEmptyContainer(closeChar))
        return SeparatorResult::Done;
    if (!atEnd() && peek() == ',') {
        consume(); // ','
        if (!skipIgnored(err))
            return SeparatorResult::Error;
        if (tryConsumeEmptyContainer(closeChar))
            return SeparatorResult::Done; // tolerate trailing comma
        return SeparatorResult::Continue;
    }
    if (closeChar == '}')
        fail(err, "expected ',' or '}' in object");
    else
        fail(err, "expected ',' or ']' in array");
    return SeparatorResult::Error;
}

bool JsonParser::parseObject(JsonValue& out, JsonParseError& err, int depth) {
    out.type = JsonValue::Type::Object;
    out.objectValue.clear();
    consume(); // '{'
    if (!skipIgnored(err))
        return false;
    if (tryConsumeEmptyContainer('}'))
        return true;
    while (true) {
        if (!skipIgnored(err))
            return false;
        if (atEnd() || peek() != '"')
            return fail(err, "expected string key in object");
        std::string key;
        if (!parseString(key, err))
            return false;
        if (!skipIgnored(err))
            return false;
        if (atEnd() || peek() != ':')
            return fail(err, "expected ':' in object");
        consume(); // ':'
        if (!skipIgnored(err))
            return false;
        JsonValue value;
        if (!parseValue(value, err, depth + 1))
            return false;
        out.objectValue.emplace_back(std::move(key),
                                     std::move(value));
        const SeparatorResult sep = consumeElementSeparator('}', err);
        if (sep == SeparatorResult::Done)
            return true;
        if (sep == SeparatorResult::Continue)
            continue;
        return false; // error already recorded
    }
}

bool JsonParser::parseArray(JsonValue& out, JsonParseError& err, int depth) {
    out.type = JsonValue::Type::Array;
    out.arrayValue.clear();
    consume(); // '['
    if (!skipIgnored(err))
        return false;
    if (tryConsumeEmptyContainer(']'))
        return true;
    while (true) {
        if (!skipIgnored(err))
            return false;
        JsonValue value;
        if (!parseValue(value, err, depth + 1))
            return false;
        out.arrayValue.push_back(std::move(value));
        const SeparatorResult sep = consumeElementSeparator(']', err);
        if (sep == SeparatorResult::Done)
            return true;
        if (sep == SeparatorResult::Continue)
            continue;
        return false; // error already recorded
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
