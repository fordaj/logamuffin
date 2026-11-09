#pragma once

// Log level registry: built-in defaults, lookup, and validation.
//
// The extensible log-level model (issue #4): Config holds the sorted list
// of LogLevels plus threshold/scalar settings. Config file and env
// application live in config.cpp; this module only manages the registry.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace logamuffin {

// Maximum length of a level name (e.g. "CRITICAL").
inline constexpr std::size_t kMaxLevelNameLength = 64;

// Values at or above this go to stderr (ERROR/CRITICAL and custom high
// categories). Mirrors the documented stream routing.
inline constexpr int kErrorLevelValueThreshold = 40;

// Canonical severity aliases (bidirectional, single source of truth).
// "WARNING" aliases "WARN"; "FATAL" aliases "CRITICAL". Lookup,
// valid-levels listing, and parse_level() all consult this table.
struct LevelAlias {
    std::string_view alias;
    std::string_view canonical;
};

inline constexpr std::array<LevelAlias, 2> kLevelAliases = {{
    {"WARNING", "WARN"},
    {"FATAL", "CRITICAL"},
}};

// Returns the alias partner for upperName (canonical if upperName is an
// alias, alias if upperName is canonical, empty otherwise).
std::string_view level_alias_target(std::string_view upperName);

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

std::vector<LogLevel> default_levels();
void sort_levels(std::vector<LogLevel>& levels);

bool valid_level_name(const std::string& upperName);
const LogLevel* find_level_exact(const std::vector<LogLevel>& levels,
                                const std::string& upperName);
LogLevel* find_level_by_name_mut(std::vector<LogLevel>& levels,
                                 const std::string& upperName);
const LogLevel* find_level_with_alias(
    const std::vector<LogLevel>& levels,
    const std::string& upperName);
const LogLevel* find_level_by_value(const std::vector<LogLevel>& levels,
                                    int value);
LogLevel ad_hoc_numeric_level(int value);

bool upsert_level(std::vector<LogLevel>& levels,
                 const std::string& upperName,
                 std::optional<int> value,
                 std::optional<std::string> colorEscape,
                 std::string& err);

bool is_error_level(const LogLevel& level);
std::string valid_levels_string(const Config& cfg);

} // namespace logamuffin
