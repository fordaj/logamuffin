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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace logamuffin;

namespace {

// RAII helper: save/restore an environment variable around a test.
// Use with SUBCASE so each case starts clean.
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

} // namespace

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

TEST_CASE("parse_level accepts canonical names and aliases case-insensitively") {
    Level out{};

    CHECK(parse_level("TRACE", out));
    CHECK(out == Level::Trace);
    CHECK(parse_level("DEBUG", out));
    CHECK(out == Level::Debug);
    CHECK(parse_level("INFO", out));
    CHECK(out == Level::Info);
    CHECK(parse_level("WARNING", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("WARN", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("ERROR", out));
    CHECK(out == Level::Error);
    CHECK(parse_level("CRITICAL", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("FATAL", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("ALWAYS", out));
    CHECK(out == Level::Always);

    SUBCASE("case-insensitive") {
        CHECK(parse_level("trace", out));
        CHECK(out == Level::Trace);
        CHECK(parse_level("Info", out));
        CHECK(out == Level::Info);
        CHECK(parse_level("wArNiNg", out));
        CHECK(out == Level::Warning);
        CHECK(parse_level("warn", out));
        CHECK(out == Level::Warning);
        CHECK(parse_level("critical", out));
        CHECK(out == Level::Critical);
        CHECK(parse_level("fatal", out));
        CHECK(out == Level::Critical);
        CHECK(parse_level("always", out));
        CHECK(out == Level::Always);
    }

    SUBCASE("numeric built-in values") {
        CHECK(parse_level("-1", out));
        CHECK(out == Level::Always);
        CHECK(parse_level("5", out));
        CHECK(out == Level::Trace);
        CHECK(parse_level("10", out));
        CHECK(out == Level::Debug);
        CHECK(parse_level("20", out));
        CHECK(out == Level::Info);
        CHECK(parse_level("30", out));
        CHECK(out == Level::Warning);
        CHECK(parse_level("40", out));
        CHECK(out == Level::Error);
        CHECK(parse_level("50", out));
        CHECK(out == Level::Critical);
    }
}

TEST_CASE("parse_level rejects invalid values") {
    Level out = Level::Info; // sentinel: must remain untouched on failure
    CHECK_FALSE(parse_level("", out));
    CHECK(out == Level::Info);
    CHECK_FALSE(parse_level("BOGUS", out));
    CHECK_FALSE(parse_level("123", out));
    CHECK_FALSE(parse_level("WARNI", out));
    CHECK_FALSE(parse_level("INFORMATION", out));
    CHECK_FALSE(parse_level(" INFO", out));
    CHECK_FALSE(parse_level("INFO ", out));
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
        "2026-11-08 14:59:00,123", "testpc", Level::Info, "app.sh", "hello");

    CHECK(entry == "2026-11-08 14:59:00,123 testpc [INFO] app.sh: hello\n");
    CHECK(entry.find('\033') == std::string::npos);
    CHECK(entry.back() == '\n');

    // Canonical labels: Warning displays as WARN, FATAL maps to CRITICAL.
    CHECK(format_file_entry("T", "PC", Level::Warning, "S", "M").find("[WARN]") != std::string::npos);
    CHECK(format_file_entry("T", "PC", Level::Critical, "S", "M").find("[CRITICAL]") != std::string::npos);

    // Multiline messages are preserved verbatim (minus \r via clean_message upstream).
    const std::string multi = format_file_entry("T", "PC", Level::Error, "S", "first\nsecond");
    CHECK(multi.find("first\nsecond") != std::string::npos);
    CHECK(multi.find('\033') == std::string::npos);
}

TEST_CASE("format_console_output plain branches depend on width") {
    const std::string full = format_console_output(
        "2026-11-08 14:59:00,123", "testpc", Level::Info, "app.sh", "hello", false, 180);
    CHECK(full.find("2026-11-08 14:59:00,123") != std::string::npos);
    CHECK(full.find("testpc") != std::string::npos);
    CHECK(full.find("app.sh") != std::string::npos);
    CHECK(full.find("[INFO]") != std::string::npos);
    CHECK(full.find("hello") != std::string::npos);
    CHECK(full.find('\033') == std::string::npos);
    CHECK(full.back() == '\n');

    const std::string medium = format_console_output(
        "2026-11-08 14:59:00,123", "testpc", Level::Info, "app.sh", "hello", false, 120);
    CHECK(medium.find("app.sh") != std::string::npos);
    CHECK(medium.find("[INFO]") != std::string::npos);
    CHECK(medium.find("2026-11-08") == std::string::npos);
    CHECK(medium.find("testpc") == std::string::npos);

    const std::string narrow = format_console_output(
        "2026-11-08 14:59:00,123", "testpc", Level::Info, "app.sh", "hello", false, 80);
    CHECK(narrow.find("[INFO]") != std::string::npos);
    CHECK(narrow.find("hello") != std::string::npos);
    CHECK(narrow.find("app.sh") == std::string::npos);
    CHECK(narrow.find("2026-11-08") == std::string::npos);
}

TEST_CASE("format_console_output width thresholds are inclusive") {
    // 179 -> medium, 180 -> full, 119 -> narrow, 120 -> medium.
    const std::string at179 = format_console_output("T180", "PC180", Level::Info, "S180", "m", false, 179);
    CHECK(at179.find("T180") == std::string::npos);
    CHECK(at179.find("S180") != std::string::npos);

    const std::string at180 = format_console_output("T180", "PC180", Level::Info, "S180", "m", false, 180);
    CHECK(at180.find("T180") != std::string::npos);

    const std::string at119 = format_console_output("T119", "PC119", Level::Info, "S119", "m", false, 119);
    CHECK(at119.find("S119") == std::string::npos);

    const std::string at120 = format_console_output("T120", "PC120", Level::Info, "S120", "m", false, 120);
    CHECK(at120.find("S120") != std::string::npos);
}

TEST_CASE("format_console_output colored output contains ANSI codes") {
    const std::string full = format_console_output(
        "2026-11-08 14:59:00,123", "testpc", Level::Warning, "app.sh", "hello", true, 180);
    CHECK(full.find('\033') != std::string::npos);
    CHECK(full.find("[WARN]") != std::string::npos);
    CHECK(full.find("hello") != std::string::npos);
    CHECK(full.find(std::string(ANSI_RESET)) != std::string::npos);
    CHECK(full.back() == '\n');

    const std::string narrow = format_console_output("T", "PC", Level::Error, "S", "msg", true, 80);
    CHECK(narrow.find('\033') != std::string::npos);
    CHECK(narrow.find("[ERROR]") != std::string::npos);
    CHECK(narrow.find("msg") != std::string::npos);
}

TEST_CASE("timestamp uses YYYY-MM-DD HH:MM:SS,mmm format") {
    const std::string ts = timestamp();
    REQUIRE(ts.size() == 23);
    CHECK(ts[4] == '-');
    CHECK(ts[7] == '-');
    CHECK(ts[10] == ' ');
    CHECK(ts[13] == ':');
    CHECK(ts[16] == ':');
    CHECK(ts[19] == ',');
    for (std::size_t i = 0; i < ts.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19)
            continue;
        CHECK_MESSAGE(std::isdigit(static_cast<unsigned char>(ts[i])), "index=", i);
    }
}

TEST_CASE("ANSI constants and version macro are defined") {
    CHECK_FALSE(ANSI_RESET.empty());
    CHECK(ANSI_RESET[0] == '\033');
    CHECK_FALSE(std::string(ANSI_RED).empty());
    CHECK(std::string(LOGAMUFFIN_VERSION).find('.') != std::string::npos);
    CHECK_FALSE(std::string(LOGAMUFFIN_VERSION).empty());
}
