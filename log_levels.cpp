#include "log_levels.hpp"

#include <algorithm>

#include "ansi_colors.hpp"

namespace logamuffin {

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
    if (upperName.empty() || upperName.size() > kMaxLevelNameLength)
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

const LogLevel* find_level_by_value(const std::vector<LogLevel>& levels,
                                    int value) {
    // Levels are sorted by (value, name), so the first exact value match is
    // the canonical category for that number when duplicates exist.
    for (const auto& entry : levels) {
        if (entry.value == value) {
            return &entry;
        }
    }
    return nullptr;
}

LogLevel ad_hoc_numeric_level(int value) {
    LogLevel entry;
    entry.name = std::to_string(value);
    entry.color = std::string(ANSI_RESET);
    entry.value = value;
    return entry;
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
    return errorName || level.value >= kErrorLevelValueThreshold;
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

} // namespace logamuffin
