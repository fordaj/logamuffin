#include "config.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include "color.hpp"
#include "json_parser.hpp"
#include "logamuffin.hpp"
#include "text_utils.hpp"

namespace logamuffin
{

  namespace
  {

    // Maximum accepted config file size (4 MiB).
    constexpr std::size_t kMaxConfigFileSizeBytes = 4 * 1024 * 1024;

    // Maximum numeric SGR color code accepted via JSON numbers.
    constexpr int kMaxColorCode = 999;

    // ---------------------------------------------------------------------------
    // Alias tables (normalized keys from normalize_key())
    // ---------------------------------------------------------------------------
    // Each table lists every accepted spelling for one semantic concept.
    // matches_any() is the single dispatch predicate; adding a spelling
    // means adding one entry here, not editing an if-chain.

    constexpr std::array<std::string_view, 6> kThresholdKeyAliases = { "level", "minlevel", "threshold", "loglevel", "minimumlevel", "minloglevel" };

    constexpr std::array<std::string_view, 5> kColorSettingKeyAliases = { "color", "colors", "ansi", "colored", "coloroutput" };

    constexpr std::array<std::string_view, 7> kFileKeyAliases = { "file", "logfile", "logfilepath", "filepath", "path", "logpath", "fileoutput" };

    constexpr std::array<std::string_view, 3> kSourceKeyAliases = { "source", "logsource", "sourcename" };

    constexpr std::array<std::string_view, 8> kLevelsKeyAliases = { "levels", "categories", "loglevels", "logcategories", "leveldefinitions", "leveldefs", "leveldef", "logleveldefinitions" };

    constexpr std::array<std::string_view, 6> kLevelValueFieldAliases = { "value", "level", "num", "number", "numeric", "priority" };

    constexpr std::array<std::string_view, 5> kColorFieldAliases = { "color", "colour", "ansi", "escape", "code" };

    // Level-list env vars in precedence order (later wins).
    constexpr std::array<std::string_view, 4> kLevelListEnvVars = { "LOGAMUFFIN_LEVELS", "LOGAMUFFIN_CATEGORIES", "LOG_CATEGORIES", "LOG_LEVELS" };

    template<std::size_t N>
    bool matches_any(std::string_view normalizedKey, const std::array<std::string_view, N>& aliases)
    {
      for (const auto alias: aliases)
      {
        if (normalizedKey == alias)
        {
          return true;
        }
      }
      return false;
    }

    // Level value/color pair supplied by JSON or NAME=VALUE:COLOR entries.
    struct LevelFields
    {
      std::optional<int> value;
      std::optional<std::string> color;
    };

    // Threshold from a config file: numeric wins, else name resolved after env
    // load.
    struct ThresholdSpec
    {
      std::optional<std::string> name;
      std::optional<int> number;
    };

    // ---------------------------------------------------------------------------
    // Level entry parsing ("NAME=VALUE:COLOR" and friends)
    // ---------------------------------------------------------------------------
    // Grammar: NAME (=|:) [VALUE[:COLOR] | COLOR | VALUE]. Value forms are
    // tried before color forms so numeric strings bind to value, not color.

    // Splits NAME from the rest of an entry at the first separator: when
    // both '=' and ':' are present, the earlier one wins. When the name
    // carries no separator, the rest is empty.
    void split_entry_name_rest(const std::string& entry, std::string& outName, std::string& outRest)
    {
      const std::size_t eqPos    = entry.find('=');
      const std::size_t colonPos = entry.find(':');
      const std::size_t sep      = (eqPos != std::string::npos && (colonPos == std::string::npos || eqPos < colonPos)) ? eqPos : colonPos;
      if (sep == std::string::npos)
      {
        outName = entry;
        outRest = "";
        return;
      }
      outName = entry.substr(0, sep);
      outRest = trim(entry.substr(sep + 1));
    }

    // Resolves a color in a level entry, prefixing the error with context.
    bool resolve_entry_color(const std::string& entry, const std::string& colorPart, std::string& outEscape, std::string& err)
    {
      std::string colorErr;
      if (!resolve_color(colorPart, outEscape, colorErr))
      {
        err = "invalid log level entry '" + entry + "': " + colorErr;
        return false;
      }
      return true;
    }

    // Parses the "VALUE[:COLOR]" / "COLOR" / "VALUE" remainder of a level
    // entry. Value forms are tried before color forms so numeric strings
    // bind to value, not color.
    bool parse_level_fields(const std::string& entry, const std::string& restPart, LevelFields& outFields, std::string& err)
    {
      // VALUE:COLOR form — a second colon separates value from color.
      if (const std::size_t secondColon = restPart.find(':'); secondColon != std::string::npos)
      {
        const std::string valuePart = trim(restPart.substr(0, secondColon));
        const std::string colorPart = trim(restPart.substr(secondColon + 1));
        int value                   = 0;
        if (!parse_int_strict(valuePart, value) || colorPart.empty())
        {
          err = "invalid log level entry '" + entry + "' (expected NAME=VALUE:COLOR)";
          return false;
        }
        std::string escape;
        if (!resolve_entry_color(entry, colorPart, escape, err))
        {
          return false;
        }
        outFields.value = value;
        outFields.color = std::move(escape);
        return true;
      }
      // Single trailing token: numeric → value, otherwise → color.
      if (int value = 0; parse_int_strict(restPart, value))
      {
        outFields.value = value;
        return true;
      }
      std::string escape;
      if (!resolve_entry_color(entry, restPart, escape, err))
      {
        return false;
      }
      outFields.color = std::move(escape);
      return true;
    }

    bool parse_level_entry(const std::string& rawEntry, std::string& outName, LevelFields& outFields, std::string& err)
    {
      const std::string entry = trim(rawEntry);
      if (entry.empty())
      {
        err = "empty log level entry";
        return false;
      }
      std::string name;
      std::string restPart;
      split_entry_name_rest(entry, name, restPart);
      const std::string nameTrim = trim(name);
      outName                    = uppercase(nameTrim);
      if (!valid_level_name(outName))
      {
        err = "invalid log level name '" + nameTrim + "'";
        return false;
      }
      outFields = LevelFields{};
      if (restPart.empty())
      {
        return true;
      }
      return parse_level_fields(entry, restPart, outFields, err);
    }

    // ---------------------------------------------------------------------------
    // JSON scalar helpers
    // ---------------------------------------------------------------------------

    bool json_to_bool(const JsonValue& value, bool& out, std::string& err)
    {
      if (value.type == JsonValue::Type::Bool)
      {
        out = value.boolValue;
        return true;
      }
      if (value.type == JsonValue::Type::Number)
      {
        out = (value.numberValue != 0.0);
        return true;
      }
      if (value.type == JsonValue::Type::String)
      {
        if (parse_bool_string(value.stringValue, out))
        {
          return true;
        }
      }
      err = "expected a boolean (true/false)";
      return false;
    }

    bool json_to_color(const JsonValue& value, std::string& outEscape, std::string& err)
    {
      if (value.type == JsonValue::Type::String)
      {
        return resolve_color(value.stringValue, outEscape, err);
      }
      if (value.type == JsonValue::Type::Number)
      {
        int code = 0;
        if (!json_number_to_int(value, code) || code < 0 || code > kMaxColorCode)
        {
          err = "invalid color code (expected 0-999)";
          return false;
        }
        outEscape = "\x1b[" + std::to_string(code) + "m";
        return true;
      }
      err = "expected a color string";
      return false;
    }

    bool apply_level_object_fields(const JsonValue& objectValue, LevelFields& fields, std::string& err)
    {
      fields = LevelFields{};
      for (const auto& kv: objectValue.objectValue)
      {
        const std::string key = normalize_key(kv.first);
        if (matches_any(key, kLevelValueFieldAliases))
        {
          int intValue = 0;
          if (!json_scalar_to_int(kv.second, intValue))
          {
            err = "level value must be an integer";
            return false;
          }
          fields.value = intValue;
        }
        else if (matches_any(key, kColorFieldAliases))
        {
          std::string escape;
          if (!json_to_color(kv.second, escape, err))
          {
            return false;
          }
          fields.color = escape;
        }
        else
        {
          continue; // ignore "name" and unknown fields for forward compatibility
        }
      }
      return true;
    }

    // ---------------------------------------------------------------------------
    // Levels JSON (object form and array form)
    // ---------------------------------------------------------------------------

    // Applies one named level definition (number, string, or object).
    bool apply_named_level_def(Config& cfg, const std::string& name, const JsonValue& def, std::string& err)
    {
      if (def.type == JsonValue::Type::Number)
      {
        int intValue = 0;
        if (!json_number_to_int(def, intValue))
        {
          err = "level '" + name + "' value must be an integer";
          return false;
        }
        return upsert_level(cfg.levels, name, intValue, std::nullopt, err);
      }
      if (def.type == JsonValue::Type::String)
      {
        if (int intValue = 0; parse_int_strict(def.stringValue, intValue))
        {
          return upsert_level(cfg.levels, name, intValue, std::nullopt, err);
        }
        std::string escape;
        if (!resolve_color(def.stringValue, escape, err))
        {
          err = "level '" + name + "': " + err;
          return false;
        }
        return upsert_level(cfg.levels, name, std::nullopt, escape, err);
      }
      if (def.type == JsonValue::Type::Object)
      {
        LevelFields fields;
        if (!apply_level_object_fields(def, fields, err))
        {
          err = "level '" + name + "': " + err;
          return false;
        }
        return upsert_level(cfg.levels, name, fields.value, fields.color, err);
      }
      err = "level '" + name +
            "' must be a number, color string, or object with "
            "\"value\"/\"color\"";
      return false;
    }

    bool apply_levels_object(Config& cfg, const JsonValue& value, std::string& err)
    {
      for (const auto& kv: value.objectValue)
      {
        const std::string name = uppercase(trim(kv.first));
        if (!valid_level_name(name))
        {
          err = "invalid log level name '" + trim(kv.first) + "'";
          return false;
        }
        if (!apply_named_level_def(cfg, name, kv.second, err))
        {
          return false;
        }
      }
      return true;
    }

    // Applies one array string entry ("NAME=VALUE:COLOR").
    bool apply_level_entry_string(Config& cfg, const JsonValue& element, std::string& err)
    {
      std::string name;
      LevelFields fields;
      std::string entryErr;
      if (!parse_level_entry(element.stringValue, name, fields, entryErr))
      {
        err = entryErr;
        return false;
      }
      return upsert_level(cfg.levels, name, fields.value, fields.color, err);
    }

    // Applies one array object entry ({"name": ..., "value": ..., "color": ...}).
    bool apply_level_entry_object(Config& cfg, const JsonValue& element, std::string& err)
    {
      const JsonValue* nameValue = nullptr;
      for (const auto& kv: element.objectValue)
      {
        if (normalize_key(kv.first) == "name")
        {
          nameValue = &kv.second;
          break;
        }
      }
      if (!nameValue || nameValue->type != JsonValue::Type::String)
      {
        err = "level object is missing a string \"name\"";
        return false;
      }
      const std::string name = uppercase(trim(nameValue->stringValue));
      if (!valid_level_name(name))
      {
        err = "invalid log level name '" + trim(nameValue->stringValue) + "'";
        return false;
      }
      LevelFields fields;
      if (!apply_level_object_fields(element, fields, err))
      {
        return false;
      }
      return upsert_level(cfg.levels, name, fields.value, fields.color, err);
    }

    bool apply_levels_array(Config& cfg, const JsonValue& value, std::string& err)
    {
      for (const auto& element: value.arrayValue)
      {
        if (element.type == JsonValue::Type::String)
        {
          if (!apply_level_entry_string(cfg, element, err))
          {
            return false;
          }
        }
        else if (element.type == JsonValue::Type::Object)
        {
          if (!apply_level_entry_object(cfg, element, err))
          {
            return false;
          }
        }
        else
        {
          err = "levels array entries must be strings or objects "
                "with \"name\"/\"value\"/\"color\"";
          return false;
        }
      }
      return true;
    }

    bool apply_levels_json(Config& cfg, const JsonValue& value, std::string& err)
    {
      if (value.type == JsonValue::Type::Object)
      {
        return apply_levels_object(cfg, value, err);
      }
      if (value.type == JsonValue::Type::Array)
      {
        return apply_levels_array(cfg, value, err);
      }
      err = "\"levels\" must be an object or array";
      return false;
    }

    // ---------------------------------------------------------------------------
    // Config file JSON (top-level keys)
    // ---------------------------------------------------------------------------

    // Threshold key: numeric value wins, else name resolved after env load.
    bool apply_threshold_key(const JsonValue& value, ThresholdSpec& pending, std::string& err)
    {
      if (value.type == JsonValue::Type::Number)
      {
        int intValue = 0;
        if (!json_number_to_int(value, intValue))
        {
          err = "\"level\" must be an integer or a level name";
          return false;
        }
        pending.number = intValue;
        pending.name.reset();
        return true;
      }
      if (value.type == JsonValue::Type::String)
      {
        if (int intValue = 0; parse_int_strict(value.stringValue, intValue))
        {
          pending.number = intValue;
          pending.name.reset();
        }
        else
        {
          pending.name = uppercase(trim(value.stringValue));
          pending.number.reset();
        }
        return true;
      }
      err = "\"level\" must be an integer or a level name";
      return false;
    }

    bool apply_file_json(Config& cfg, const JsonValue& root, ThresholdSpec& pending, std::string& err)
    {
      if (!root.isObject())
      {
        err = "root must be a JSON object";
        return false;
      }
      pending = ThresholdSpec{};
      for (const auto& kv: root.objectValue)
      {
        const std::string key  = normalize_key(kv.first);
        const JsonValue& value = kv.second;
        if (matches_any(key, kThresholdKeyAliases))
        {
          if (!apply_threshold_key(value, pending, err))
          {
            return false;
          }
        }
        else if (matches_any(key, kColorSettingKeyAliases))
        {
          bool colorValue = false;
          if (!json_to_bool(value, colorValue, err))
          {
            err = "\"color\": " + err;
            return false;
          }
          cfg.colorEnabled = colorValue;
        }
        else if (matches_any(key, kFileKeyAliases))
        {
          if (value.type != JsonValue::Type::String || trim(value.stringValue).empty())
          {
            err = "\"file\" must be a non-empty path string";
            return false;
          }
          cfg.logFile = value.stringValue;
        }
        else if (matches_any(key, kSourceKeyAliases))
        {
          if (value.type != JsonValue::Type::String || trim(value.stringValue).empty())
          {
            err = "\"source\" must be a non-empty string";
            return false;
          }
          cfg.source = value.stringValue;
        }
        else if (matches_any(key, kLevelsKeyAliases))
        {
          if (!apply_levels_json(cfg, value, err))
          {
            return false;
          }
        }
        else
        {
          continue; // unknown top-level keys ignored
        }
      }
      return true;
    }

    // ---------------------------------------------------------------------------
    // Environment overrides (level lists + per-category vars)
    // ---------------------------------------------------------------------------

    std::vector<std::string> split_level_list(const std::string& raw)
    {
      std::vector<std::string> entries;
      std::string current;
      for (char c: raw)
      {
        if (c == ',' || c == ';')
        {
          entries.push_back(current);
          current.clear();
        }
        else
        {
          current += c;
        }
      }
      entries.push_back(current);
      return entries;
    }

    // Returns the "levels" member of a top-level JSON object (any of the
    // accepted key spellings), or nullptr when the root is not such an
    // object.
    const JsonValue* levels_member(const JsonValue& root)
    {
      if (!root.isObject())
      {
        return nullptr;
      }
      for (const auto& kv: root.objectValue)
      {
        if (matches_any(normalize_key(kv.first), kLevelsKeyAliases))
        {
          return &kv.second;
        }
      }
      return nullptr;
    }

    // Applies an env var that holds a JSON document: a top-level object
    // with a "levels" member applies that member; any other JSON value is
    // treated as the levels document itself.
    bool apply_levels_json_env(Config& cfg, std::string_view envName, const std::string& trimmedRaw, std::string& err)
    {
      const std::string prefix = "Invalid " + std::string(envName) + ": ";
      JsonParser parser(trimmedRaw);
      JsonValue root;
      JsonParseError parseErr;
      if (!parser.parse(root, parseErr))
      {
        err = prefix + parseErr.message + " at " + json_location(trimmedRaw, parseErr.pos);
        return false;
      }
      const JsonValue* levels = levels_member(root);
      if (!apply_levels_json(cfg, levels ? *levels : root, err))
      {
        err = prefix + err;
        return false;
      }
      return true;
    }

    // Applies one env var that holds either a comma list or a JSON doc.
    bool apply_single_level_list_env(Config& cfg, std::string_view envName, const char* raw, std::string& err)
    {
      const std::string trimmedRaw = trim(raw);
      // A JSON object/array is accepted as an alternative to the
      // comma-separated NAME=VALUE:COLOR list.
      if (!trimmedRaw.empty() && (trimmedRaw.front() == '{' || trimmedRaw.front() == '['))
      {
        return apply_levels_json_env(cfg, envName, trimmedRaw, err);
      }
      for (const auto& entryRaw: split_level_list(raw))
      {
        const std::string entry = trim(entryRaw);
        if (entry.empty())
        {
          continue;
        }
        std::string name;
        LevelFields fields;
        std::string entryErr;
        if (!parse_level_entry(entry, name, fields, entryErr))
        {
          err = "Invalid " + std::string(envName) + ": " + entryErr;
          return false;
        }
        if (!upsert_level(cfg.levels, name, fields.value, fields.color, err))
        {
          err = "Invalid " + std::string(envName) + ": " + err;
          return false;
        }
      }
      return true;
    }

    bool apply_env_level_lists(Config& cfg, std::string& err)
    {
      for (const auto envName: kLevelListEnvVars)
      {
        const std::string envNameStr(envName);
        const char* raw = std::getenv(envNameStr.c_str());
        if (!raw || !*raw)
        {
          continue;
        }
        if (!apply_single_level_list_env(cfg, envName, raw, err))
        {
          return false;
        }
      }
      return true;
    }

    // "NOTICE" -> "NOTICE"; every non-alphanumeric character becomes '_'
    // (the env-var suffix form).
    std::string per_category_suffix(const std::string& name)
    {
      std::string suffix;
      for (char c: name)
      {
        suffix += ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '_';
      }
      return suffix;
    }

    // Returns the first set variable from `vars`, or nullptr when none is
    // set. Per-category override uses the first spelling that is set.
    const std::string* first_set_env_var(const std::array<std::string, 2>& vars)
    {
      for (const auto& var: vars)
      {
        if (std::getenv(var.c_str()))
        {
          return &var;
        }
      }
      return nullptr;
    }

    // Applies LOG_LEVEL_VALUE_<NAME> / LOG_LEVEL_<NAME>_VALUE when set.
    // Returns false only on a malformed value; unset variables are ignored.
    bool apply_per_category_value(Config& cfg, const std::string& name, const std::string& suffix, std::string& err)
    {
      const std::array<std::string, 2> vars = { "LOG_LEVEL_VALUE_" + suffix, "LOG_LEVEL_" + suffix + "_VALUE" };
      const std::string* var                = first_set_env_var(vars);
      if (!var)
      {
        return true;
      }
      const char* raw = std::getenv(var->c_str());
      int value       = 0;
      if (!raw || !parse_int_strict(raw, value))
      {
        err = "Invalid " + *var + ": '" + (raw ? raw : "") + "' (expected an integer)";
        return false;
      }
      if (!upsert_level(cfg.levels, name, value, std::nullopt, err))
      {
        err = "Invalid " + *var + ": " + err;
        return false;
      }
      return true;
    }

    // Applies LOG_LEVEL_COLOR_<NAME> / LOG_LEVEL_<NAME>_COLOR when set.
    // Returns false only on a malformed value; unset variables are ignored.
    bool apply_per_category_color(Config& cfg, const std::string& name, const std::string& suffix, std::string& err)
    {
      const std::array<std::string, 2> vars = { "LOG_LEVEL_COLOR_" + suffix, "LOG_LEVEL_" + suffix + "_COLOR" };
      const std::string* var                = first_set_env_var(vars);
      if (!var)
      {
        return true;
      }
      const char* raw = std::getenv(var->c_str());
      std::string escape;
      std::string colorErr;
      if (!raw || !resolve_color(raw, escape, colorErr))
      {
        err = "Invalid " + *var + ": '" + (raw ? raw : "") + "' (" + colorErr + ")";
        return false;
      }
      if (!upsert_level(cfg.levels, name, std::nullopt, escape, err))
      {
        err = "Invalid " + *var + ": " + err;
        return false;
      }
      return true;
    }

    // Per-category overrides: LOG_LEVEL_VALUE_<NAME> / LOG_LEVEL_COLOR_<NAME>
    // (and the LOG_LEVEL_<NAME>_VALUE / _COLOR spellings).
    bool apply_env_per_category_overrides(Config& cfg, std::string& err)
    {
      // Collect names first: upserting mutates cfg.levels while we iterate.
      std::vector<std::string> names;
      for (const auto& level: cfg.levels)
      {
        names.push_back(level.name);
      }
      for (const auto& name: names)
      {
        const std::string suffix = per_category_suffix(name);
        if (!apply_per_category_value(cfg, name, suffix, err))
        {
          return false;
        }
        if (!apply_per_category_color(cfg, name, suffix, err))
        {
          return false;
        }
      }
      return true;
    }

    bool apply_env_levels(Config& cfg, std::string& err)
    {
      if (!apply_env_level_lists(cfg, err))
      {
        return false;
      }
      if (!apply_env_per_category_overrides(cfg, err))
      {
        return false;
      }
      sort_levels(cfg.levels);
      return true;
    }

    // ---------------------------------------------------------------------------
    // Threshold + scalar resolution (env wins over file over defaults)
    // ---------------------------------------------------------------------------

    // Sets *target to the named env var when set and non-empty.
    void apply_env_string(std::optional<std::string>& target, const char* envName)
    {
      if (const char* value = std::getenv(envName))
      {
        if (*value)
        {
          target = std::string(value);
        }
      }
    }

    bool resolve_threshold_and_scalars(Config& cfg, const ThresholdSpec& pending, std::string& err)
    {
      apply_env_string(cfg.logFile, "LOG_FILE");
      apply_env_string(cfg.source, "LOG_SOURCE");
      if (const char* envColor = std::getenv("LOG_COLOR"))
      {
        const std::string v = uppercase(trim(envColor));
        cfg.colorEnabled    = is_truthy_token(v);
      }
      if (const char* envLevel = std::getenv("LOG_LEVEL"))
      {
        const std::string trimmed = trim(envLevel);
        int numeric               = 0;
        if (parse_int_strict(trimmed, numeric))
        {
          cfg.thresholdValue = numeric;
          return true;
        }
        const std::string name = uppercase(trimmed);
        if (name == "ALWAYS")
        {
          err = std::string("Invalid LOG_LEVEL: ") + envLevel;
          return false;
        }
        const LogLevel* found = find_level_with_alias(cfg.levels, name);
        if (!found)
        {
          err = std::string("Invalid LOG_LEVEL: ") + envLevel;
          return false;
        }
        cfg.thresholdValue = found->value;
        return true;
      }
      if (pending.number.has_value())
      {
        cfg.thresholdValue = *pending.number;
        return true;
      }
      if (pending.name.has_value())
      {
        const std::string& name = *pending.name;
        if (name == "ALWAYS")
        {
          err = "Invalid config level: ALWAYS cannot be used as the "
                "minimum level";
          return false;
        }
        const LogLevel* found = find_level_with_alias(cfg.levels, name);
        if (!found)
        {
          err = "Invalid config level: unknown level '" + name + "'";
          return false;
        }
        cfg.thresholdValue = found->value;
        return true;
      }
      if (const LogLevel* info = find_level_exact(cfg.levels, "INFO"))
      {
        cfg.thresholdValue = info->value;
      }
      return true;
    }

    // ---------------------------------------------------------------------------
    // Config file IO
    // ---------------------------------------------------------------------------

    bool read_config_file(const std::string& path, std::string& content, std::string& err)
    {
      std::error_code ec;
      const auto size = std::filesystem::file_size(path, ec);
      if (ec)
      {
        // file_size fails when the file is missing; report as open failure.
        err = "cannot open config file '" + path + "'";
        return false;
      }
      if (size > kMaxConfigFileSizeBytes)
      {
        err = "config file '" + path + "' is too large (max 4 MiB)";
        return false;
      }
      std::ifstream in(path, std::ios::binary);
      if (!in)
      {
        err = "cannot open config file '" + path + "'";
        return false;
      }
      std::ostringstream stream;
      stream << in.rdbuf();
      if (in.bad())
      {
        err = "failed to read config file '" + path + "'";
        return false;
      }
      content = stream.str();
      return true;
    }

    // Resolves the winning config path: explicit --config, then
    // LOG_CONFIG/LOGAMUFFIN_CONFIG, then ./.logamuffin autodiscovery.
    std::optional<std::string> resolve_config_path(const std::optional<std::string>& cliConfigPath)
    {
      if (cliConfigPath.has_value())
      {
        return *cliConfigPath;
      }
      for (const char* envName: { "LOG_CONFIG", "LOGAMUFFIN_CONFIG" })
      {
        if (const char* envConfig = std::getenv(envName))
        {
          if (*envConfig)
          {
            return std::string(envConfig);
          }
        }
      }
      std::error_code ec;
      if (std::filesystem::exists(".logamuffin", ec))
      {
        return std::string(".logamuffin");
      }
      return std::nullopt;
    }

    // Reads and applies the JSON config file at path, if present.
    bool apply_config_file(Config& cfg, const std::string& path, ThresholdSpec& pending, std::string& err)
    {
      std::string content;
      if (!read_config_file(path, content, err))
      {
        return false;
      }
      JsonParser parser(content);
      JsonValue root;
      JsonParseError parseErr;
      if (!parser.parse(root, parseErr))
      {
        err = "invalid config file '" + path + "': " + parseErr.message + " at " + json_location(content, parseErr.pos);
        return false;
      }
      if (!apply_file_json(cfg, root, pending, err))
      {
        err = "invalid config file '" + path + "': " + err;
        return false;
      }
      sort_levels(cfg.levels);
      return true;
    }

  } // namespace

  bool load_config(const std::optional<std::string>& cliConfigPath, Config& cfg, std::string& err)
  {
    cfg.levels         = default_levels();
    cfg.thresholdValue = 20;
    cfg.colorEnabled.reset();
    cfg.logFile.reset();
    cfg.source.reset();

    const std::optional<std::string> configPath = resolve_config_path(cliConfigPath);

    ThresholdSpec pending;

    if (configPath.has_value())
    {
      if (!apply_config_file(cfg, *configPath, pending, err))
      {
        return false;
      }
    }

    if (!apply_env_levels(cfg, err))
    {
      return false;
    }

    if (!resolve_threshold_and_scalars(cfg, pending, err))
    {
      return false;
    }

    sort_levels(cfg.levels);
    return true;
  }

} // namespace logamuffin
