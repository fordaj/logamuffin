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

// Terminal-width thresholds for console prefix detail.
constexpr int kFullFormatWidth = 180;
constexpr int kMediumFormatWidth = 120;

// Config-aware source resolution (overload of logamuffin::get_source/1):
std::string get_source(const Config& cfg, const char* argv0) {
    if (cfg.source.has_value() && !cfg.source->empty())
        return basename(*cfg.source);
    return basename(argv0 ? argv0 : "logamuffin");
}

void print_usage(std::ostream& out, const Config& cfg) {
    out << "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
           "[MESSAGE ...]\n"
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
    out << "logamuffin - cross-platform logging utility\n"
        << "\n"
        << "Usage: logamuffin [--config <path>] LEVEL MESSAGE "
           "[MESSAGE ...]\n"
        << "\n"
        << "Default levels (name, numeric value, default color):\n"
        << "  ALWAYS   -1 purple   (bypasses the minimum level filter)\n"
        << "  TRACE     5 cyan\n"
        << "  DEBUG    10 green\n"
        << "  INFO     20 reset\n"
        << "  WARN     30 yellow  (alias: WARNING)\n"
        << "  ERROR    40 red\n"
        << "  CRITICAL 50 purple  (alias: FATAL)\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>   JSON config file (aliases: "
           "--config=<path>).\n"
        << "                    Also configurable with LOG_CONFIG.\n"
        << "  --version         Print the version and exit.\n"
        << "  --help, -h        Print this help and exit.\n"
        << "\n"
        << "Configuration (lowest to highest precedence):\n"
        << "  built-in defaults\n"
        << "  <cwd>/.logamuffin (autodiscovered JSON file; missing file\n"
        << "                     means defaults)\n"
        << "  LOG_CONFIG file, then --config file (single winning file)\n"
        << "  environment variables (LOG_LEVEL, LOG_COLOR, LOG_FILE,\n"
        << "                     LOG_SOURCE, LOG_LEVELS/LOG_CATEGORIES,\n"
        << "                     LOG_LEVEL_VALUE_<NAME>, "
           "LOG_LEVEL_COLOR_<NAME>)\n"
        << "\n"
        << "Example config file:\n"
        << "  {\n"
        << "    \"level\": \"INFO\",\n"
        << "    \"color\": true,\n"
        << "    \"levels\": {\n"
        << "      \"TRACE\": {\"value\": 5, \"color\": \"cyan\"},\n"
        << "      \"NOTICE\": {\"value\": 25, \"color\": \"blue\"}\n"
        << "    }\n"
        << "  }\n"
        << "\n"
        << "Example environment definition:\n"
        << "  LOG_LEVELS='NOTICE=25:blue,INFO=20:green' \\\n"
        << "      logamuffin NOTICE \"hello\"\n";
}

int run(int argc, char* argv[]) {
    // --version is only valid as the sole argument. With extra
    // arguments it is treated as a (unknown) level name, matching the
    // original executable and the CLI integration tests.
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "logamuffin " << LOGAMUFFIN_VERSION << '\n';
        return 0;
    }
    // Parse leading options before LEVEL.
    std::optional<std::string> cliConfigPath;
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
        if (arg == "--config") {
            if (idx + 1 >= argc) {
                std::cerr << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = argv[idx + 1];
            ++idx; // consumed path
            continue;
        }
        if (arg.rfind("--config=", 0) == 0) {
            const std::string path = arg.substr(9);
            if (path.empty()) {
                std::cerr
                    << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = path;
            continue;
        }
        if (arg.rfind("--", 0) == 0 || (arg.size() > 1 && arg[0] == '-')) {
            std::cerr << "logamuffin: unknown option: " << arg << '\n';
            return 2;
        }
        break;
    }

    if (wantHelp) {
        print_help(std::cout);
        return 0;
    }

    // Also accept --config immediately after LEVEL (before MESSAGE),
    // e.g. `logamuffin INFO --config file.json "message"`.
    int messageStart = 0;
    if (idx < argc && idx + 1 < argc) {
        const std::string afterLevel = argv[idx + 1];
        if (afterLevel == "--config") {
            if (idx + 2 >= argc) {
                std::cerr << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = argv[idx + 2];
            messageStart = idx + 3;
        } else if (afterLevel.rfind("--config=", 0) == 0) {
            const std::string path = afterLevel.substr(9);
            if (path.empty()) {
                std::cerr
                    << "logamuffin: --config requires a path\n";
                return 2;
            }
            cliConfigPath = path;
            messageStart = idx + 2;
        } else {
            messageStart = idx + 1;
        }
    } else {
        messageStart = idx + 1;
    }

    Config cfg;
    std::string cfgErr;
    if (!load_config(cliConfigPath, cfg, cfgErr)) {
        std::cerr << "logamuffin: " << cfgErr << '\n';
        return 2;
    }

    if (idx >= argc || messageStart >= argc) {
        print_usage(std::cerr, cfg);
        return 2;
    }

    const std::string levelUpper = uppercase(trim(argv[idx]));
    const LogLevel* found =
        find_level_with_alias(cfg.levels, levelUpper);
    if (!found) {
        std::cerr << "Unknown log level: " << argv[idx]
                  << ". Valid levels: " << valid_levels_string(cfg)
                  << '\n';
        return 2;
    }

    // Copy the resolved level so filtering and output stay stable even
    // though cfg.levels is already sorted.
    const LogLevel level = *found;

    // Join all message arguments with spaces.
    std::string message;
    for (int i = messageStart; i < argc; ++i) {
        if (i > messageStart)
            message += ' ';
        message += argv[i];
    }
    message = clean_message(std::move(message));

    // Filter before doing timestamp, formatting, and file I/O.
    const bool isAlways = (level.name == "ALWAYS");
    if (!isAlways && level.value < cfg.thresholdValue) {
        return 0;
    }

    const std::string time = timestamp();
    const std::string pc = hostname();
    const std::string source = get_source(cfg, argv[0]);

    FILE* stream = is_error_level(level) ? stderr : stdout;
    const bool color = cfg.colorEnabled.has_value()
        ? *cfg.colorEnabled : terminal(stream);

    const int width = terminal(stream) ? terminal_width() : kFullFormatWidth;

    std::string prefix;

    // Timestamp and hostname at >= 180 columns;
    // source filename at >= 120 columns.
    if (width >= kFullFormatWidth) {
        if (color) {
            prefix += ANSI_GREEN;
            prefix += time;
            prefix += ANSI_RESET;
            prefix += ' ';
            prefix += ANSI_PURPLE;
            prefix += pc;
            prefix += ANSI_RESET;
            prefix += ' ';
            prefix += ANSI_BLUE;
            prefix += source;
            prefix += ':';
            prefix += ANSI_RESET;
            prefix += ' ';
        } else {
            prefix += time + " " + pc + " " + source + ": ";
        }
    } else if (width >= kMediumFormatWidth) {
        if (color) {
            prefix += ANSI_BLUE;
            prefix += source;
            prefix += ':';
            prefix += ANSI_RESET;
            prefix += ' ';
        } else {
            prefix += source + ": ";
        }
    }

    // Level is always included, regardless of terminal width.
    if (color) {
        prefix += ANSI_HI_BLACK;
        prefix += '[';
        prefix += level.name;
        prefix += ']';
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += level.color;
    } else {
        prefix += '[';
        prefix += level.name;
        prefix += "] ";
    }


    std::string output;
    output.reserve(prefix.size() + message.size() + 16);
    output += prefix;
    output += message;

    if (color)
        output += ANSI_RESET;

    output += '\n';

    std::fwrite(output.data(), 1, output.size(), stream);
    std::fflush(stream);

    // Plain-text file logging, independent of console colors.
    const std::filesystem::path file =
        cfg.logFile.has_value() && !cfg.logFile->empty()
            ? std::filesystem::path(*cfg.logFile)
            : std::filesystem::path(".logs/log.log");

    std::error_code ec;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(
            file.parent_path(), ec);

        if (ec) {
            std::cerr << "logamuffin: failed to create log directory: "
                      << ec.message() << '\n';
            return 1;
        }
    }

    // Construct the complete entry before acquiring the lock.
    std::string entry;
    entry.reserve(
        time.size() + pc.size() + source.size() +
        message.size() + level.name.size() + 8);

    entry += time;
    entry += ' ';
    entry += pc;
    entry += " [";
    entry += level.name;
    entry += "] ";
    entry += source;
    entry += ": ";
    entry += message;
    entry += '\n';

    if (!append_locked(file, entry)) {
        std::cerr << "logamuffin: failed to append log file\n";
    }

    return 0;
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
