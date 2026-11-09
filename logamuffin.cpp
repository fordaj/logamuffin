#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "config.hpp"
#include "log_levels.hpp"
#include "logamuffin.hpp"
#include "platform.hpp"
#include "text_utils.hpp"
#include "version.hpp"

using namespace logamuffin;

namespace {

// --config=<path> equals form. Prefix length replaces the former
// hard-coded substr(9) at two call sites.
constexpr std::string_view kConfigEqualsPrefix = "--config=";
constexpr std::size_t kConfigEqualsLen = kConfigEqualsPrefix.size();
constexpr std::string_view kConfigMissingPathMessage =
    "logamuffin: --config requires a path\n";

// Full help text (static part of print_help). Kept as one named constant
// so print_help stays a trivial emitter; usage still builds its dynamic
// levels line via valid_levels_string(cfg).
constexpr std::string_view kHelpText =
    "logamuffin - cross-platform logging utility\n"
    "\n"
    "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
    "[MESSAGE ...]\n"
    "\n"
    "LEVEL is a level name (e.g. TRACE, INFO) or a numeric value "
    "(e.g. 50). A number that matches a configured category value "
    "uses that category; any other number is logged as-is "
    "(e.g. [50]) and is filtered against the minimum level.\n"
    "\n"
    "Default levels (name, numeric value, default color):\n"
    "  ALWAYS   -1 purple   (bypasses the minimum level filter)\n"
    "  TRACE     5 cyan\n"
    "  DEBUG    10 green\n"
    "  INFO     20 reset\n"
    "  WARN     30 yellow  (alias: WARNING)\n"
    "  ERROR    40 red\n"
    "  CRITICAL 50 purple  (alias: FATAL)\n"
    "\n"
    "Options:\n"
    "  --config <path>   JSON config file (aliases: "
    "--config=<path>).\n"
    "                    Also configurable with LOG_CONFIG.\n"
    "  --version         Print the version and exit.\n"
    "  --help, -h        Print this help and exit.\n"
    "\n"
    "Configuration (lowest to highest precedence):\n"
    "  built-in defaults\n"
    "  <cwd>/.logamuffin (autodiscovered JSON file; missing file\n"
    "                     means defaults)\n"
    "  LOG_CONFIG file, then --config file (single winning file)\n"
    "  environment variables (LOG_LEVEL, LOG_COLOR, LOG_FILE,\n"
    "                     LOG_SOURCE, LOG_LEVELS/LOG_CATEGORIES,\n"
    "                     LOG_LEVEL_VALUE_<NAME>, "
    "LOG_LEVEL_COLOR_<NAME>)\n"
    "\n"
    "Example config file:\n"
    "  {\n"
    "    \"level\": \"INFO\",\n"
    "    \"color\": true,\n"
    "    \"levels\": {\n"
    "      \"TRACE\": {\"value\": 5, \"color\": \"cyan\"},\n"
    "      \"NOTICE\": {\"value\": 25, \"color\": \"blue\"}\n"
    "    }\n"
    "  }\n"
    "\n"
    "Example environment definition:\n"
    "  LOG_LEVELS='NOTICE=25:blue,INFO=20:green' \\\n"
    "      logamuffin NOTICE \"hello\"\n";

// Config-aware source resolution (overload of logamuffin::get_source/1):
std::string get_source(const Config& cfg, const char* argv0) {
    if (cfg.source.has_value() && !cfg.source->empty())
        return basename(*cfg.source);
    return basename(argv0 ? argv0 : "logamuffin");
}

void print_usage(std::ostream& out, const Config& cfg) {
    out << "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
           "[MESSAGE ...]\n"
        << "LEVEL is a level name (e.g. TRACE) or a numeric value "
           "(e.g. 50).\n"
        << "Levels: " << valid_levels_string(cfg) << "\n"
        << "Options:\n"
        << "  --config <path>  Use the JSON config file at <path> "
           "(takes precedence over ./.logamuffin autodiscovery)\n"
        << "  --version        Print the logamuffin version\n"
        << "  --help           Print help and exit\n"
        << "\n"
        << "Custom categories can be defined with LOG_LEVELS, the config "
           "file, or the programmatic CLI categories. "
           "See README.md (Configuration) for details.\n";
}

void print_help(std::ostream& out) {
    out << kHelpText;
}

// ---------------------------------------------------------------------------
// --config helpers (single spelling for both positions)
// ---------------------------------------------------------------------------

bool is_config_flag(std::string_view arg) {
    return arg == "--config";
}

bool is_config_equals_flag(std::string_view arg) {
    return arg.starts_with(kConfigEqualsPrefix);
}

// Extracts the path after "--config=". Caller must have checked
// is_config_equals_flag(arg); empty result means missing path.
std::string take_config_equals(std::string_view arg) {
    return std::string(arg.substr(kConfigEqualsLen));
}

void report_config_missing_path() {
    std::cerr << kConfigMissingPathMessage;
}

// ---------------------------------------------------------------------------
// CLI parsing (leading options + --config after LEVEL)
// ---------------------------------------------------------------------------

struct ParsedArgs {
    std::optional<std::string> cliConfigPath;
    int levelIndex = 1;
    int messageStart = 1;
};

// Parses CLI args into ParsedArgs. Returns false when run() should return
// immediately with exitCode (error/help/version already printed);
// returns true when parsing succeeded and execution should continue.
bool parse_cli_args(int argc, char* argv[], ParsedArgs& out, int& exitCode) {
    // --version is only valid as the sole argument. With extra
    // arguments it is treated as a (unknown) level name, matching the
    // original executable and the CLI integration tests.
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "logamuffin " << LOGAMUFFIN_VERSION << '\n';
        exitCode = 0;
        return false;
    }

    // Parse leading options before LEVEL.
    bool wantHelp = false;
    int idx = 1;
    for (; idx < argc; ++idx) {
        const std::string arg = argv[idx];
        if (arg == "--") {
            ++idx;
            break;
        }
        if (arg == "--version") {
            // Not the sole argument: fall through as LEVEL so it is
            // reported as an unknown log level.
            break;
        }
        if (arg == "--help" || arg == "-h" || arg == "--usage") {
            wantHelp = true;
            continue;
        }
        if (is_config_flag(arg)) {
            if (idx + 1 >= argc) {
                report_config_missing_path();
                exitCode = 2;
                return false;
            }
            out.cliConfigPath = argv[idx + 1];
            ++idx; // consumed path
            continue;
        }
        if (is_config_equals_flag(arg)) {
            const std::string configPath = take_config_equals(arg);
            if (configPath.empty()) {
                report_config_missing_path();
                exitCode = 2;
                return false;
            }
            out.cliConfigPath = configPath;
            continue;
        }
        {
            int numericCandidate = 0;
            // A negative integer (e.g. -1) is a LEVEL, not an option.
            if (parse_int_strict(arg, numericCandidate) &&
                (arg.empty() || arg[0] == '+' || arg[0] == '-' ||
                 (arg[0] >= '0' && arg[0] <= '9'))) {
                // Only treat it as LEVEL when the whole arg is
                // numeric; leading-dash words stay unknown options.
                break;
            }
        }
        if (arg.rfind("--", 0) == 0 || (arg.size() > 1 && arg[0] == '-')) {
            std::cerr << "logamuffin: unknown option: " << arg << '\n';
            exitCode = 2;
            return false;
        }
        break;
    }

    if (wantHelp) {
        print_help(std::cout);
        exitCode = 0;
        return false;
    }

    // Also accept --config immediately after LEVEL (before MESSAGE),
    // e.g. `logamuffin INFO --config file.json "message"`.
    int messageStart = 0;
    if (idx < argc && idx + 1 < argc) {
        const std::string afterLevel = argv[idx + 1];
        if (is_config_flag(afterLevel)) {
            if (idx + 2 >= argc) {
                report_config_missing_path();
                exitCode = 2;
                return false;
            }
            out.cliConfigPath = argv[idx + 2];
            messageStart = idx + 3;
        } else if (is_config_equals_flag(afterLevel)) {
            const std::string configPath = take_config_equals(afterLevel);
            if (configPath.empty()) {
                report_config_missing_path();
                exitCode = 2;
                return false;
            }
            out.cliConfigPath = configPath;
            messageStart = idx + 2;
        } else {
            messageStart = idx + 1;
        }
    } else {
        messageStart = idx + 1;
    }

    out.levelIndex = idx;
    out.messageStart = messageStart;
    return true;
}

// ---------------------------------------------------------------------------
// Level resolution (word name or numeric value)
// ---------------------------------------------------------------------------

// Resolves argv[idx] to a LogLevel. Prints an unknown-level error and
// returns false on failure.
bool resolve_level(const Config& cfg, const char* rawArg, LogLevel& outLevel) {
    const std::string levelRaw = trim(rawArg);
    const std::string levelUpper = uppercase(levelRaw);
    if (const LogLevel* foundNamed =
            find_level_with_alias(cfg.levels, levelUpper)) {
        // Word name (including WARN/WARNING, CRITICAL/FATAL aliases).
        outLevel = *foundNamed;
        return true;
    }
    int numericValue = 0;
    if (!parse_int_strict(levelRaw, numericValue)) {
        std::cerr << "Unknown log level: " << rawArg
                  << ". Valid levels: " << valid_levels_string(cfg)
                  << " (or a numeric value like 50)\n";
        return false;
    }
    // Numeric LEVEL (issue #3): resolve to a category with the same
    // value when one exists (e.g. 50 -> CRITICAL, 25 -> NOTICE);
    // otherwise emit an ad-hoc numeric level ([25], [50]) with the
    // default color. Filtering and stderr routing use the number.
    if (const LogLevel* byValue =
            find_level_by_value(cfg.levels, numericValue)) {
        outLevel = *byValue;
    } else {
        outLevel = ad_hoc_numeric_level(numericValue);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Message + filtering
// ---------------------------------------------------------------------------

std::string build_message(int argc, char* argv[], int messageStart) {
    // Join all message arguments with spaces.
    std::string message;
    for (int i = messageStart; i < argc; ++i) {
        if (i > messageStart)
            message += ' ';
        message += argv[i];
    }
    return clean_message(std::move(message));
}

bool should_emit_level(const Config& cfg, const LogLevel& level) {
    // ALWAYS bypasses the threshold; everything else is at-or-above.
    const bool isAlways = (level.name == "ALWAYS");
    return isAlways || level.value >= cfg.thresholdValue;
}

// ---------------------------------------------------------------------------
// Output (console + file)
// ---------------------------------------------------------------------------

// Shared timestamp/hostname/source for both console and file lines so a
// single invocation is self-consistent.
struct LogContext {
    std::string time;
    std::string host;
    std::string source;
};

LogContext make_log_context(const Config& cfg, const char* argv0) {
    return LogContext{timestamp(), hostname(), get_source(cfg, argv0)};
}

// Formats and writes the console line for one log invocation.
void emit_console_line(const Config& cfg, const LogLevel& level,
                       const std::string& message,
                       const LogContext& ctx) {
    FILE* stream = is_error_level(level) ? stderr : stdout;
    const bool color = cfg.colorEnabled.has_value()
        ? *cfg.colorEnabled : terminal(stream);
    const int width = terminal(stream) ? terminal_width() : kFullFormatWidth;

    const std::string output = format_console_output(
        ctx.time, ctx.host, level, ctx.source, message, color, width);

    std::fwrite(output.data(), 1, output.size(), stream);
    std::fflush(stream);
}

// Appends the plain-text file entry. Returns 1 when the log directory
// cannot be created (matches prior behavior), otherwise 0. Append
// failure only warns (console already succeeded).
int write_file_log(const Config& cfg, const LogLevel& level,
                   const std::string& message,
                   const LogContext& ctx) {
    // Plain-text file logging, independent of console colors.
    const std::filesystem::path file =
        cfg.logFile.has_value() && !cfg.logFile->empty()
            ? std::filesystem::path(*cfg.logFile)
            : std::filesystem::path(".logs/log.log");

    std::error_code ec;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(), ec);

        if (ec) {
            std::cerr << "logamuffin: failed to create log directory: "
                      << ec.message() << '\n';
            return 1;
        }
    }

    // Construct the complete entry before acquiring the lock.
    const std::string entry =
        format_file_entry(ctx.time, ctx.host, level, ctx.source, message);

    if (!append_locked(file, entry)) {
        std::cerr << "logamuffin: failed to append log file\n";
    }

    return 0;
}

// Orchestrates one log invocation: parse -> config -> level -> filter
// -> console + file. Each phase lives in its own helper; this function
// only sequences them with guard-clause early returns.
int run(int argc, char* argv[]) {
    ParsedArgs args;
    int exitCode = 0;
    if (!parse_cli_args(argc, argv, args, exitCode))
        return exitCode;

    Config cfg;
    std::string cfgErr;
    if (!load_config(args.cliConfigPath, cfg, cfgErr)) {
        std::cerr << "logamuffin: " << cfgErr << '\n';
        return 2;
    }

    if (args.levelIndex >= argc || args.messageStart >= argc) {
        print_usage(std::cerr, cfg);
        return 2;
    }

    LogLevel level;
    if (!resolve_level(cfg, argv[args.levelIndex], level))
        return 2;

    const std::string message =
        build_message(argc, argv, args.messageStart);

    // Filter before doing timestamp, formatting, and file I/O.
    if (!should_emit_level(cfg, level))
        return 0;

    const LogContext ctx = make_log_context(cfg, argv[0]);
    emit_console_line(cfg, level, message, ctx);

    return write_file_log(cfg, level, message, ctx);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "logamuffin: " << e.what() << '\n';
        return 1;
    }
}
