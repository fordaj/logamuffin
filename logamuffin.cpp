
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstdio>
#include <exception>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <sys/file.h>
    #include <sys/ioctl.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <cerrno>
#endif

#include "version.hpp"
#include "logamuffin.hpp"

namespace {

using namespace logamuffin;

// --- Extensible categories, colors, JSON/env configuration (issue #4) ---

std::string lowercase(std::string s) {
    for (char& c : s)
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() &&
           std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    std::size_t end = s.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

// Normalized key for config fields / color names: lowercase, no separators.
std::string normalize_key(const std::string& s) {
    const std::string low = lowercase(trim(s));
    std::string out;
    out.reserve(low.size());
    for (char c : low) {
        if (c == '_' || c == '-' || c == ' ')
            continue;
        out += c;
    }
    return out;
}

bool parse_int_strict(const std::string& raw, int& out) {
    const std::string s = trim(raw);
    if (s.empty())
        return false;
    std::size_t i = 0;
    if (s[i] == '+' || s[i] == '-') {
        ++i;
        if (i >= s.size())
            return false;
    }
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    try {
        const long value = std::stol(s);
        if (value < -1000000 || value > 1000000)
            return false;
        out = static_cast<int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Log levels and configuration
// ---------------------------------------------------------------------------

struct LogLevel {
    std::string name;   // canonical uppercase name
    std::string color;  // ANSI escape sequence
    int value = 0;
};

struct Config {
    std::vector<LogLevel> levels;
    int thresholdValue = 20; // default: INFO
    std::optional<bool> colorEnabled;
    std::optional<std::string> logFile;
    std::optional<std::string> source;
};

std::vector<LogLevel> default_levels() {
    return {
        {"ALWAYS",   std::string(ANSI_PURPLE), -1},
        {"TRACE",    std::string(ANSI_CYAN),   5},
        {"DEBUG",    std::string(ANSI_GREEN),  10},
        {"INFO",     std::string(ANSI_RESET),  20},
        {"WARN",     std::string(ANSI_YELLOW), 30},
        {"ERROR",    std::string(ANSI_RED),    40},
        {"CRITICAL", std::string(ANSI_PURPLE), 50},
    };
}

void sort_levels(std::vector<LogLevel>& levels) {
    std::sort(levels.begin(), levels.end(),
        [](const LogLevel& a, const LogLevel& b) {
            if (a.value != b.value)
                return a.value < b.value;
            return a.name < b.name;
        });
}

bool valid_level_name(const std::string& upperName) {
    if (upperName.empty() || upperName.size() > 64)
        return false;
    for (char c : upperName) {
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    }
    return true;
}

const LogLevel* find_level_exact(const std::vector<LogLevel>& levels,
                                 const std::string& upperName) {
    for (const auto& entry : levels)
        if (entry.name == upperName)
            return &entry;
    return nullptr;
}

LogLevel* find_level_mutable(std::vector<LogLevel>& levels,
                             const std::string& upperName) {
    for (auto& entry : levels)
        if (entry.name == upperName)
            return &entry;
    return nullptr;
}

const LogLevel* find_level_with_alias(const std::vector<LogLevel>& levels,
                                      const std::string& upperName) {
    if (const LogLevel* found = find_level_exact(levels, upperName))
        return found;
    std::string alias;
    if (upperName == "WARN")
        alias = "WARNING";
    else if (upperName == "WARNING")
        alias = "WARN";
    else if (upperName == "FATAL")
        alias = "CRITICAL";
    else if (upperName == "CRITICAL")
        alias = "FATAL";
    if (!alias.empty())
        return find_level_exact(levels, alias);
    return nullptr;
}

bool upsert_level(std::vector<LogLevel>& levels,
                  const std::string& upperName,
                  std::optional<int> value,
                  std::optional<std::string> colorEscape,
                  std::string& err) {
    if (!valid_level_name(upperName)) {
        err = "invalid log level name '" + upperName +
              "' (use letters, digits, '_' or '-')";
        return false;
    }
    if (LogLevel* existing = find_level_mutable(levels, upperName)) {
        if (value.has_value())
            existing->value = *value;
        if (colorEscape.has_value())
            existing->color = *colorEscape;
        return true;
    }
    if (!value.has_value()) {
        err = "log level '" + upperName +
              "' has no numeric value (add one, e.g. " +
              upperName + "=25:blue)";
        return false;
    }
    LogLevel entry;
    entry.name = upperName;
    entry.color = colorEscape.has_value() ? *colorEscape
                                          : std::string(ANSI_RESET);
    entry.value = *value;
    levels.push_back(std::move(entry));
    return true;
}

bool is_error_level(const LogLevel& level) {
    const std::string& n = level.name;
    const bool errorName =
        (n == "ERROR" || n == "CRITICAL" || n == "FATAL");
    return errorName || level.value >= 40;
}

std::string valid_levels_string(const Config& cfg) {
    std::string out;
    for (std::size_t i = 0; i < cfg.levels.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += cfg.levels[i].name;
        // Mention built-in aliases so usage errors list every documented
        // name (WARN/WARNING, CRITICAL/FATAL).
        if (cfg.levels[i].name == "WARN")
            out += "/WARNING";
        else if (cfg.levels[i].name == "CRITICAL")
            out += "/FATAL";
    }
    return out;
}

// ---------------------------------------------------------------------------
// Color resolution
// ---------------------------------------------------------------------------

bool color_name_to_escape(const std::string& normalized, std::string& out) {
    const std::string& n = normalized;
    using SV = std::string_view;
    auto ret = [&](SV v) {
        out = std::string(v);
        return true;
    };
    if (n == "reset" || n == "none" || n == "default" || n == "plain" ||
        n == "" )
        return ret(ANSI_RESET);
    if (n == "black") return ret(ANSI_BLACK);
    if (n == "red") return ret(ANSI_RED);
    if (n == "green") return ret(ANSI_GREEN);
    if (n == "yellow") return ret(ANSI_YELLOW);
    if (n == "blue") return ret(ANSI_BLUE);
    if (n == "purple" || n == "magenta") return ret(ANSI_PURPLE);
    if (n == "cyan") return ret(ANSI_CYAN);
    if (n == "white") return ret(ANSI_WHITE);
    if (n == "boldblack") return ret(ANSI_BOLD_BLACK);
    if (n == "boldred") return ret(ANSI_BOLD_RED);
    if (n == "boldgreen") return ret(ANSI_BOLD_GREEN);
    if (n == "boldyellow") return ret(ANSI_BOLD_YELLOW);
    if (n == "boldblue") return ret(ANSI_BOLD_BLUE);
    if (n == "boldpurple" || n == "boldmagenta") return ret(ANSI_BOLD_PURPLE);
    if (n == "boldcyan") return ret(ANSI_BOLD_CYAN);
    if (n == "boldwhite") return ret(ANSI_BOLD_WHITE);
    if (n == "underlineblack") return ret(ANSI_UNDERLINE_BLACK);
    if (n == "underlinered") return ret(ANSI_UNDERLINE_RED);
    if (n == "underlinegreen") return ret(ANSI_UNDERLINE_GREEN);
    if (n == "underlineyellow") return ret(ANSI_UNDERLINE_YELLOW);
    if (n == "underlineblue") return ret(ANSI_UNDERLINE_BLUE);
    if (n == "underlinepurple" || n == "underlinemagenta")
        return ret(ANSI_UNDERLINE_PURPLE);
    if (n == "underlinecyan") return ret(ANSI_UNDERLINE_CYAN);
    if (n == "underlinewhite") return ret(ANSI_UNDERLINE_WHITE);
    if (n == "bgblack" || n == "backgroundblack") return ret(ANSI_BG_BLACK);
    if (n == "bgred" || n == "backgroundred") return ret(ANSI_BG_RED);
    if (n == "bggreen" || n == "backgroundgreen") return ret(ANSI_BG_GREEN);
    if (n == "bgyellow" || n == "backgroundyellow") return ret(ANSI_BG_YELLOW);
    if (n == "bgblue" || n == "backgroundblue") return ret(ANSI_BG_BLUE);
    if (n == "bgpurple" || n == "backgroundpurple" ||
        n == "bgmagenta" || n == "backgroundmagenta")
        return ret(ANSI_BG_PURPLE);
    if (n == "bgcyan" || n == "backgroundcyan") return ret(ANSI_BG_CYAN);
    if (n == "bgwhite" || n == "backgroundwhite") return ret(ANSI_BG_WHITE);
    if (n == "hiblack" || n == "brightblack" || n == "gray" ||
        n == "grey" || n == "darkgray" || n == "darkgrey")
        return ret(ANSI_HI_BLACK);
    if (n == "hired" || n == "brightred") return ret(ANSI_HI_RED);
    if (n == "higreen" || n == "brightgreen") return ret(ANSI_HI_GREEN);
    if (n == "hiyellow" || n == "brightyellow") return ret(ANSI_HI_YELLOW);
    if (n == "hiblue" || n == "brightblue") return ret(ANSI_HI_BLUE);
    if (n == "hipurple" || n == "brightpurple" ||
        n == "himagenta" || n == "brightmagenta")
        return ret(ANSI_HI_PURPLE);
    if (n == "hicyan" || n == "brightcyan") return ret(ANSI_HI_CYAN);
    if (n == "hiwhite" || n == "brightwhite" || n == "lightgray" ||
        n == "lightgrey" || n == "silver")
        return ret(ANSI_HI_WHITE);
    if (n == "boldhiblack" || n == "boldbrightblack")
        return ret(ANSI_BOLD_HI_BLACK);
    if (n == "boldhired" || n == "boldbrightred")
        return ret(ANSI_BOLD_HI_RED);
    if (n == "boldhigreen" || n == "boldbrightgreen")
        return ret(ANSI_BOLD_HI_GREEN);
    if (n == "boldhiyellow" || n == "boldbrightyellow")
        return ret(ANSI_BOLD_HI_YELLOW);
    if (n == "boldhiblue" || n == "boldbrightblue")
        return ret(ANSI_BOLD_HI_BLUE);
    if (n == "boldhipurple" || n == "boldbrightpurple" ||
        n == "boldhimagenta" || n == "boldbrightmagenta")
        return ret(ANSI_BOLD_HI_PURPLE);
    if (n == "boldhicyan" || n == "boldbrightcyan")
        return ret(ANSI_BOLD_HI_CYAN);
    if (n == "boldhiwhite" || n == "boldbrightwhite")
        return ret(ANSI_BOLD_HI_WHITE);
    if (n == "bghiblack" || n == "backgroundhiblack")
        return ret(ANSI_BG_HI_BLACK);
    if (n == "bghired" || n == "backgroundhired")
        return ret(ANSI_BG_HI_RED);
    if (n == "bghigreen" || n == "backgroundhigreen")
        return ret(ANSI_BG_HI_GREEN);
    if (n == "bghiyellow" || n == "backgroundhiyellow")
        return ret(ANSI_BG_HI_YELLOW);
    if (n == "bghiblue" || n == "backgroundhiblue")
        return ret(ANSI_BG_HI_BLUE);
    if (n == "bghipurple" || n == "backgroundhipurple" ||
        n == "bghimagenta" || n == "backgroundhimagenta")
        return ret(ANSI_BG_HI_PURPLE);
    if (n == "bghicyan" || n == "backgroundhicyan")
        return ret(ANSI_BG_HI_CYAN);
    if (n == "bghiwhite" || n == "backgroundhiwhite")
        return ret(ANSI_BG_HI_WHITE);
    return false;
}

std::string replace_literal_escapes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == '0' && i + 3 < s.size() &&
                s[i + 2] == '3' && s[i + 3] == '3') {
                out += '\x1b';
                i += 4;
                continue;
            }
            if (s[i + 1] == 'e') {
                out += '\x1b';
                i += 2;
                continue;
            }
            if ((s[i + 1] == 'x' || s[i + 1] == 'X') &&
                i + 3 < s.size() &&
                s[i + 2] == '1' && (s[i + 3] == 'b' || s[i + 3] == 'B')) {
                out += '\x1b';
                i += 4;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

bool is_sgr_code(const std::string& s) {
    if (s.empty())
        return false;
    bool hasDigit = false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)))
            hasDigit = true;
        else if (c != ';' && c != ',')
            return false;
    }
    return hasDigit;
}

bool resolve_color(const std::string& specRaw, std::string& outEscape,
                   std::string& err) {
    const std::string spec = trim(specRaw);
    if (spec.empty()) {
        err = "empty color";
        return false;
    }
    // Full escape sequence containing a real ESC byte.
    if (spec.find('\x1b') != std::string::npos) {
        if (spec.find('[') != std::string::npos && spec.back() == 'm') {
            outEscape = spec;
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Literal "\033" / "\e" / "\x1b" text.
    if (spec.find('\\') != std::string::npos) {
        const std::string expanded = replace_literal_escapes(spec);
        if (expanded.find('\x1b') != std::string::npos) {
            if (expanded.find('[') != std::string::npos &&
                expanded.back() == 'm') {
                outEscape = expanded;
                return true;
            }
            err = "invalid color '" + specRaw + "'";
            return false;
        }
        // Fall through: a backslash otherwise is invalid.
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // "[0;31m" without ESC.
    if (spec.front() == '[' && spec.back() == 'm') {
        std::string inner = spec.substr(1, spec.size() - 2);
        if (is_sgr_code(inner)) {
            outEscape = std::string("\x1b") + spec;
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Hex truecolor: #RRGGBB or #RGB.
    if (spec.front() == '#') {
        const std::string hex = spec.substr(1);
        auto hexDigit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        if (hex.size() == 6) {
            for (char c : hex)
                if (hexDigit(c) < 0) {
                    err = "invalid color '" + specRaw + "'";
                    return false;
                }
            const int r = hexDigit(hex[0]) * 16 + hexDigit(hex[1]);
            const int g = hexDigit(hex[2]) * 16 + hexDigit(hex[3]);
            const int b = hexDigit(hex[4]) * 16 + hexDigit(hex[5]);
            outEscape = "\x1b[38;2;" + std::to_string(r) + ";" +
                        std::to_string(g) + ";" + std::to_string(b) + "m";
            return true;
        }
        if (hex.size() == 3) {
            for (char c : hex)
                if (hexDigit(c) < 0) {
                    err = "invalid color '" + specRaw + "'";
                    return false;
                }
            const int r = hexDigit(hex[0]) * 17;
            const int g = hexDigit(hex[1]) * 17;
            const int b = hexDigit(hex[2]) * 17;
            outEscape = "\x1b[38;2;" + std::to_string(r) + ";" +
                        std::to_string(g) + ";" + std::to_string(b) + "m";
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Bare SGR code: "31", "0;31", "1;91", ...
    if (is_sgr_code(spec)) {
        std::string code = spec;
        std::replace(code.begin(), code.end(), ',', ';');
        outEscape = "\x1b[" + code + "m";
        return true;
    }
    // Named color (case-insensitive, separators ignored).
    if (color_name_to_escape(normalize_key(spec), outEscape))
        return true;
    err = "unknown color '" + specRaw + "'";
    return false;
}

// ---------------------------------------------------------------------------
// Minimal JSON parser (no external dependencies)
// ---------------------------------------------------------------------------

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
    explicit JsonParser(std::string text) : text_(std::move(text)) {
        if (text_.size() >= 3 &&
            static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
    }

    bool parse(JsonValue& out, JsonParseError& err) {
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

private:
    std::string text_;
    std::size_t pos_ = 0;

    bool fail(JsonParseError& err, const std::string& msg) {
        err = {msg, pos_};
        return false;
    }

    bool skipIgnored(JsonParseError& err) {
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

    bool parseValue(JsonValue& out, JsonParseError& err, int depth) {
        if (depth > 64)
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

    bool parseLiteral(const std::string& lit, bool boolVal, JsonValue& out,
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

    bool parseNumber(JsonValue& out, JsonParseError& err) {
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

    static void appendUtf8(std::string& out, unsigned int cp) {
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

    bool parseHex4(JsonParseError& err, unsigned int& value) {
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

    bool parseString(std::string& out, JsonParseError& err) {
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

    bool parseObject(JsonValue& out, JsonParseError& err, int depth) {
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

    bool parseArray(JsonValue& out, JsonParseError& err, int depth) {
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
};

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
    if (std::floor(d) != d || d < -1000000 || d > 1000000)
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

// "NAME=VALUE:COLOR" | "NAME=VALUE" | "NAME=COLOR" | "NAME:VALUE[:COLOR]" ...
bool parse_level_entry(const std::string& rawEntry, std::string& outName,
                       std::optional<int>& outValue,
                       std::optional<std::string>& outColor,
                       std::string& err) {
    const std::string entry = trim(rawEntry);
    if (entry.empty()) {
        err = "empty log level entry";
        return false;
    }
    const std::size_t eq = entry.find('=');
    const std::size_t colon = entry.find(':');
    std::string namePart;
    std::string rest;
    if (eq != std::string::npos && (colon == std::string::npos || eq < colon)) {
        namePart = entry.substr(0, eq);
        rest = trim(entry.substr(eq + 1));
    } else if (colon != std::string::npos) {
        namePart = entry.substr(0, colon);
        rest = trim(entry.substr(colon + 1));
    } else {
        namePart = entry;
        rest = "";
    }
    outName = uppercase(trim(namePart));
    if (!valid_level_name(outName)) {
        err = "invalid log level name '" + trim(namePart) + "'";
        return false;
    }
    outValue.reset();
    outColor.reset();
    if (rest.empty())
        return true;
    const std::size_t colon2 = rest.find(':');
    if (colon2 != std::string::npos) {
        const std::string left = trim(rest.substr(0, colon2));
        const std::string right = trim(rest.substr(colon2 + 1));
        int value = 0;
        if (!parse_int_strict(left, value) || right.empty()) {
            err = "invalid log level entry '" + entry +
                  "' (expected NAME=VALUE:COLOR)";
            return false;
        }
        std::string escape;
        std::string colorErr;
        if (!resolve_color(right, escape, colorErr)) {
            err = "invalid log level entry '" + entry + "': " + colorErr;
            return false;
        }
        outValue = value;
        outColor = escape;
        return true;
    }
    int value = 0;
    if (parse_int_strict(rest, value)) {
        outValue = value;
        return true;
    }
    std::string escape;
    std::string colorErr;
    if (!resolve_color(rest, escape, colorErr)) {
        err = "invalid log level entry '" + entry + "': " + colorErr;
        return false;
    }
    outColor = escape;
    return true;
}

bool parse_bool_string(const std::string& raw, bool& out) {
    const std::string v = uppercase(trim(raw));
    if (v == "1" || v == "TRUE" || v == "YES" || v == "ON") {
        out = true;
        return true;
    }
    if (v == "0" || v == "FALSE" || v == "NO" || v == "OFF") {
        out = false;
        return true;
    }
    return false;
}

bool json_to_bool(const JsonValue& value, bool& out, std::string& err) {
    if (value.type == JsonValue::Type::Bool) {
        out = value.boolValue;
        return true;
    }
    if (value.type == JsonValue::Type::Number) {
        out = (value.numberValue != 0.0);
        return true;
    }
    if (value.type == JsonValue::Type::String) {
        if (parse_bool_string(value.stringValue, out))
            return true;
    }
    err = "expected a boolean (true/false)";
    return false;
}

bool json_to_color(const JsonValue& value, std::string& outEscape,
                   std::string& err) {
    if (value.type == JsonValue::Type::String)
        return resolve_color(value.stringValue, outEscape, err);
    if (value.type == JsonValue::Type::Number) {
        int code = 0;
        if (!json_number_to_int(value, code) || code < 0 || code > 999) {
            err = "invalid color code (expected 0-999)";
            return false;
        }
        outEscape = "\x1b[" + std::to_string(code) + "m";
        return true;
    }
    err = "expected a color string";
    return false;
}

bool apply_level_object_fields(const JsonValue& objectValue,
                               std::optional<int>& value,
                               std::optional<std::string>& colorEscape,
                               std::string& err) {
    value.reset();
    colorEscape.reset();
    for (const auto& kv : objectValue.objectValue) {
        const std::string key = normalize_key(kv.first);
        if (key == "value" || key == "level" || key == "num" ||
            key == "number" || key == "numeric" || key == "priority") {
            int intValue = 0;
            if (!json_scalar_to_int(kv.second, intValue)) {
                err = "level value must be an integer";
                return false;
            }
            value = intValue;
        } else if (key == "color" || key == "colour" || key == "ansi" ||
                   key == "escape" || key == "code") {
            std::string escape;
            if (!json_to_color(kv.second, escape, err))
                return false;
            colorEscape = escape;
        } else if (key == "name") {
            continue; // name comes from the object key / "name" field
        } else {
            continue; // ignore unknown fields for forward compatibility
        }
    }
    return true;
}

bool apply_levels_json(Config& cfg, const JsonValue& value,
                       std::string& err) {
    if (value.type == JsonValue::Type::Object) {
        for (const auto& kv : value.objectValue) {
            const std::string name = uppercase(trim(kv.first));
            if (!valid_level_name(name)) {
                err = "invalid log level name '" + trim(kv.first) + "'";
                return false;
            }
            const JsonValue& def = kv.second;
            if (def.type == JsonValue::Type::Number) {
                int intValue = 0;
                if (!json_number_to_int(def, intValue)) {
                    err = "level '" + name +
                          "' value must be an integer";
                    return false;
                }
                if (!upsert_level(cfg.levels, name, intValue,
                                  std::nullopt, err))
                    return false;
            } else if (def.type == JsonValue::Type::String) {
                int intValue = 0;
                if (parse_int_strict(def.stringValue, intValue)) {
                    if (!upsert_level(cfg.levels, name, intValue,
                                      std::nullopt, err))
                        return false;
                } else {
                    std::string escape;
                    if (!resolve_color(def.stringValue, escape, err)) {
                        err = "level '" + name + "': " + err;
                        return false;
                    }
                    if (!upsert_level(cfg.levels, name, std::nullopt,
                                      escape, err))
                        return false;
                }
            } else if (def.type == JsonValue::Type::Object) {
                std::optional<int> intValue;
                std::optional<std::string> escape;
                if (!apply_level_object_fields(def, intValue, escape, err)) {
                    err = "level '" + name + "': " + err;
                    return false;
                }
                if (!upsert_level(cfg.levels, name, intValue, escape, err))
                    return false;
            } else {
                err = "level '" + name +
                      "' must be a number, color string, or object with "
                      "\"value\"/\"color\"";
                return false;
            }
        }
        return true;
    }
    if (value.type == JsonValue::Type::Array) {
        for (const auto& element : value.arrayValue) {
            if (element.type == JsonValue::Type::String) {
                std::string name;
                std::optional<int> entryValue;
                std::optional<std::string> entryColor;
                std::string entryErr;
                if (!parse_level_entry(element.stringValue, name,
                                       entryValue, entryColor, entryErr)) {
                    err = entryErr;
                    return false;
                }
                if (!upsert_level(cfg.levels, name, entryValue,
                                  entryColor, err))
                    return false;
            } else if (element.type == JsonValue::Type::Object) {
                const JsonValue* nameValue = nullptr;
                for (const auto& kv : element.objectValue) {
                    if (normalize_key(kv.first) == "name") {
                        nameValue = &kv.second;
                        break;
                    }
                }
                if (!nameValue ||
                    nameValue->type != JsonValue::Type::String) {
                    err = "level object is missing a string \"name\"";
                    return false;
                }
                const std::string name =
                    uppercase(trim(nameValue->stringValue));
                if (!valid_level_name(name)) {
                    err = "invalid log level name '" +
                          trim(nameValue->stringValue) + "'";
                    return false;
                }
                std::optional<int> intValue;
                std::optional<std::string> escape;
                if (!apply_level_object_fields(element, intValue, escape,
                                               err))
                    return false;
                if (!upsert_level(cfg.levels, name, intValue, escape, err))
                    return false;
            } else {
                err = "levels array entries must be strings or objects "
                      "with \"name\"/\"value\"/\"color\"";
                return false;
            }
        }
        return true;
    }
    err = "\"levels\" must be an object or array";
    return false;
}

bool apply_file_json(Config& cfg, const JsonValue& root,
                     std::optional<std::string>& pendingThresholdName,
                     std::optional<int>& pendingThresholdNumber,
                     std::string& err) {
    if (!root.isObject()) {
        err = "root must be a JSON object";
        return false;
    }
    pendingThresholdName.reset();
    pendingThresholdNumber.reset();
    for (const auto& kv : root.objectValue) {
        const std::string key = normalize_key(kv.first);
        const JsonValue& value = kv.second;
        if (key == "level" || key == "minlevel" || key == "threshold" ||
            key == "loglevel" || key == "minimumlevel" ||
            key == "minloglevel") {
            if (value.type == JsonValue::Type::Number) {
                int intValue = 0;
                if (!json_number_to_int(value, intValue)) {
                    err = "\"level\" must be an integer or a level name";
                    return false;
                }
                pendingThresholdNumber = intValue;
                pendingThresholdName.reset();
            } else if (value.type == JsonValue::Type::String) {
                int intValue = 0;
                if (parse_int_strict(value.stringValue, intValue)) {
                    pendingThresholdNumber = intValue;
                    pendingThresholdName.reset();
                } else {
                    pendingThresholdName =
                        uppercase(trim(value.stringValue));
                    pendingThresholdNumber.reset();
                }
            } else {
                err = "\"level\" must be an integer or a level name";
                return false;
            }
        } else if (key == "color" || key == "colors" || key == "ansi" ||
                   key == "colored" || key == "coloroutput") {
            bool colorValue = false;
            if (!json_to_bool(value, colorValue, err)) {
                err = "\"color\": " + err;
                return false;
            }
            cfg.colorEnabled = colorValue;
        } else if (key == "file" || key == "logfile" ||
                   key == "logfilepath" || key == "filepath" ||
                   key == "path" || key == "logpath" ||
                   key == "fileoutput") {
            if (value.type != JsonValue::Type::String ||
                trim(value.stringValue).empty()) {
                err = "\"file\" must be a non-empty path string";
                return false;
            }
            cfg.logFile = value.stringValue;
        } else if (key == "source" || key == "logsource" ||
                   key == "sourcename") {
            if (value.type != JsonValue::Type::String ||
                trim(value.stringValue).empty()) {
                err = "\"source\" must be a non-empty string";
                return false;
            }
            cfg.source = value.stringValue;
        } else if (key == "levels" || key == "categories" ||
                   key == "loglevels" || key == "logcategories" ||
                   key == "leveldefinitions" || key == "leveldefs" ||
                   key == "leveldef" || key == "logleveldefinitions") {
            if (!apply_levels_json(cfg, value, err))
                return false;
        } else {
            continue; // unknown top-level keys ignored
        }
    }
    return true;
}

std::vector<std::string> split_level_list(const std::string& raw) {
    std::vector<std::string> entries;
    std::string current;
    for (char c : raw) {
        if (c == ',' || c == ';') {
            entries.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    entries.push_back(current);
    return entries;
}

bool apply_env_levels(Config& cfg, std::string& err) {
    // LOGAMUFFIN_* aliases, then LOG_CATEGORIES, then LOG_LEVELS wins.
    for (const char* envName : {"LOGAMUFFIN_LEVELS",
                                "LOGAMUFFIN_CATEGORIES",
                                "LOG_CATEGORIES", "LOG_LEVELS"}) {
        const char* raw = std::getenv(envName);
        if (!raw || !*raw)
            continue;
        const std::string trimmedRaw = trim(raw);
        // A JSON object/array is accepted as an alternative to the
        // comma-separated NAME=VALUE:COLOR list.
        if (!trimmedRaw.empty() &&
            (trimmedRaw.front() == '{' || trimmedRaw.front() == '[')) {
            JsonParser parser(trimmedRaw);
            JsonValue root;
            JsonParseError parseErr;
            if (!parser.parse(root, parseErr)) {
                err = std::string("Invalid ") + envName + ": " +
                      parseErr.message + " at " +
                      json_location(trimmedRaw, parseErr.pos);
                return false;
            }
            if (root.isObject()) {
                const JsonValue* levels = nullptr;
                for (const auto& kv : root.objectValue) {
                    const std::string key = normalize_key(kv.first);
                    if (key == "levels" || key == "categories" ||
                        key == "loglevels" || key == "logcategories") {
                        levels = &kv.second;
                        break;
                    }
                }
                if (levels) {
                    if (!apply_levels_json(cfg, *levels, err)) {
                        err = std::string("Invalid ") + envName + ": " + err;
                        return false;
                    }
                    continue;
                }
            }
            if (!apply_levels_json(cfg, root, err)) {
                err = std::string("Invalid ") + envName + ": " + err;
                return false;
            }
            continue;
        }
        for (const auto& entryRaw : split_level_list(raw)) {
            const std::string entry = trim(entryRaw);
            if (entry.empty())
                continue;
            std::string name;
            std::optional<int> value;
            std::optional<std::string> colorEscape;
            std::string entryErr;
            if (!parse_level_entry(entry, name, value, colorEscape,
                                   entryErr)) {
                err = std::string("Invalid ") + envName + ": " + entryErr;
                return false;
            }
            if (!upsert_level(cfg.levels, name, value, colorEscape, err)) {
                err = std::string("Invalid ") + envName + ": " + err;
                return false;
            }
        }
    }
    // Per-category overrides: LOG_LEVEL_VALUE_<NAME> / LOG_LEVEL_COLOR_<NAME>
    // (and the LOG_LEVEL_<NAME>_VALUE / _COLOR spellings).
    std::vector<std::string> names;
    for (const auto& level : cfg.levels)
        names.push_back(level.name);
    for (const auto& name : names) {
        std::string suffix;
        for (char c : name)
            suffix += ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                          ? c : '_';
        const std::string valueVars[] = {
            "LOG_LEVEL_VALUE_" + suffix,
            "LOG_LEVEL_" + suffix + "_VALUE",
        };
        const std::string colorVars[] = {
            "LOG_LEVEL_COLOR_" + suffix,
            "LOG_LEVEL_" + suffix + "_COLOR",
        };
        for (const auto& var : valueVars) {
            const char* raw = std::getenv(var.c_str());
            if (!raw)
                continue;
            int value = 0;
            if (!parse_int_strict(raw, value)) {
                err = "Invalid " + var + ": '" + std::string(raw) +
                      "' (expected an integer)";
                return false;
            }
            if (!upsert_level(cfg.levels, name, value, std::nullopt, err)) {
                err = "Invalid " + var + ": " + err;
                return false;
            }
            break;
        }
        for (const auto& var : colorVars) {
            const char* raw = std::getenv(var.c_str());
            if (!raw)
                continue;
            std::string escape;
            std::string colorErr;
            if (!resolve_color(raw, escape, colorErr)) {
                err = "Invalid " + var + ": '" + std::string(raw) +
                      "' (" + colorErr + ")";
                return false;
            }
            if (!upsert_level(cfg.levels, name, std::nullopt, escape, err)) {
                err = "Invalid " + var + ": " + err;
                return false;
            }
            break;
        }
    }
    sort_levels(cfg.levels);
    return true;
}

bool resolve_threshold_and_scalars(
    Config& cfg,
    const std::optional<std::string>& pendingThresholdName,
    const std::optional<int>& pendingThresholdNumber,
    std::string& err) {
    if (const char* envFile = std::getenv("LOG_FILE")) {
        if (*envFile)
            cfg.logFile = std::string(envFile);
    }
    if (const char* envSource = std::getenv("LOG_SOURCE")) {
        if (*envSource)
            cfg.source = std::string(envSource);
    }
    if (const char* envColor = std::getenv("LOG_COLOR")) {
        const std::string v = uppercase(trim(envColor));
        cfg.colorEnabled =
            (v == "1" || v == "TRUE" || v == "YES" || v == "ON");
    }
    if (const char* envLevel = std::getenv("LOG_LEVEL")) {
        const std::string trimmed = trim(envLevel);
        int numeric = 0;
        if (parse_int_strict(trimmed, numeric)) {
            cfg.thresholdValue = numeric;
            return true;
        }
        const std::string name = uppercase(trimmed);
        if (name == "ALWAYS") {
            err = std::string("Invalid LOG_LEVEL: ") + envLevel;
            return false;
        }
        const LogLevel* found =
            find_level_with_alias(cfg.levels, name);
        if (!found) {
            err = std::string("Invalid LOG_LEVEL: ") + envLevel;
            return false;
        }
        cfg.thresholdValue = found->value;
        return true;
    }
    if (pendingThresholdNumber.has_value()) {
        cfg.thresholdValue = *pendingThresholdNumber;
        return true;
    }
    if (pendingThresholdName.has_value()) {
        const std::string& name = *pendingThresholdName;
        if (name == "ALWAYS") {
            err = "Invalid config level: ALWAYS cannot be used as the "
                  "minimum level";
            return false;
        }
        const LogLevel* found = find_level_with_alias(cfg.levels, name);
        if (!found) {
            err = "Invalid config level: unknown level '" + name + "'";
            return false;
        }
        cfg.thresholdValue = found->value;
        return true;
    }
    if (const LogLevel* info = find_level_exact(cfg.levels, "INFO"))
        cfg.thresholdValue = info->value;
    return true;
}

bool read_config_file(const std::string& path, std::string& content,
                      std::string& err) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        // file_size fails when the file is missing; report as open failure.
        err = "cannot open config file '" + path + "'";
        return false;
    }
    if (size > 4 * 1024 * 1024) {
        err = "config file '" + path + "' is too large (max 4 MiB)";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open config file '" + path + "'";
        return false;
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    if (in.bad()) {
        err = "failed to read config file '" + path + "'";
        return false;
    }
    content = stream.str();
    return true;
}

bool load_config(const std::optional<std::string>& cliConfigPath,
                 Config& cfg, std::string& err) {
    cfg.levels = default_levels();
    cfg.thresholdValue = 20;
    cfg.colorEnabled.reset();
    cfg.logFile.reset();
    cfg.source.reset();

    std::optional<std::string> configPath;
    if (cliConfigPath.has_value()) {
        configPath = *cliConfigPath;
    } else if (const char* envConfig = std::getenv("LOG_CONFIG")) {
        if (*envConfig)
            configPath = std::string(envConfig);
    } else if (const char* envConfig =
                   std::getenv("LOGAMUFFIN_CONFIG")) {
        if (*envConfig)
            configPath = std::string(envConfig);
    } else {
        std::error_code ec;
        if (std::filesystem::exists(".logamuffin", ec))
            configPath = ".logamuffin";
    }

    std::optional<std::string> pendingThresholdName;
    std::optional<int> pendingThresholdNumber;

    if (configPath.has_value()) {
        std::string content;
        if (!read_config_file(*configPath, content, err))
            return false;
        JsonParser parser(content);
        JsonValue root;
        JsonParseError parseErr;
        if (!parser.parse(root, parseErr)) {
            err = "invalid config file '" + *configPath + "': " +
                  parseErr.message + " at " +
                  json_location(content, parseErr.pos);
            return false;
        }
        if (!apply_file_json(cfg, root, pendingThresholdName,
                             pendingThresholdNumber, err)) {
            err = "invalid config file '" + *configPath + "': " + err;
            return false;
        }
        sort_levels(cfg.levels);
    }

    if (!apply_env_levels(cfg, err))
        return false;

    if (!resolve_threshold_and_scalars(cfg, pendingThresholdName,
                                       pendingThresholdNumber, err))
        return false;

    sort_levels(cfg.levels);
    return true;
}

// ---------------------------------------------------------------------------
// Filesystem / process helpers (from logamuffin.cpp; hostname/TTY/file lock)
// ---------------------------------------------------------------------------

std::string hostname() {
    std::string result;

#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = sizeof(buffer);

    if (GetComputerNameA(buffer, &size))
        result.assign(buffer, size);
#else
    char buffer[256]{};

    if (gethostname(buffer, sizeof(buffer) - 1) == 0)
        result = buffer;
#endif

    constexpr std::string_view suffix = ".local";

    if (result.size() >= suffix.size() &&
        result.compare(result.size() - suffix.size(),
                       suffix.size(), suffix) == 0) {
        result.resize(result.size() - suffix.size());
    }

    return result.empty() ? "unknown" : result;
}

bool terminal(FILE* stream) {
#ifdef _WIN32
    const int fd = stream == stderr ? _fileno(stderr) : _fileno(stdout);
    return _isatty(fd) != 0;
#else
    const int fd = stream == stderr ? STDERR_FILENO : STDOUT_FILENO;
    return isatty(fd) != 0;
#endif
}


int terminal_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (GetConsoleScreenBufferInfo(out, &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif

    return 120; // Sensible fallback
}

bool append_locked(
    const std::filesystem::path& path,
    const std::string& entry)
{

#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    OVERLAPPED overlap{};
    if (!LockFileEx(
            file, LOCKFILE_EXCLUSIVE_LOCK,
            0, 1, 0, &overlap)) {
        CloseHandle(file);
        return false;
    }

    LARGE_INTEGER end{};
    bool success = SetFilePointerEx(
        file, end, nullptr, FILE_END) != 0;

    std::size_t written = 0;
    while (success && written < entry.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                entry.size() - written,
                static_cast<std::size_t>(MAXDWORD)));

        DWORD count = 0;
        if (!WriteFile(
                file, entry.data() + written,
                chunk, &count, nullptr) || count == 0) {
            success = false;
            break;
        }

        written += count;
    }

    if (!UnlockFileEx(file, 0, 1, 0, &overlap))
        success = false;

    CloseHandle(file);
    return success;

    #else
    const int fd = open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_APPEND,
        0666);

    if (fd == -1)
        return false;

    bool success = false;

    if (flock(fd, LOCK_EX) == 0) {
        std::size_t written = 0;
        success = true;

        while (written < entry.size()) {
            const ssize_t count = write(
                fd,
                entry.data() + written,
                entry.size() - written);

            if (count < 0 && errno == EINTR)
                continue;

            if (count <= 0) {
                success = false;
                break;
            }

            written += static_cast<std::size_t>(count);
        }

        if (flock(fd, LOCK_UN) != 0)
            success = false;
    }

    if (close(fd) != 0)
        success = false;

    return success;
#endif
}

// Config-aware source resolution (overload of logamuffin::get_source/1):
std::string get_source(const Config& cfg, const char* argv0) {
    if (cfg.source.has_value() && !cfg.source->empty())
        return basename(*cfg.source);
    return basename(argv0 ? argv0 : "logamuffin");
}

void print_usage(std::ostream& out, const Config& cfg) {
    out << "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
           "[MESSAGE ...]\n"
        << "Levels: " << valid_levels_string(cfg) << "\n"
        << "Options:\n"
        << "  --config <path>  Use the JSON config file at <path> "
           "(takes precedence over ./.logamuffin autodiscovery)\n"
        << "  --version        Print the logamuffin version\n"
        << "  --help           Print help and exit\n"
        << "\n"
        << "Custom categories can be defined with LOG_LEVELS, the config "
           "file, or the programmatic CLI categories. "
           "See README.md (Configuration) for details.\n";
}

void print_help(std::ostream& out) {
    out << "logamuffin - cross-platform logging utility\n"
        << "\n"
        << "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
           "[MESSAGE ...]\n"
        << "\n"
        << "Default levels (name, numeric value, default color):\n"
        << "  ALWAYS   -1 purple   (bypasses the minimum level filter)\n"
        << "  TRACE     5 cyan\n"
        << "  DEBUG    10 green\n"
        << "  INFO     20 reset\n"
        << "  WARN     30 yellow  (alias: WARNING)\n"
        << "  ERROR    40 red\n"
        << "  CRITICAL 50 purple  (alias: FATAL)\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>   JSON config file (aliases: "
           "--config=<path>).\n"
        << "                    Also configurable with LOG_CONFIG.\n"
        << "  --version         Print the version and exit.\n"
        << "  --help, -h        Print this help and exit.\n"
        << "\n"
        << "Configuration (lowest to highest precedence):\n"
        << "  built-in defaults\n"
        << "  <cwd>/.logamuffin (autodiscovered JSON file; missing file\n"
        << "                     means defaults)\n"
        << "  LOG_CONFIG file, then --config file (single winning file)\n"
        << "  environment variables (LOG_LEVEL, LOG_COLOR, LOG_FILE,\n"
        << "                     LOG_SOURCE, LOG_LEVELS/LOG_CATEGORIES,\n"
        << "                     LOG_LEVEL_VALUE_<NAME>, "
           "LOG_LEVEL_COLOR_<NAME>)\n"
        << "\n"
        << "Example config file:\n"
        << "  {\n"
        << "    \"level\": \"INFO\",\n"
        << "    \"color\": true,\n"
        << "    \"levels\": {\n"
        << "      \"TRACE\": {\"value\": 5, \"color\": \"cyan\"},\n"
        << "      \"NOTICE\": {\"value\": 25, \"color\": \"blue\"}\n"
        << "    }\n"
        << "  }\n"
        << "\n"
        << "Example environment definition:\n"
        << "  LOG_LEVELS='NOTICE=25:blue,INFO=20:green' \\\n"
        << "      logamuffin NOTICE \"hello\"\n";
}

int run(int argc, char* argv[]) {
    // --version is only valid as the sole argument. With extra
    // arguments it is treated as a (unknown) level name, matching the
    // original executable and the CLI integration tests.
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "logamuffin " << LOGAMUFFIN_VERSION << '\n';
        return 0;
    }
    // Parse leading options before LEVEL.
    std::optional<std::string> cliConfigPath;
    bool wantHelp = false;
    int idx = 1;
    for (; idx < argc; ++idx) {
        const std::string arg = argv[idx];
        if (arg == "--") {
            ++idx;
            break;
        }
        if (arg == "--version") {
            // Not the sole argument: fall through as LEVEL so it is
            // reported as an unknown log level.
            break;
        }
        if (arg == "--help" || arg == "-h" || arg == "--usage") {
            wantHelp = true;
            continue;
        }
        if (arg == "--config") {
            if (idx + 1 >= argc) {
                std::cerr << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = argv[idx + 1];
            ++idx; // consumed path
            continue;
        }
        if (arg.rfind("--config=", 0) == 0) {
            const std::string path = arg.substr(9);
            if (path.empty()) {
                std::cerr
                    << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = path;
            continue;
        }
        if (arg.rfind("--", 0) == 0 || (arg.size() > 1 && arg[0] == '-')) {
            std::cerr << "logamuffin: unknown option: " << arg << '\n';
            return 2;
        }
        break;
    }

    if (wantHelp) {
        print_help(std::cout);
        return 0;
    }

    // Also accept --config immediately after LEVEL (before MESSAGE),
    // e.g. `logamuffin INFO --config file.json "message"`.
    int messageStart = 0;
    if (idx < argc && idx + 1 < argc) {
        const std::string afterLevel = argv[idx + 1];
        if (afterLevel == "--config") {
            if (idx + 2 >= argc) {
                std::cerr << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = argv[idx + 2];
            messageStart = idx + 3;
        } else if (afterLevel.rfind("--config=", 0) == 0) {
            const std::string path = afterLevel.substr(9);
            if (path.empty()) {
                std::cerr
                    << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = path;
            messageStart = idx + 2;
        } else {
            messageStart = idx + 1;
        }
    } else {
        messageStart = idx + 1;
    }

    Config cfg;
    std::string cfgErr;
    if (!load_config(cliConfigPath, cfg, cfgErr)) {
        std::cerr << "logamuffin: " << cfgErr << '\n';
        return 2;
    }

    if (idx >= argc || messageStart >= argc) {
        print_usage(std::cerr, cfg);
        return 2;
    }

    const std::string levelUpper = uppercase(trim(argv[idx]));
    const LogLevel* found =
        find_level_with_alias(cfg.levels, levelUpper);
    if (!found) {
        std::cerr << "Unknown log level: " << argv[idx]
                  << ". Valid levels: " << valid_levels_string(cfg)
                  << '\n';
        return 2;
    }

    // Copy the resolved level so filtering and output stay stable even
    // though cfg.levels is already sorted.
    const LogLevel level = *found;

    // Join all message arguments with spaces.
    std::string message;
    for (int i = messageStart; i < argc; ++i) {
        if (i > messageStart)
            message += ' ';
        message += argv[i];
    }
    message = clean_message(std::move(message));

    // Filter before doing timestamp, formatting, and file I/O.
    const bool isAlways = (level.name == "ALWAYS");
    if (!isAlways && level.value < cfg.thresholdValue) {
        return 0;
    }

    const std::string time = timestamp();
    const std::string pc = hostname();
    const std::string source = get_source(cfg, argv[0]);

    FILE* stream = is_error_level(level) ? stderr : stdout;
    const bool color = cfg.colorEnabled.has_value()
        ? *cfg.colorEnabled : terminal(stream);

const int width = terminal(stream) ? terminal_width() : 180;

std::string prefix;

// Timestamp and hostname at >= 180 columns;
// source filename at >= 120 columns.
if (width >= 180) {
    if (color) {
        prefix += ANSI_GREEN;
        prefix += time;
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += ANSI_PURPLE;
        prefix += pc;
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += ANSI_BLUE;
        prefix += source;
        prefix += ':';
        prefix += ANSI_RESET;
        prefix += ' ';
    } else {
        prefix += time + " " + pc + " " + source + ": ";
    }
} else if (width >= 120) {
    if (color) {
        prefix += ANSI_BLUE;
        prefix += source;
        prefix += ':';
        prefix += ANSI_RESET;
        prefix += ' ';
    } else {
        prefix += source + ": ";
    }
}

// Level is always included, regardless of terminal width.
if (color) {
    prefix += ANSI_HI_BLACK;
    prefix += '[';
    prefix += level.name;
    prefix += ']';
    prefix += ANSI_RESET;
    prefix += ' ';
    prefix += level.color;
} else {
    prefix += '[';
    prefix += level.name;
    prefix += "] ";
}


    std::string output;
output.reserve(prefix.size() + message.size() + 16);
output += prefix;
output += message;

    if (color)
        output += ANSI_RESET;

    output += '\n';

    std::fwrite(output.data(), 1, output.size(), stream);
    std::fflush(stream);

    // Plain-text file logging, independent of console colors.
    const std::filesystem::path file =
        cfg.logFile.has_value() && !cfg.logFile->empty()
            ? std::filesystem::path(*cfg.logFile)
            : std::filesystem::path(".logs/log.log");

    std::error_code ec;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(
            file.parent_path(), ec);

        if (ec) {
            std::cerr << "logamuffin: failed to create log directory: "
                      << ec.message() << '\n';
            return 1;
        }
    }

    // Construct the complete entry before acquiring the lock.
    std::string entry;
    entry.reserve(
        time.size() + pc.size() + source.size() +
        message.size() + level.name.size() + 8);

    entry += time;
    entry += ' ';
    entry += pc;
    entry += " [";
    entry += level.name;
    entry += "] ";
    entry += source;
    entry += ": ";
    entry += message;
    entry += '\n';

    if (!append_locked(file, entry)) {
        std::cerr << "logamuffin: failed to append log file\n";
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "logamuffin: " << e.what() << '\n';
        return 1;
    }
}
