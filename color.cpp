#include "color.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "ansi_colors.hpp"
#include "text_utils.hpp"

namespace logamuffin {

namespace {

// Escape byte that prefixes every ANSI sequence.
constexpr char kEscapeChar = '\x1b';

// Hex truecolor helpers: #RRGGBB (kHexLongLength digits) and #RGB
// (kHexShortLength digits, expanded by kHexShortExpand per digit,
// e.g. F -> FF).
constexpr int kHexRadix = 16;
constexpr int kHexShortExpand = 17;
constexpr std::size_t kHexLongLength = 6;
constexpr std::size_t kHexShortLength = 3;

// ---------------------------------------------------------------------------
// Named color table (normalized keys from normalize_key())
// ---------------------------------------------------------------------------
// Each entry maps one accepted spelling to its ANSI escape. Grouped by
// family so a new alias is a one-line data addition. Lookup scans this
// table (linear is fine for ~111 entries and keeps group order).

struct NamedColorEntry {
    std::string_view name;
    std::string_view escape;
};

constexpr std::array<NamedColorEntry, 111> kNamedColors = {{
    // Reset / plain
    {"reset", ANSI_RESET},
    {"none", ANSI_RESET},
    {"default", ANSI_RESET},
    {"plain", ANSI_RESET},
    {"", ANSI_RESET},
    // Base colors
    {"black", ANSI_BLACK},
    {"red", ANSI_RED},
    {"green", ANSI_GREEN},
    {"yellow", ANSI_YELLOW},
    {"blue", ANSI_BLUE},
    {"purple", ANSI_PURPLE},
    {"magenta", ANSI_PURPLE},
    {"cyan", ANSI_CYAN},
    {"white", ANSI_WHITE},
    // Bold
    {"boldblack", ANSI_BOLD_BLACK},
    {"boldred", ANSI_BOLD_RED},
    {"boldgreen", ANSI_BOLD_GREEN},
    {"boldyellow", ANSI_BOLD_YELLOW},
    {"boldblue", ANSI_BOLD_BLUE},
    {"boldpurple", ANSI_BOLD_PURPLE},
    {"boldmagenta", ANSI_BOLD_PURPLE},
    {"boldcyan", ANSI_BOLD_CYAN},
    {"boldwhite", ANSI_BOLD_WHITE},
    // Underline
    {"underlineblack", ANSI_UNDERLINE_BLACK},
    {"underlinered", ANSI_UNDERLINE_RED},
    {"underlinegreen", ANSI_UNDERLINE_GREEN},
    {"underlineyellow", ANSI_UNDERLINE_YELLOW},
    {"underlineblue", ANSI_UNDERLINE_BLUE},
    {"underlinepurple", ANSI_UNDERLINE_PURPLE},
    {"underlinemagenta", ANSI_UNDERLINE_PURPLE},
    {"underlinecyan", ANSI_UNDERLINE_CYAN},
    {"underlinewhite", ANSI_UNDERLINE_WHITE},
    // Background
    {"bgblack", ANSI_BG_BLACK},
    {"backgroundblack", ANSI_BG_BLACK},
    {"bgred", ANSI_BG_RED},
    {"backgroundred", ANSI_BG_RED},
    {"bggreen", ANSI_BG_GREEN},
    {"backgroundgreen", ANSI_BG_GREEN},
    {"bgyellow", ANSI_BG_YELLOW},
    {"backgroundyellow", ANSI_BG_YELLOW},
    {"bgblue", ANSI_BG_BLUE},
    {"backgroundblue", ANSI_BG_BLUE},
    {"bgpurple", ANSI_BG_PURPLE},
    {"backgroundpurple", ANSI_BG_PURPLE},
    {"bgmagenta", ANSI_BG_PURPLE},
    {"backgroundmagenta", ANSI_BG_PURPLE},
    {"bgcyan", ANSI_BG_CYAN},
    {"backgroundcyan", ANSI_BG_CYAN},
    {"bgwhite", ANSI_BG_WHITE},
    {"backgroundwhite", ANSI_BG_WHITE},
    // High-intensity (bright) foreground
    {"hiblack", ANSI_HI_BLACK},
    {"brightblack", ANSI_HI_BLACK},
    {"gray", ANSI_HI_BLACK},
    {"grey", ANSI_HI_BLACK},
    {"darkgray", ANSI_HI_BLACK},
    {"darkgrey", ANSI_HI_BLACK},
    {"hired", ANSI_HI_RED},
    {"brightred", ANSI_HI_RED},
    {"higreen", ANSI_HI_GREEN},
    {"brightgreen", ANSI_HI_GREEN},
    {"hiyellow", ANSI_HI_YELLOW},
    {"brightyellow", ANSI_HI_YELLOW},
    {"hiblue", ANSI_HI_BLUE},
    {"brightblue", ANSI_HI_BLUE},
    {"hipurple", ANSI_HI_PURPLE},
    {"brightpurple", ANSI_HI_PURPLE},
    {"himagenta", ANSI_HI_PURPLE},
    {"brightmagenta", ANSI_HI_PURPLE},
    {"hicyan", ANSI_HI_CYAN},
    {"brightcyan", ANSI_HI_CYAN},
    {"hiwhite", ANSI_HI_WHITE},
    {"brightwhite", ANSI_HI_WHITE},
    {"lightgray", ANSI_HI_WHITE},
    {"lightgrey", ANSI_HI_WHITE},
    {"silver", ANSI_HI_WHITE},
    // Bold high-intensity
    {"boldhiblack", ANSI_BOLD_HI_BLACK},
    {"boldbrightblack", ANSI_BOLD_HI_BLACK},
    {"boldhired", ANSI_BOLD_HI_RED},
    {"boldbrightred", ANSI_BOLD_HI_RED},
    {"boldhigreen", ANSI_BOLD_HI_GREEN},
    {"boldbrightgreen", ANSI_BOLD_HI_GREEN},
    {"boldhiyellow", ANSI_BOLD_HI_YELLOW},
    {"boldbrightyellow", ANSI_BOLD_HI_YELLOW},
    {"boldhiblue", ANSI_BOLD_HI_BLUE},
    {"boldbrightblue", ANSI_BOLD_HI_BLUE},
    {"boldhipurple", ANSI_BOLD_HI_PURPLE},
    {"boldbrightpurple", ANSI_BOLD_HI_PURPLE},
    {"boldhimagenta", ANSI_BOLD_HI_PURPLE},
    {"boldbrightmagenta", ANSI_BOLD_HI_PURPLE},
    {"boldhicyan", ANSI_BOLD_HI_CYAN},
    {"boldbrightcyan", ANSI_BOLD_HI_CYAN},
    {"boldhiwhite", ANSI_BOLD_HI_WHITE},
    {"boldbrightwhite", ANSI_BOLD_HI_WHITE},
    // High-intensity backgrounds
    {"bghiblack", ANSI_BG_HI_BLACK},
    {"backgroundhiblack", ANSI_BG_HI_BLACK},
    {"bghired", ANSI_BG_HI_RED},
    {"backgroundhired", ANSI_BG_HI_RED},
    {"bghigreen", ANSI_BG_HI_GREEN},
    {"backgroundhigreen", ANSI_BG_HI_GREEN},
    {"bghiyellow", ANSI_BG_HI_YELLOW},
    {"backgroundhiyellow", ANSI_BG_HI_YELLOW},
    {"bghiblue", ANSI_BG_HI_BLUE},
    {"backgroundhiblue", ANSI_BG_HI_BLUE},
    {"bghipurple", ANSI_BG_HI_PURPLE},
    {"backgroundhipurple", ANSI_BG_HI_PURPLE},
    {"bghimagenta", ANSI_BG_HI_PURPLE},
    {"backgroundhimagenta", ANSI_BG_HI_PURPLE},
    {"bghicyan", ANSI_BG_HI_CYAN},
    {"backgroundhicyan", ANSI_BG_HI_CYAN},
    {"bghiwhite", ANSI_BG_HI_WHITE},
    {"backgroundhiwhite", ANSI_BG_HI_WHITE},
}};

bool color_name_to_escape(const std::string& normalized,
                          std::string& outEscape) {
    for (const auto& entry : kNamedColors) {
        if (normalized == entry.name) {
            outEscape = std::string(entry.escape);
            return true;
        }
    }
    return false;
}

// Expands literal escape text ("\033", "\e", "\x1b") to real ESC bytes
// so specs typed in shell/config files without a raw ESC still work.
std::string expand_literal_escape_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            // "\033" (4 chars).
            if (text[i + 1] == '0' && i + 3 < text.size() &&
                text[i + 2] == '3' && text[i + 3] == '3') {
                out += kEscapeChar;
                i += 4;
                continue;
            }
            // "\e" (2 chars).
            if (text[i + 1] == 'e') {
                out += kEscapeChar;
                i += 2;
                continue;
            }
            // "\x1b" / "\X1B" (4 chars).
            if ((text[i + 1] == 'x' || text[i + 1] == 'X') &&
                i + 3 < text.size() &&
                text[i + 2] == '1' &&
                (text[i + 3] == 'b' || text[i + 3] == 'B')) {
                out += kEscapeChar;
                i += 4;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// True when text looks like SGR parameters (digits, ';' or ',',
// with at least one digit): "31", "0;31", "1;91".
bool is_sgr_code_text(const std::string& text) {
    if (text.empty())
        return false;
    bool hasDigit = false;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)))
            hasDigit = true;
        else if (c != ';' && c != ',')
            return false;
    }
    return hasDigit;
}

// True when text ends like an SGR body: contains '[' and ends with 'm'.
bool ends_with_sgr_terminator(const std::string& text) {
    return !text.empty() && text.back() == 'm' &&
           text.find('[') != std::string::npos;
}

// Combines two hex digits into a byte (0-255). Caller validated digits.
int parse_hex_pair(char high, char low) {
    return hex_digit_value(high) * kHexRadix + hex_digit_value(low);
}

bool digits_valid(const std::string& digits) {
    for (char c : digits)
        if (hex_digit_value(c) < 0)
            return false;
    return true;
}

} // namespace

bool resolve_color(const std::string& specRaw, std::string& outEscape,
                   std::string& err) {
    const std::string spec = trim(specRaw);
    if (spec.empty()) {
        err = "empty color";
        return false;
    }
    // Precedence: esc -> literal esc -> bracket -> hex -> bare SGR -> named.
    // Full escape sequence containing a real ESC byte (e.g. "\033[0;31m").
    if (spec.find(kEscapeChar) != std::string::npos) {
        if (ends_with_sgr_terminator(spec)) {
            outEscape = spec;
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Literal "\033" / "\e" / "\x1b" text (shell-typed, no raw ESC).
    if (spec.find('\\') != std::string::npos) {
        const std::string expanded = expand_literal_escape_text(spec);
        if (expanded.find(kEscapeChar) != std::string::npos &&
            ends_with_sgr_terminator(expanded)) {
            outEscape = expanded;
            return true;
        }
        // A backslash that expands to no ESC is invalid, not a name.
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // "[0;31m" bracket form without ESC.
    if (!spec.empty() && spec.front() == '[' && spec.back() == 'm') {
        const std::string inner = spec.substr(1, spec.size() - 2);
        if (is_sgr_code_text(inner)) {
            outEscape = std::string(1, kEscapeChar) + spec;
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Hex truecolor: #RRGGBB or #RGB.
    if (!spec.empty() && spec.front() == '#') {
        const std::string hexDigits = spec.substr(1);
        if (hexDigits.size() == kHexLongLength && digits_valid(hexDigits)) {
            const int red = parse_hex_pair(hexDigits[0], hexDigits[1]);
            const int green = parse_hex_pair(hexDigits[2], hexDigits[3]);
            const int blue = parse_hex_pair(hexDigits[4], hexDigits[5]);
            outEscape = "\x1b[38;2;" + std::to_string(red) + ";" +
                        std::to_string(green) + ";" + std::to_string(blue) + "m";
            return true;
        }
        if (hexDigits.size() == kHexShortLength && digits_valid(hexDigits)) {
            const int red = hex_digit_value(hexDigits[0]) * kHexShortExpand;
            const int green = hex_digit_value(hexDigits[1]) * kHexShortExpand;
            const int blue = hex_digit_value(hexDigits[2]) * kHexShortExpand;
            outEscape = "\x1b[38;2;" + std::to_string(red) + ";" +
                        std::to_string(green) + ";" + std::to_string(blue) + "m";
            return true;
        }
        err = "invalid color '" + specRaw + "'";
        return false;
    }
    // Bare SGR code: "31", "0;31", "1;91" (',' accepted as ';').
    if (is_sgr_code_text(spec)) {
        std::string code = spec;
        std::replace(code.begin(), code.end(), ',', ';');
        outEscape = "\x1b[" + code + "m";
        return true;
    }
    // Named color (case-insensitive, separators ignored via normalize_key()).
    if (color_name_to_escape(normalize_key(spec), outEscape))
        return true;
    err = "unknown color '" + specRaw + "'";
    return false;
}

} // namespace logamuffin
