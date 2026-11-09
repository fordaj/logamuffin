#pragma once

// Logamuffin core helpers (declarations).
//
// Pure, independently testable logic shared by:
//   - the executable's run() in logamuffin.cpp and
//   - the C++ unit tests in tests/unit/test_logamuffin.cpp
//
// Implementations live in logamuffin_core.cpp; this header holds only
// declarations, the Level model, and shared constants (widths, reserves)
// so both translation units see one interface without duplicating bodies.
// No platform-specific filesystem or terminal I/O here (hostname, TTY
// detection, file locking live in platform.cpp).

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "ansi_colors.hpp"
#include "log_levels.hpp"

namespace logamuffin {

// Terminal-width thresholds for console prefix detail. Single source of
// truth: logamuffin.cpp (run) and format_console_output() both consult
// these, so captured (non-TTY) output always uses kFullFormatWidth.
inline constexpr int kFullFormatWidth = 180;
inline constexpr int kMediumFormatWidth = 120;

// Trailing reserve sizes for formatted output buffers.
inline constexpr std::size_t kConsoleOutputReserve = 16;
inline constexpr std::size_t kFileEntryReserve = 8;

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

// Built-in severity table (canonical names/colors). Kept as data, not
// logic, so adding a level is a one-line data change.
inline constexpr std::array<LevelInfo, 7> kBuiltinLevels{{
    {"ALWAYS",   ANSI_PURPLE, Level::Always},
    {"TRACE",    ANSI_CYAN,   Level::Trace},
    {"DEBUG",    ANSI_GREEN,   Level::Debug},
    {"INFO",     ANSI_RESET,    Level::Info},
    {"WARN",     ANSI_YELLOW,   Level::Warning},
    {"ERROR",    ANSI_RED,   Level::Error},
    {"CRITICAL", ANSI_PURPLE,   Level::Critical}
}};

std::string uppercase(std::string s);
std::string basename(std::string path);
bool env_enabled(const char* name, bool fallback);
bool parse_level(std::string value, Level& out);
const LevelInfo& info(Level level);
std::string timestamp();
std::string clean_message(std::string message);
std::string get_source(const char* argv0);

// Filtering predicate used by run(): ALWAYS bypasses the threshold,
// everything else is emitted at-or-above the threshold.
bool should_emit(Level level, Level threshold);

std::string join_message(const std::vector<std::string>& parts);
std::string join_message(int argc, char* argv[], int start = 2);

// Plain-text file entry, independent of console colors.
// Format: "<time> <host> [<LEVEL>] <source>: <message>\n"
// Canonical LogLevel form used by run() with Config levels (custom
// categories included). The Level-enum form delegates here.
std::string format_file_entry(
    std::string_view time,
    std::string_view host,
    const LogLevel& level,
    std::string_view source,
    std::string_view message);
std::string format_file_entry(
    std::string_view time,
    std::string_view host,
    Level level,
    std::string_view source,
    std::string_view message);

// Console output line (with optional ANSI colors).
// width selects how much context is shown:
//   >= kFullFormatWidth: time + hostname + source,
//   >= kMediumFormatWidth: source only,
//   below: level only.
// This mirrors the non-TTY fallback (kFullFormatWidth) used by run()
// when output is captured, so captured output always includes the
// full prefix. Canonical LogLevel form; Level-enum form delegates here.
std::string format_console_output(
    std::string_view time,
    std::string_view host,
    const LogLevel& level,
    std::string_view source,
    std::string_view message,
    bool color,
    int width);
std::string format_console_output(
    std::string_view time,
    std::string_view host,
    Level level,
    std::string_view source,
    std::string_view message,
    bool color,
    int width);

} // namespace logamuffin
