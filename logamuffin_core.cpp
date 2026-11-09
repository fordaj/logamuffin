#include "logamuffin.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "text_utils.hpp"

namespace logamuffin
{

  namespace
  {

    // Timestamp: "YYYY-MM-DD HH:MM:SS,mmm" (27 chars + null).
    constexpr std::size_t kTimestampBufferSize = 40;
    constexpr long long kMillisPerSecond       = 1000;
    constexpr const char* kTimestampFormat     = "%Y-%m-%d %H:%M:%S";

    // Index of INFO in kBuiltinLevels for info() fallback.
    constexpr std::size_t kInfoFallbackIndex = 3;

    // Name/numeric → Level derived from the single kBuiltinLevels table in
    // logamuffin.hpp. Aliases (WARNING→WARN, FATAL→CRITICAL) are single-sourced
    // in log_levels.hpp (kLevelAliases). Numeric forms map to built-ins only;
    // custom numeric levels are handled by resolve_level() in logamuffin.cpp.
    bool lookup_level_name(std::string_view upperValue, Level& out)
    {
      for (const auto& entry: kBuiltinLevels)
      {
        if (upperValue == entry.name)
        {
          out = entry.level;
          return true;
        }
      }
      // Aliases consult the single-source table in log_levels.hpp.
      if (const auto canonical = level_alias_target(upperValue); !canonical.empty())
      {
        for (const auto& entry: kBuiltinLevels)
        {
          if (canonical == entry.name)
          {
            out = entry.level;
            return true;
          }
        }
      }
      // Numeric built-in values (-1, 5, 10, 20, 30, 40, 50). Guard against
      // surrounding whitespace so " 5 " is rejected like the old table.
      int numeric = 0;
      if (trim(std::string(upperValue)) == upperValue && parse_int_strict(std::string(upperValue), numeric))
      {
        for (const auto& entry: kBuiltinLevels)
        {
          if (numeric == static_cast<int>(entry.level))
          {
            out = entry.level;
            return true;
          }
        }
      }
      return false;
    }

  } // namespace

  std::string uppercase(std::string s)
  {
    for (char& c: s)
    {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
  }

  std::string basename(std::string path)
  {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
  }

  bool env_enabled(const char* name, bool fallback)
  {
    const char* value = std::getenv(name);
    if (!value)
    {
      return fallback;
    }
    const std::string upperValue = uppercase(value);
    return is_truthy_token(upperValue);
  }

  bool parse_level(std::string value, Level& out)
  {
    const std::string upperValue = uppercase(std::move(value));
    return lookup_level_name(upperValue, out);
  }

  const LevelInfo& info(Level level)
  {
    for (const auto& levelInfo: kBuiltinLevels)
    {
      if (levelInfo.level == level)
      {
        return levelInfo;
      }
    }
    // Unknown enum values fall back to INFO rather than UB.
    return kBuiltinLevels[kInfoFallbackIndex];
  }

  std::string timestamp()
  {
    using namespace std::chrono;

    const auto now    = system_clock::now();
    const auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count() % kMillisPerSecond;

    const std::time_t time = system_clock::to_time_t(now);
    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    char buffer[kTimestampBufferSize]{};
    std::strftime(buffer, sizeof(buffer), kTimestampFormat, &tm);

    char result[kTimestampBufferSize]{};
    std::snprintf(result, sizeof(result), "%s,%03lld", buffer, static_cast<long long>(millis));

    return result;
  }

  std::string clean_message(std::string message)
  {
    message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
    return message;
  }

  std::string get_source(const char* argv0)
  {
    const char* source = std::getenv("LOG_SOURCE");

    if (source && *source)
    {
      return basename(source);
    }

    return basename(argv0 ? argv0 : "logamuffin");
  }

  bool should_emit(Level level, Level threshold)
  {
    return level == Level::Always || static_cast<int>(level) >= static_cast<int>(threshold);
  }

  std::string join_message(const std::vector<std::string>& parts)
  {
    std::string message;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
      if (i > 0)
      {
        message += ' ';
      }
      message += parts[i];
    }
    return clean_message(std::move(message));
  }

  std::string join_message(int argc, char* argv[], int start)
  {
    std::vector<std::string> parts;
    for (int i = start; i < argc; ++i)
    {
      parts.emplace_back(argv[i] ? argv[i] : "");
    }
    return join_message(parts);
  }

  std::string format_file_entry(std::string_view time, std::string_view host, const LogLevel& level, std::string_view source, std::string_view message)
  {
    std::string entry;
    entry.reserve(time.size() + host.size() + source.size() + message.size() + level.name.size() + kFileEntryReserve);

    entry += time;
    entry += ' ';
    entry += host;
    entry += " [";
    entry += level.name;
    entry += "] ";
    entry += source;
    entry += ": ";
    entry += message;
    entry += '\n';

    return entry;
  }

  std::string format_file_entry(std::string_view time, std::string_view host, Level level, std::string_view source, std::string_view message)
  {
    const auto& levelInfo = info(level);
    const LogLevel logLevel{ std::string(levelInfo.name), std::string(levelInfo.color), static_cast<int>(level) };
    return format_file_entry(time, host, logLevel, source, message);
  }

  std::string format_console_output(std::string_view time, std::string_view host, const LogLevel& level, std::string_view source, std::string_view message, bool color, int width)
  {
    std::string prefix;

    if (width >= kFullFormatWidth)
    {
      if (color)
      {
        prefix += ANSI_GREEN;
        prefix += time;
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += ANSI_PURPLE;
        prefix += host;
        prefix += ANSI_RESET;
        prefix += ' ';
        prefix += ANSI_BLUE;
        prefix += source;
        prefix += ':';
        prefix += ANSI_RESET;
        prefix += ' ';
      }
      else
      {
        prefix += time;
        prefix += " ";
        prefix += host;
        prefix += " ";
        prefix += source;
        prefix += ": ";
      }
    }
    else if (width >= kMediumFormatWidth)
    {
      if (color)
      {
        prefix += ANSI_BLUE;
        prefix += source;
        prefix += ':';
        prefix += ANSI_RESET;
        prefix += ' ';
      }
      else
      {
        prefix += source;
        prefix += ": ";
      }
    }

    if (color)
    {
      prefix += ANSI_HI_BLACK;
      prefix += '[';
      prefix += level.name;
      prefix += ']';
      prefix += ANSI_RESET;
      prefix += ' ';
      prefix += level.color;
    }
    else
    {
      prefix += '[';
      prefix += level.name;
      prefix += "] ";
    }

    std::string output;
    output.reserve(prefix.size() + message.size() + kConsoleOutputReserve);
    output += prefix;
    output += message;

    if (color)
    {
      output += ANSI_RESET;
    }

    output += '\n';

    return output;
  }

  std::string format_console_output(std::string_view time, std::string_view host, Level level, std::string_view source, std::string_view message, bool color, int width)
  {
    const auto& levelInfo = info(level);
    const LogLevel logLevel{ std::string(levelInfo.name), std::string(levelInfo.color), static_cast<int>(level) };
    return format_console_output(time, host, logLevel, source, message, color, width);
  }

} // namespace logamuffin
