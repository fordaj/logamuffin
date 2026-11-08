// Logamuffin unit tests (doctest)
//
// Tests for core helpers in logamuffin.cpp. The production CLI entry point
// is excluded via LOGAMUFFIN_NO_MAIN so the translation unit can link
// against doctest's implementation-provided main().
//
// Build (from the repository root):
//   clang++ -std=c++20 -O2 tests/unit/test_logamuffin.cpp -o build/tests/test_logamuffin
// Run:
//   ./build/tests/test_logamuffin
// Or run everything with:
//   ./run_tests.sh

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../../tools/doctest/v2.5.3/doctest.h"

#define LOGAMUFFIN_NO_MAIN
#include "../../logamuffin.cpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace {

void set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
    const std::string expr = name + "=" + value;
    _putenv(expr.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void unset_env(const std::string& name) {
#ifdef _WIN32
    const std::string expr = name + "=";
    _putenv(expr.c_str());
#else
    unsetenv(name.c_str());
#endif
}

// Restores the previous value of an environment variable on destruction.
struct EnvGuard {
    std::string name;
    std::string oldValue;
    bool hadValue = false;

    explicit EnvGuard(const std::string& n) : name(n) {
        const char* v = std::getenv(name.c_str());
        hadValue = (v != nullptr);
        oldValue = hadValue ? v : "";
    }

    ~EnvGuard() {
        if (hadValue) {
            set_env(name, oldValue);
        } else {
            unset_env(name);
        }
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
};

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

// ---------------------------------------------------------------------------
// uppercase
// ---------------------------------------------------------------------------

TEST_CASE("uppercase converts ASCII to uppercase") {
    CHECK(uppercase("hello") == "HELLO");
    CHECK(uppercase("Install") == "INSTALL");
    CHECK(uppercase("info123") == "INFO123");
    CHECK(uppercase("WARN/Warn/warning") == "WARN/WARN/WARNING");
    CHECK(uppercase("") == "");
    CHECK(uppercase("already-UPPER") == "ALREADY-UPPER");
    CHECK(uppercase("with spaces") == "WITH SPACES");
}

// ---------------------------------------------------------------------------
// basename
// ---------------------------------------------------------------------------

TEST_CASE("basename extracts last path component") {
    CHECK(basename("logamuffin") == "logamuffin");
    CHECK(basename("/usr/bin/logamuffin") == "logamuffin");
    CHECK(basename("C:\\tools\\logamuffin.exe") == "logamuffin.exe");
    CHECK(basename("/a/b/c.txt") == "c.txt");
    CHECK(basename("a/b\\c") == "c");
    CHECK(basename("") == "");
}

// ---------------------------------------------------------------------------
// parse_level
// ---------------------------------------------------------------------------

TEST_CASE("parse_level accepts canonical levels") {
    Level out = Level::Info;
    CHECK(parse_level("TRACE", out));
    CHECK(out == Level::Trace);
    CHECK(parse_level("DEBUG", out));
    CHECK(out == Level::Debug);
    CHECK(parse_level("INFO", out));
    CHECK(out == Level::Info);
    CHECK(parse_level("WARN", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("WARNING", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("ERROR", out));
    CHECK(out == Level::Error);
    CHECK(parse_level("CRITICAL", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("FATAL", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("ALWAYS", out));
    CHECK(out == Level::Always);
}

TEST_CASE("parse_level is case-insensitive") {
    Level out = Level::Info;
    CHECK(parse_level("trace", out));
    CHECK(out == Level::Trace);
    CHECK(parse_level("Debug", out));
    CHECK(out == Level::Debug);
    CHECK(parse_level("info", out));
    CHECK(out == Level::Info);
    CHECK(parse_level("Warn", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("warning", out));
    CHECK(out == Level::Warning);
    CHECK(parse_level("Error", out));
    CHECK(out == Level::Error);
    CHECK(parse_level("critical", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("fatal", out));
    CHECK(out == Level::Critical);
    CHECK(parse_level("always", out));
    CHECK(out == Level::Always);
    CHECK(parse_level("iNfO", out));
    CHECK(out == Level::Info);
}

TEST_CASE("parse_level rejects unknown values") {
    Level out = Level::Info;
    CHECK_FALSE(parse_level("", out));
    CHECK_FALSE(parse_level("INVALID", out));
    CHECK_FALSE(parse_level("BOGUS", out));
    CHECK_FALSE(parse_level("NOTICE", out));
    CHECK_FALSE(parse_level(" INFO", out));
    CHECK_FALSE(parse_level("INFO ", out));
    CHECK_FALSE(parse_level("WARN1", out));
}

// ---------------------------------------------------------------------------
// Severity ordering / filtering semantics
// ---------------------------------------------------------------------------

TEST_CASE("severity levels are ordered by numeric value") {
    CHECK(static_cast<int>(Level::Always) == -1);
    CHECK(static_cast<int>(Level::Trace) == 5);
    CHECK(static_cast<int>(Level::Debug) == 10);
    CHECK(static_cast<int>(Level::Info) == 20);
    CHECK(static_cast<int>(Level::Warning) == 30);
    CHECK(static_cast<int>(Level::Error) == 40);
    CHECK(static_cast<int>(Level::Critical) == 50);

    // Monotonic ordering
    CHECK(Level::Trace < Level::Debug);
    CHECK(Level::Debug < Level::Info);
    CHECK(Level::Info < Level::Warning);
    CHECK(Level::Warning < Level::Error);
    CHECK(Level::Error < Level::Critical);
}

TEST_CASE("filtering helper mirrors run() threshold logic") {
    // Same condition as run(): level != Always && level < threshold => filtered.
    auto is_filtered = [](Level level, Level threshold) {
        return level != Level::Always &&
               static_cast<int>(level) < static_cast<int>(threshold);
    };

    // At INFO threshold: TRACE/DEBUG filtered, INFO+ kept, ALWAYS bypasses.
    CHECK(is_filtered(Level::Trace, Level::Info));
    CHECK(is_filtered(Level::Debug, Level::Info));
    CHECK_FALSE(is_filtered(Level::Info, Level::Info));
    CHECK_FALSE(is_filtered(Level::Warning, Level::Info));
    CHECK_FALSE(is_filtered(Level::Always, Level::Info));

    // At ERROR threshold: WARNING filtered, ERROR kept.
    CHECK(is_filtered(Level::Warning, Level::Error));
    CHECK_FALSE(is_filtered(Level::Error, Level::Error));
    CHECK_FALSE(is_filtered(Level::Critical, Level::Error));
    CHECK_FALSE(is_filtered(Level::Always, Level::Error));

    // At TRACE threshold: nothing filtered except nothing (Always bypasses).
    CHECK_FALSE(is_filtered(Level::Trace, Level::Trace));
}

// ---------------------------------------------------------------------------
// info() / level table
// ---------------------------------------------------------------------------

TEST_CASE("info() returns canonical names") {
    CHECK(info(Level::Always).name == "ALWAYS");
    CHECK(info(Level::Trace).name == "TRACE");
    CHECK(info(Level::Debug).name == "DEBUG");
    CHECK(info(Level::Info).name == "INFO");
    CHECK(info(Level::Warning).name == "WARN");
    CHECK(info(Level::Error).name == "ERROR");
    CHECK(info(Level::Critical).name == "CRITICAL");
}

TEST_CASE("info() provides colors and fallback") {
    for (const auto& entry : levels) {
        CHECK_FALSE(entry.color.empty());
    }
    // Unknown level falls back to INFO entry (levels[3]).
    const auto& fallback = info(static_cast<Level>(999));
    CHECK(fallback.name == "INFO");
}

TEST_CASE("levels table has 7 entries") {
    CHECK(levels.size() == 7);
}

// ---------------------------------------------------------------------------
// env_enabled (configuration)
// ---------------------------------------------------------------------------

TEST_CASE("env_enabled returns fallback when unset") {
    EnvGuard guard("LOG_COLOR_UNIT_TEST");
    unset_env("LOG_COLOR_UNIT_TEST");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", true));
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", false));
}

TEST_CASE("env_enabled recognizes truthy values") {
    EnvGuard guard("LOG_COLOR_UNIT_TEST");
    set_env("LOG_COLOR_UNIT_TEST", "1");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", false));
    set_env("LOG_COLOR_UNIT_TEST", "TRUE");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", false));
    set_env("LOG_COLOR_UNIT_TEST", "true");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", false));
    set_env("LOG_COLOR_UNIT_TEST", "Yes");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", false));
    set_env("LOG_COLOR_UNIT_TEST", "ON");
    CHECK(env_enabled("LOG_COLOR_UNIT_TEST", false));
}

TEST_CASE("env_enabled rejects falsy and unknown values") {
    EnvGuard guard("LOG_COLOR_UNIT_TEST");
    set_env("LOG_COLOR_UNIT_TEST", "0");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
    set_env("LOG_COLOR_UNIT_TEST", "FALSE");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
    set_env("LOG_COLOR_UNIT_TEST", "no");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
    set_env("LOG_COLOR_UNIT_TEST", "OFF");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
    set_env("LOG_COLOR_UNIT_TEST", "");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
    set_env("LOG_COLOR_UNIT_TEST", "maybe");
    CHECK_FALSE(env_enabled("LOG_COLOR_UNIT_TEST", true));
}

// ---------------------------------------------------------------------------
// clean_message (message formatting)
// ---------------------------------------------------------------------------

TEST_CASE("clean_message strips carriage returns") {
    CHECK(clean_message("hello") == "hello");
    CHECK(clean_message("a\r\nb") == "a\nb");
    CHECK(clean_message("a\rb\r") == "ab");
    CHECK(clean_message("") == "");
    CHECK(clean_message("\r\n") == "\n");
    CHECK(clean_message("line1\nline2\nline3") == "line1\nline2\nline3");
}

// ---------------------------------------------------------------------------
// timestamp formatting
// ---------------------------------------------------------------------------

TEST_CASE("timestamp matches YYYY-MM-DD HH:MM:SS,mmm") {
    const std::string ts = timestamp();
    const std::regex re(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3}$)");
    CHECK(std::regex_match(ts, re));
}

// ---------------------------------------------------------------------------
// get_source (configuration)
// ---------------------------------------------------------------------------

TEST_CASE("get_source uses LOG_SOURCE when set") {
    EnvGuard guard("LOG_SOURCE");
    set_env("LOG_SOURCE", "/tmp/myapp.sh");
    CHECK(get_source("/usr/bin/logamuffin") == "myapp.sh");
    set_env("LOG_SOURCE", "C:\\tools\\worker.exe");
    CHECK(get_source("argv0") == "worker.exe");
}

TEST_CASE("get_source falls back to argv0 basename") {
    EnvGuard guard("LOG_SOURCE");
    unset_env("LOG_SOURCE");
    CHECK(get_source("/usr/bin/logamuffin") == "logamuffin");
    CHECK(get_source("logamuffin") == "logamuffin");
    CHECK(get_source(nullptr) == "logamuffin");
}

TEST_CASE("get_source ignores empty LOG_SOURCE") {
    EnvGuard guard("LOG_SOURCE");
    set_env("LOG_SOURCE", "");
    CHECK(get_source("/bin/mylogger") == "mylogger");
}

// ---------------------------------------------------------------------------
// hostname / terminal_width (environment helpers)
// ---------------------------------------------------------------------------

TEST_CASE("hostname returns a non-empty string") {
    CHECK_FALSE(hostname().empty());
}

TEST_CASE("terminal_width returns a positive value") {
    CHECK(terminal_width() > 0);
}

// ---------------------------------------------------------------------------
// append_locked (file output)
// ---------------------------------------------------------------------------

TEST_CASE("append_locked creates and appends to a file") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto file = dir / "logamuffin_unit_append_test.log";
    std::filesystem::remove(file);

    CHECK(append_locked(file, "first\n"));
    CHECK(append_locked(file, "second\n"));
    CHECK(read_file(file) == "first\nsecond\n");

    std::filesystem::remove(file);
}

TEST_CASE("append_locked fails for missing parent directory") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto file = dir / "logamuffin_nonexistent_dir_xyz" / "test.log";
    // Do not create the parent; open() should fail.
    CHECK_FALSE(append_locked(file, "data\n"));
}
