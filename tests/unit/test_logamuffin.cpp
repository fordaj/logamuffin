// C++ unit tests for logamuffin's testable core (logamuffin.hpp).
//
// Framework: doctest (vendored at tools/doctest/v2.5.3/doctest.h).
// Build + run via ./run_tests.sh (see docs/TESTING.md).
//
// These tests cover observable pure logic only (parsing, filtering,
// configuration, formatting). Process-level behavior (CLI exit codes,
// stdout/stderr routing, file I/O, concurrency) is covered by the
// Python integration suite in tests/integration/test_cli.py.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "logamuffin.hpp"
#include "version.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace logamuffin;

namespace {

// Shared fixtures for formatting tests (independent literals, not
// production constants, so tests verify actual documented shape).
constexpr const char* kExampleTimestamp = "2026-11-08 14:59:00,123";
constexpr const char* kExampleHost = "testpc";
constexpr const char* kExampleSource = "app.sh";
constexpr const char* kExampleMessage = "hello";

// Tiny assertion helper: true when needle appears in haystack.
inline bool contains_text(const std::string& haystack,
                          std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// Env fixture: RAII save/restore for one environment variable.
// Construct at the top of an env test (or SUBCASE) so each case starts
// clean and the host environment is restored on scope exit.
struct EnvGuard {
    const char* name;
    std::string saved;
    bool was_set = false;
    bool active = true;

    EnvGuard(const char* n) : name(n) {
        if (const char* v = std::getenv(name)) {
            saved = v;
            was_set = true;
        }
    }
    ~EnvGuard() {
        if (!active) return;
        if (was_set)
            setenv(name, saved.c_str(), 1);
        else
            unsetenv(name);
    }
    void set(const char* v) { setenv(name, v, 1); }
    void unset() { unsetenv(name); }
};

struct LevelParseCase {
    std::string_view input;
    Level expected;
};

} // namespace

// ---------------------------------------------------------------------------
// String helpers

// ---------------------------------------------------------------------------

TEST_CASE("uppercase converts ASCII to upper case") {
    CHECK(uppercase("hello") == "HELLO");
    CHECK(uppercase("INFO") == "INFO");
    CHECK(uppercase("wArNiNg") == "WARNING");
    CHECK(uppercase("") == "");
    CHECK(uppercase("log_level 123!") == "LOG_LEVEL 123!");
    CHECK(uppercase("trace") == "TRACE");
}

TEST_CASE("basename extracts final path component") {
    SUBCASE("POSIX paths") {
        CHECK(basename("/usr/local/bin/logamuffin") == "logamuffin");
        CHECK(basename("a/b/c") == "c");
        CHECK(basename("/a/b/script.sh") == "script.sh");
    }
    SUBCASE("Windows paths") {
        CHECK(basename("C:\\tools\\logamuffin.exe") == "logamuffin.exe");
        CHECK(basename("a\\b\\c") == "c");
    }
    SUBCASE("mixed separators") {
        CHECK(basename("a/b\\c/d") == "d");
        CHECK(basename("a\\b/c\\d") == "d");
    }
    SUBCASE("bare filenames and edge cases") {
        CHECK(basename("logamuffin") == "logamuffin");
        CHECK(basename("") == "");
        CHECK(basename("file.txt") == "file.txt");
        // Trailing separator yields empty final component, matching
        // the original implementation's find_last_of behavior.
        CHECK(basename("/a/b/") == "");
    }
}

// ---------------------------------------------------------------------------
// Parse

// ---------------------------------------------------------------------------

TEST_CASE("parse_level accepts canonical names and aliases case-insensitively") {
    Level out{};

    // Canonical names + documented aliases (table-driven).
    const LevelParseCase canonicalCases[] = {
        {"TRACE", Level::Trace},
        {"DEBUG", Level::Debug},
        {"INFO", Level::Info},
        {"WARNING", Level::Warning},
        {"WARN", Level::Warning},
        {"ERROR", Level::Error},
        {"CRITICAL", Level::Critical},
        {"FATAL", Level::Critical},
        {"ALWAYS", Level::Always},
    };
    for (const auto& testCase : canonicalCases) {
        out = Level::Info; // sentinel before each lookup
        CHECK_MESSAGE(parse_level(std::string(testCase.input), out),
                      "input=", testCase.input);
        CHECK_MESSAGE(out == testCase.expected,
                      "input=", testCase.input);
    }

    SUBCASE("case-insensitive") {
        const LevelParseCase insensitiveCases[] = {
            {"trace", Level::Trace},
            {"Info", Level::Info},
            {"wArNiNg", Level::Warning},
            {"warn", Level::Warning},
            {"critical", Level::Critical},
            {"fatal", Level::Critical},
            {"always", Level::Always},
        };
        for (const auto& testCase : insensitiveCases) {
            out = Level::Info;
            CHECK_MESSAGE(parse_level(std::string(testCase.input), out),
                          "input=", testCase.input);
            CHECK_MESSAGE(out == testCase.expected,
                          "input=", testCase.input);
        }
    }

    SUBCASE("numeric built-in values") {
        const LevelParseCase numericCases[] = {
            {"-1", Level::Always},
            {"5", Level::Trace},
            {"10", Level::Debug},
            {"20", Level::Info},
            {"30", Level::Warning},
            {"40", Level::Error},
            {"50", Level::Critical},
        };
        for (const auto& testCase : numericCases) {
            out = Level::Info;
            CHECK_MESSAGE(parse_level(std::string(testCase.input), out),
                          "input=", testCase.input);
            CHECK_MESSAGE(out == testCase.expected,
                          "input=", testCase.input);
        }
    }
}

TEST_CASE("parse_level rejects invalid values") {
    Level out = Level::Info; // sentinel: must remain untouched on failure
    const char* const invalid[] = {"", "BOGUS", "123", "WARNI",
                                   "INFORMATION", " INFO", "INFO "};
    for (const char* input : invalid) {
        out = Level::Info;
        CHECK_MESSAGE(!parse_level(input, out), "input=", input);
        CHECK_MESSAGE(out == Level::Info, "input=", input);
    }
}

TEST_CASE("level numeric values match documented ordering") {
    CHECK(static_cast<int>(Level::Always) == -1);
    CHECK(static_cast<int>(Level::Trace) == 5);
    CHECK(static_cast<int>(Level::Debug) == 10);
    CHECK(static_cast<int>(Level::Info) == 20);
    CHECK(static_cast<int>(Level::Warning) == 30);
    CHECK(static_cast<int>(Level::Error) == 40);
    CHECK(static_cast<int>(Level::Critical) == 50);
}

TEST_CASE("info returns display name and color for each level") {
    CHECK(info(Level::Trace).name == "TRACE");
    CHECK(info(Level::Debug).name == "DEBUG");
    CHECK(info(Level::Info).name == "INFO");
    CHECK(info(Level::Warning).name == "WARN");
    CHECK(info(Level::Error).name == "ERROR");
    CHECK(info(Level::Critical).name == "CRITICAL");
    CHECK(info(Level::Always).name == "ALWAYS");

    CHECK(info(Level::Error).color == ANSI_RED);
    CHECK(info(Level::Warning).color == ANSI_YELLOW);
    CHECK(info(Level::Critical).color == ANSI_PURPLE);
    CHECK(info(Level::Trace).color == ANSI_CYAN);
    CHECK(info(Level::Debug).color == ANSI_GREEN);
}

TEST_CASE("info falls back to INFO for unknown level") {
    const auto& fallback = info(static_cast<Level>(999));
    CHECK(fallback.name == "INFO");
    CHECK(fallback.level == Level::Info);
}

TEST_CASE("clean_message strips carriage returns but keeps line breaks") {
    CHECK(clean_message("hello") == "hello");
    CHECK(clean_message("a\r\nb") == "a\nb");
    CHECK(clean_message("a\rb") == "ab");
    CHECK(clean_message("a\nb\nc") == "a\nb\nc");
    CHECK(clean_message("") == "");
    CHECK(clean_message("\r\r") == "");
    CHECK(clean_message("line1\r\nline2\r\n") == "line1\nline2\n");
}

// ---------------------------------------------------------------------------
// Env + filtering

// ---------------------------------------------------------------------------

TEST_CASE("env_enabled interprets truthy values case-insensitively") {
    EnvGuard guard("LOG_COLOR_TEST");
    guard.unset();

    SUBCASE("unset returns fallback") {
        CHECK(env_enabled("LOG_COLOR_TEST", true) == true);
        CHECK(env_enabled("LOG_COLOR_TEST", false) == false);
    }
    SUBCASE("truthy values") {
        for (const char* v : {"1", "TRUE", "true", "True", "YES", "yes", "ON", "on"}) {
            guard.set(v);
            CHECK_MESSAGE(env_enabled("LOG_COLOR_TEST", false) == true, "value=", v);
        }
    }
    SUBCASE("falsy and unknown values") {
        for (const char* v : {"0", "FALSE", "false", "NO", "no", "OFF", "off", "", "bogus", "2"}) {
            guard.set(v);
            CHECK_MESSAGE(env_enabled("LOG_COLOR_TEST", false) == false, "value=", v);
            // Fallback only applies when the variable is unset, not
            // when it is set to a falsy/unknown value.
            CHECK_FALSE(env_enabled("LOG_COLOR_TEST", true));
        }
    }
}

TEST_CASE("get_source prefers LOG_SOURCE then falls back to argv0") {
    EnvGuard guard("LOG_SOURCE");

    SUBCASE("LOG_SOURCE path is reduced to basename") {
        guard.set("/some/dir/custom.sh");
        CHECK(get_source("./logamuffin") == "custom.sh");
        guard.set("win\\path\\script.ps1");
        CHECK(get_source("./logamuffin") == "script.ps1");
    }
    SUBCASE("unset LOG_SOURCE uses argv0 basename") {
        guard.unset();
        CHECK(get_source("/usr/bin/logamuffin") == "logamuffin");
        CHECK(get_source("logamuffin") == "logamuffin");
    }
    SUBCASE("empty LOG_SOURCE behaves as unset") {
        guard.set("");
        CHECK(get_source("/usr/bin/logamuffin") == "logamuffin");
    }
    SUBCASE("null argv0 without LOG_SOURCE yields default name") {
        guard.unset();
        CHECK(get_source(nullptr) == "logamuffin");
    }
}

TEST_CASE("should_emit implements severity filtering") {
    SUBCASE("at INFO threshold") {
        CHECK_FALSE(should_emit(Level::Trace, Level::Info));
        CHECK_FALSE(should_emit(Level::Debug, Level::Info));
        CHECK(should_emit(Level::Info, Level::Info));
        CHECK(should_emit(Level::Warning, Level::Info));
        CHECK(should_emit(Level::Error, Level::Info));
        CHECK(should_emit(Level::Critical, Level::Info));
        CHECK(should_emit(Level::Always, Level::Info));
    }
    SUBCASE("at TRACE threshold everything emits") {
        CHECK(should_emit(Level::Trace, Level::Trace));
        CHECK(should_emit(Level::Debug, Level::Trace));
        CHECK(should_emit(Level::Info, Level::Trace));
        CHECK(should_emit(Level::Always, Level::Trace));
    }
    SUBCASE("at CRITICAL threshold only top level and ALWAYS emit") {
        CHECK_FALSE(should_emit(Level::Trace, Level::Critical));
        CHECK_FALSE(should_emit(Level::Debug, Level::Critical));
        CHECK_FALSE(should_emit(Level::Info, Level::Critical));
        CHECK_FALSE(should_emit(Level::Warning, Level::Critical));
        CHECK_FALSE(should_emit(Level::Error, Level::Critical));
        CHECK(should_emit(Level::Critical, Level::Critical));
        CHECK(should_emit(Level::Always, Level::Critical));
    }
    SUBCASE("equal threshold emits and below does not") {
        CHECK(should_emit(Level::Warning, Level::Warning));
        CHECK_FALSE(should_emit(Level::Info, Level::Warning));
    }
}

// ---------------------------------------------------------------------------
// Format

// ---------------------------------------------------------------------------

TEST_CASE("join_message joins parts with single spaces") {
    CHECK(join_message(std::vector<std::string>{}) == "");
    CHECK(join_message(std::vector<std::string>{"hello"}) == "hello");
    CHECK(join_message(std::vector<std::string>{"hello", "world", "foo"}) == "hello world foo");
    // Embedded spaces inside a single part are preserved; the joiner
    // inserts exactly one separator between parts.
    CHECK(join_message(std::vector<std::string>{"a b", "c"}) == "a b c");
}

TEST_CASE("join_message from argv strips carriage returns") {
    char* argv[] = {const_cast<char*>("logamuffin"), const_cast<char*>("INFO"),
                    const_cast<char*>("hello"), const_cast<char*>("wor\rld")};
    CHECK(join_message(4, argv, 2) == "hello world");

    char* single[] = {const_cast<char*>("logamuffin"), const_cast<char*>("INFO"),
                      const_cast<char*>("solo")};
    CHECK(join_message(3, single, 2) == "solo");
}

TEST_CASE("format_file_entry produces plain-text log lines") {
    const std::string entry = format_file_entry(
        kExampleTimestamp, kExampleHost, Level::Info,
        kExampleSource, kExampleMessage);

    CHECK(entry ==
          "2026-11-08 14:59:00,123 testpc [INFO] app.sh: hello\n");
    CHECK(entry.find('\033') == std::string::npos);
    CHECK(entry.back() == '\n');

    // Canonical labels: Warning displays as WARN, FATAL maps to CRITICAL.
    CHECK(contains_text(format_file_entry("T", "PC", Level::Warning, "S", "M"),
                        "[WARN]"));
    CHECK(contains_text(format_file_entry("T", "PC", Level::Critical, "S", "M"),
                        "[CRITICAL]"));

    // Multiline messages are preserved verbatim (minus \r via clean_message upstream).
    const std::string multi = format_file_entry("T", "PC", Level::Error, "S", "first\nsecond");
    CHECK(contains_text(multi, "first\nsecond"));
    CHECK(multi.find('\033') == std::string::npos);
}

TEST_CASE("format_console_output plain branches depend on width") {
    const std::string full = format_console_output(
        kExampleTimestamp, kExampleHost, Level::Info,
        kExampleSource, kExampleMessage, false, 180);
    CHECK(contains_text(full, kExampleTimestamp));
    CHECK(contains_text(full, kExampleHost));
    CHECK(contains_text(full, kExampleSource));
    CHECK(contains_text(full, "[INFO]"));
    CHECK(contains_text(full, kExampleMessage));
    CHECK(full.find('\033') == std::string::npos);
    CHECK(full.back() == '\n');

    const std::string medium = format_console_output(
        kExampleTimestamp, kExampleHost, Level::Info,
        kExampleSource, kExampleMessage, false, 120);
    CHECK(contains_text(medium, kExampleSource));
    CHECK(contains_text(medium, "[INFO]"));
    CHECK(!contains_text(medium, "2026-11-08"));
    CHECK(!contains_text(medium, kExampleHost));

    const std::string narrow = format_console_output(
        kExampleTimestamp, kExampleHost, Level::Info,
        kExampleSource, kExampleMessage, false, 80);
    CHECK(contains_text(narrow, "[INFO]"));
    CHECK(contains_text(narrow, kExampleMessage));
    CHECK(!contains_text(narrow, kExampleSource));
    CHECK(!contains_text(narrow, "2026-11-08"));
}

TEST_CASE("format_console_output width thresholds are inclusive") {
    // 179 -> medium, 180 -> full, 119 -> narrow, 120 -> medium.
    const std::string at179 = format_console_output("T180", "PC180", Level::Info, "S180", "m", false, 179);
    CHECK(!contains_text(at179, "T180"));
    CHECK(contains_text(at179, "S180"));

    const std::string at180 = format_console_output("T180", "PC180", Level::Info, "S180", "m", false, 180);
    CHECK(contains_text(at180, "T180"));

    const std::string at119 = format_console_output("T119", "PC119", Level::Info, "S119", "m", false, 119);
    CHECK(!contains_text(at119, "S119"));

    const std::string at120 = format_console_output("T120", "PC120", Level::Info, "S120", "m", false, 120);
    CHECK(contains_text(at120, "S120"));
}

TEST_CASE("format_console_output colored output contains ANSI codes") {
    const std::string full = format_console_output(
        kExampleTimestamp, kExampleHost, Level::Warning,
        kExampleSource, kExampleMessage, true, 180);
    CHECK(full.find('\033') != std::string::npos);
    CHECK(contains_text(full, "[WARN]"));
    CHECK(contains_text(full, kExampleMessage));
    CHECK(contains_text(full, std::string(ANSI_RESET)));
    CHECK(full.back() == '\n');

    const std::string narrow = format_console_output("T", "PC", Level::Error, "S", "msg", true, 80);
    CHECK(narrow.find('\033') != std::string::npos);
    CHECK(contains_text(narrow, "[ERROR]"));
    CHECK(contains_text(narrow, "msg"));
}

// ---------------------------------------------------------------------------
// Time

// ---------------------------------------------------------------------------

TEST_CASE("timestamp uses YYYY-MM-DD HH:MM:SS,mmm format") {
    const std::string ts = timestamp();
    // Shape: "YYYY-MM-DD HH:MM:SS,mmm" (23 chars). Parse with sscanf
    // so failures name the field instead of a digit index.
    REQUIRE(ts.size() == 23);
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0, millis = 0;
    const int matched = std::sscanf(
        ts.c_str(), "%4d-%2d-%2d %2d:%2d:%2d,%3d",
        &year, &month, &day, &hour, &minute, &second, &millis);
    CHECK_MESSAGE(matched == 7, "timestamp=", ts);
    CHECK_MESSAGE(year >= 1970, "year=", year);
    CHECK_MESSAGE(year <= 9999, "year=", year);
    CHECK_MESSAGE(month >= 1, "month=", month);
    CHECK_MESSAGE(month <= 12, "month=", month);
    CHECK_MESSAGE(day >= 1, "day=", day);
    CHECK_MESSAGE(day <= 31, "day=", day);
    CHECK_MESSAGE(hour >= 0, "hour=", hour);
    CHECK_MESSAGE(hour <= 23, "hour=", hour);
    CHECK_MESSAGE(minute >= 0, "minute=", minute);
    CHECK_MESSAGE(minute <= 59, "minute=", minute);
    CHECK_MESSAGE(second >= 0, "second=", second);
    CHECK_MESSAGE(second <= 60, "second=", second);
    CHECK_MESSAGE(millis >= 0, "millis=", millis);
    CHECK_MESSAGE(millis <= 999, "millis=", millis);
    CHECK(ts[4] == '-');
    CHECK(ts[7] == '-');
    CHECK(ts[10] == ' ');
    CHECK(ts[13] == ':');
    CHECK(ts[16] == ':');
    CHECK(ts[19] == ',');
}

TEST_CASE("ANSI constants and version macro are defined") {
    CHECK_FALSE(ANSI_RESET.empty());
    CHECK(ANSI_RESET[0] == '\033');
    CHECK_FALSE(std::string(ANSI_RED).empty());
    CHECK(std::string(LOGAMUFFIN_VERSION).find('.') != std::string::npos);
    CHECK_FALSE(std::string(LOGAMUFFIN_VERSION).empty());
}
