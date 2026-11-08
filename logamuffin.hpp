#pragma once

// Logamuffin core helpers.
//
// Pure, independently testable logic extracted from logamuffin.cpp so that:
//   - the executable's run() in logamuffin.cpp and
//   - the C++ unit tests in tests/unit/test_logamuffin.cpp
// share one implementation.
//
// This header intentionally contains no platform-specific filesystem or
// terminal I/O (hostname, TTY detection, file locking live in logamuffin.cpp).
// Functions are inline so the header can be included from multiple
// translation units without ODR violations.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi_colors.hpp"

namespace logamuffin {

enum class Level : int {
    Always   = -1,
    Trace    = 5,
    Debug    = 10,
    Info     = 20,
    Warning  = 30,
    Error    = 40,
    Critical = 50
};

struct LevelInfo {
    std::string_view name;
    std::string_view color;
    Level level;
};

inline constexpr std::array<LevelInfo, 7> levels{{
    {"ALWAYS",   ANSI_PURPLE, Level::Always},
    {"TRACE",    ANSI_CYAN,   Level::Trace},
    {"DEBUG",    ANSI_GREEN,   Level::Debug},
    {"INFO",     ANSI_RESET,    Level::Info},
    {"WARN",     ANSI_YELLOW,   Level::Warning},
    {"ERROR",    ANSI_RED,   Level::Error},
    {"CRITICAL", ANSI_PURPLE,   Level::Critical}
}};


inline std::string uppercase(std::string s) {
    for (char& c : s)
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::string basename(std::string path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos
        ? path : path.substr(pos + 1);
}

inline bool env_enabled(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value)
        return fallback;

    const std::string v = uppercase(value);
    return v == "1" || v == "TRUE" || v == "YES" || v == "ON";
}

inline bool parse_level(std::string value, Level& out) {
    value = uppercase(std::move(value));

    if (value == "TRACE")    out = Level::Trace;
    else if (value == "DEBUG") out = Level::Debug;
    else if (value == "INFO")  out = Level::Info;
    else if (value == "WARN" || value == "WARNING")
        out = Level::Warning;
    else if (value == "ERROR") out = Level::Error;
    else if (value == "FATAL" || value == "CRITICAL")
        out = Level::Critical;
    else if (value == "ALWAYS") out = Level::Always;
    else return false;

    return true;
}

inline const LevelInfo& info(Level level) {
    for (const auto& entry : levels)
        if (entry.level == level)
            return entry;

    return levels[3];
}

inline std::string timestamp() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto millis = duration_cast<milliseconds>(
        now.time_since_epoch()).count() % 1000;

    const std::time_t time = system_clock::to_time_t(now);
    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

    char result[40]{};
    std::snprintf(result, sizeof(result), "%s,%03lld",
                  buffer, static_cast<long long>(millis));

    return result;
}

inline std::string clean_message(std::string message) {
    message.erase(
        std::remove(message.begin(), message.end(), '\r'),
        message.end());
    return message;
}

inline std::string get_source(const char* argv0) {
    const char* source = std::getenv("LOG_SOURCE");

    if (source && *source)
        return basename(source);

    return basename(argv0 ? argv0 : "logamuffin");
}

// Filtering predicate used by run(): ALWAYS bypasses the threshold,
// everything else is emitted at-or-above the threshold.
inline bool should_emit(Level level, Level threshold) {
    return level == Level::Always ||
        static_cast<int>(level) >= static_cast<int>(threshold);
}

inline std::string join_message(const std::vector<std::string>& parts) {
    std::string message;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            message += ' ';
        message += parts[i];
    }
    return clean_message(std::move(message));
}

inline std::string join_message(int argc, char* argv[], int start = 2) {
    std::vector<std::string> parts;
    for (int i = start; i < argc; ++i)
        parts.emplace_back(argv[i] ? argv[i] : "");
    return join_message(parts);
}

// Plain-text file entry, independent of console colors.
// Format: "<time> <pc> [<LEVEL>] <source>: <message>\n"
inline std::string format_file_entry(
    std::string_view time,
    std::string_view pc,
    Level level,
    std::string_view source,
    std::string_view message) {
    const auto& li = info(level);

    std::string entry;
    entry.reserve(
        time.size() + pc.size() + source.size() +
        message.size() + li.name.size() + 8);

    entry += time;
    entry += ' ';
    entry += pc;
    entry += " [";
    entry += li.name;
    entry += "] ";
    entry += source;
    entry += ": ";
    entry += message;
    entry += '\n';

    return entry;
}

// Console output line (with optional ANSI colors).
// width selects how much context is shown:
//   >= 180: time + hostname + source, >= 120: source only, below: level only.
// This mirrors the non-TTY fallback (width 180) used by run() when output
// is captured, so captured output always includes the full prefix.
inline std::string format_console_output(
    std::string_view time,
    std::string_view pc,
    Level level,
    std::string_view source,
    std::string_view message,
    bool color,
    int width) {
    const auto& li = info(level);

    std::string prefix;

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
            prefix += time;
            prefix += " ";
            prefix += pc;
            prefix += " ";
            prefix += source;
            prefix += ": ";
        }
    } else if (width >= 120) {
        if (color) {
            prefix += ANSI_BLUE;
            prefix += source;
            prefix += ':';
            prefix += ANSI_RESET;
            prefix += ' ';
        } else {
            prefix += source;
            prefix += ": ";
        }
    }

    if (color) {
        prefix += ANSI_HI_BLACK;
        prefix += '[';
        prefix += li.name;
        prefix += ']';
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += li.color;
    } else {
        prefix += '[';
        prefix += li.name;
        prefix += "] ";
    }

    std::string output;
    output.reserve(prefix.size() + message.size() + 16);
    output += prefix;
    output += message;

    if (color)
        output += ANSI_RESET;

    output += '\n';

    return output;
}

} // namespace logamuffin
