#include "color.hpp"

#include <algorithm>
#include <cctype>

#include "ansi_colors.hpp"
#include "text_utils.hpp"

namespace logamuffin {

namespace {

// ---------------------------------------------------------------------------
// Named color table
// ---------------------------------------------------------------------------
// Uses normalized keys from normalize_key() (lowercase, no separators).

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

} // namespace

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

} // namespace logamuffin
