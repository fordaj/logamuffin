#pragma once

// Log level registry: built-in defaults, lookup, and validation.
//
// The extensible log-level model (issue #4): Config holds the sorted list
// of LogLevels plus threshold/scalar settings. Config file and env
// application live in config.cpp; this module only manages the registry.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace logamuffin {

// Maximum length of a level name (e.g. "CRITICAL").
inline constexpr std::size_t kMaxLevelNameLength = 64;

// Values at or above this go to stderr (ERROR/CRITICAL and custom high
// categories). Mirrors the documented stream routing.
inline constexpr int kErrorLevelValueThreshold = 40;

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
LogLevel* find_level_mutable(std::vector<LogLevel>& levels,
                            const std::string& upperName);
const LogLevel* find_level_with_alias(
    const std::vector<LogLevel>& levels,
    const std::string& upperName);

bool upsert_level(std::vector<LogLevel>& levels,
                 const std::string& upperName,
                 std::optional<int> value,
                 std::optional<std::string> colorEscape,
                 std::string& err);

bool is_error_level(const LogLevel& level);
std::string valid_levels_string(const Config& cfg);

} // namespace logamuffin
