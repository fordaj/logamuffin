#include "text_utils.hpp"

#include <cctype>

#include "logamuffin.hpp"

namespace logamuffin {

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
        if (value < -kLevelValueLimit || value > kLevelValueLimit)
            return false;
        out = static_cast<int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

int hex_digit_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

bool parse_bool_string(const std::string& raw, bool& out) {
    const std::string v = uppercase(trim(raw));
    if (is_truthy_token(v)) {
        out = true;
        return true;
    }
    if (is_falsy_token(v)) {
        out = false;
        return true;
    }
    return false;
}

} // namespace logamuffin
