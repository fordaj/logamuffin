#pragma once

// Shared text helpers used by config, color, and JSON modules.
//
// Small, pure string utilities extracted from logamuffin.cpp so each
// module can depend on them explicitly instead of sharing an anonymous
// namespace. No I/O, no platform dependencies.

#include <string>

namespace logamuffin {

// Maximum absolute numeric value accepted for log level values.
// Level values come from env/JSON/config strings; larger magnitudes are
// treated as invalid to catch configuration mistakes early.
inline constexpr int kLevelValueLimit = 1000000;

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

} // namespace logamuffin
