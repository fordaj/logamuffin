#pragma once

// Shared text helpers used by config, color, and JSON modules.
//
// Small, pure string utilities extracted from logamuffin.cpp so each
// module can depend on them explicitly instead of sharing an anonymous
// namespace. No I/O, no platform dependencies.

#include <array>
#include <string>
#include <string_view>

namespace logamuffin {

// Maximum absolute numeric value accepted for log level values.
// Level values come from env/JSON/config strings; larger magnitudes are
// treated as invalid to catch configuration mistakes early.
inline constexpr int kLevelValueLimit = 1000000;

// Boolean tokens for env/config values (uppercase, trimmed). Single
// source so env_enabled(), parse_bool_string(), and LOG_COLOR handling
// cannot drift apart.
inline constexpr std::array<std::string_view, 4> kTruthyTokens = {
    "1", "TRUE", "YES", "ON"};
inline constexpr std::array<std::string_view, 4> kFalsyTokens = {
    "0", "FALSE", "NO", "OFF"};

inline bool contains_token(std::string_view upperValue,
                           const std::array<std::string_view, 4>& tokens) {
    for (const auto token : tokens)
        if (upperValue == token)
            return true;
    return false;
}

inline bool is_truthy_token(std::string_view upperValue) {
    return contains_token(upperValue, kTruthyTokens);
}

inline bool is_falsy_token(std::string_view upperValue) {
    return contains_token(upperValue, kFalsyTokens);
}

std::string lowercase(std::string s);
std::string trim(const std::string& s);

// Config field / color name keys: lowercase with separators removed, so
// "log_level", "log-level", and "loglevel" are equivalent.
std::string normalize_key(const std::string& s);

// Strict decimal integer parsing: optional sign, digits only, within
// +/-kLevelValueLimit. Returns false on empty, malformed, or out-of-range
// input.
bool parse_int_strict(const std::string& raw, int& out);

bool parse_bool_string(const std::string& raw, bool& out);

// Returns 0-15 for a hex digit, -1 otherwise. Shared by color and JSON
// parsers so the digit math cannot drift.
int hex_digit_value(char c);

} // namespace logamuffin
