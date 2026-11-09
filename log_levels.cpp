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

// ---------------------------------------------------------------------------
// Level name validation (file-local helpers)
// ---------------------------------------------------------------------------

namespace {

bool is_digit_char(char c) {
    return c >= '0' && c <= '9';
}

bool is_valid_level_char(char c) {
    return (c >= 'A' && c <= 'Z') || is_digit_char(c) ||
           c == '_' || c == '-';
}

} // namespace

bool valid_level_name(const std::string& upperName) {
    if (upperName.empty() || upperName.size() > kMaxLevelNameLength)
        return false;
    for (char c : upperName) {
        if (!is_valid_level_char(c))
            return false;
    }
    return true;
}

std::string_view level_alias_target(std::string_view upperName) {
    for (const auto& aliasPair : kLevelAliases) {
        if (upperName == aliasPair.alias)
            return aliasPair.canonical;
        if (upperName == aliasPair.canonical)
            return aliasPair.alias;
    }
    return {};
}

const LogLevel* find_level_exact(const std::vector<LogLevel>& levels,
                                 const std::string& upperName) {
    for (const auto& entry : levels)
        if (entry.name == upperName)
            return &entry;
    return nullptr;
}

LogLevel* find_level_by_name_mut(std::vector<LogLevel>& levels,
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
    if (const auto alias = level_alias_target(upperName);
        !alias.empty()) {
        return find_level_exact(levels, std::string(alias));
    }
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
    // Ad-hoc levels display as "[<value>]" with default color so numeric
    // LEVELS without a configured category still render and filter.
    LogLevel entry;
    entry.name = std::to_string(value);
    entry.color = std::string(ANSI_RESET);
    entry.value = value;
    return entry;
}

// ---------------------------------------------------------------------------
// upsert_level modes: existing update (value and/or color) vs new insert.
// ---------------------------------------------------------------------------

namespace {

void apply_value_update(LogLevel* existing,
                        const std::optional<int>& value) {
    if (value.has_value())
        existing->value = *value;
}

void apply_color_update(LogLevel* existing,
                        const std::optional<std::string>& colorEscape) {
    if (colorEscape.has_value())
        existing->color = *colorEscape;
}

bool insert_new_level(std::vector<LogLevel>& levels,
                      const std::string& upperName,
                      const std::optional<int>& value,
                      const std::optional<std::string>& colorEscape,
                      std::string& err) {
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

} // namespace

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
    // Existing level: apply only the fields the caller supplied.
    if (LogLevel* existing = find_level_by_name_mut(levels, upperName)) {
        apply_value_update(existing, value);
        apply_color_update(existing, colorEscape);
        return true;
    }
    // New level: requires a numeric value; color defaults to reset.
    return insert_new_level(levels, upperName, value, colorEscape, err);
}

bool is_error_level(const LogLevel& level) {
    // Name check catches ERROR/CRITICAL/FATAL even when their numeric
    // value was remapped low; value check catches custom categories
    // at/above the error threshold with arbitrary names.
    const std::string& name = level.name;
    const bool errorName =
        (name == "ERROR" || name == "CRITICAL" || name == "FATAL");
    return errorName || level.value >= kErrorLevelValueThreshold;
}

std::string valid_levels_string(const Config& cfg) {
    std::string out;
    for (std::size_t i = 0; i < cfg.levels.size(); ++i) {
        if (i > 0)
            out += ", ";
        const std::string& name = cfg.levels[i].name;
        out += name;
        // Mention built-in aliases so usage errors list every documented
        // name (WARN/WARNING, CRITICAL/FATAL), via the shared table.
        if (const auto alias = level_alias_target(name);
            !alias.empty()) {
            out += "/";
            out += alias;
        }
    }
    return out;
}

} // namespace logamuffin
