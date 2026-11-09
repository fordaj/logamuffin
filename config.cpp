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

namespace logamuffin {

namespace {

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

constexpr std::array<std::string_view, 6> kThresholdKeyAliases = {
    "level", "minlevel", "threshold",
    "loglevel", "minimumlevel", "minloglevel"};

constexpr std::array<std::string_view, 5> kColorSettingKeyAliases = {
    "color", "colors", "ansi", "colored", "coloroutput"};

constexpr std::array<std::string_view, 7> kFileKeyAliases = {
    "file", "logfile", "logfilepath", "filepath",
    "path", "logpath", "fileoutput"};

constexpr std::array<std::string_view, 3> kSourceKeyAliases = {
    "source", "logsource", "sourcename"};

constexpr std::array<std::string_view, 8> kLevelsKeyAliases = {
    "levels", "categories", "loglevels", "logcategories",
    "leveldefinitions", "leveldefs", "leveldef", "logleveldefinitions"};

// Levels key as accepted inside a JSON document passed via env
// (a subset of kLevelsKeyAliases; kept separate to preserve behavior).
constexpr std::array<std::string_view, 4> kEnvLevelsKeyAliases = {
    "levels", "categories", "loglevels", "logcategories"};

constexpr std::array<std::string_view, 6> kLevelValueFieldAliases = {
    "value", "level", "num", "number", "numeric", "priority"};

constexpr std::array<std::string_view, 5> kColorFieldAliases = {
    "color", "colour", "ansi", "escape", "code"};

// Level-list env vars in precedence order (later wins).
constexpr std::array<std::string_view, 4> kLevelListEnvVars = {
    "LOGAMUFFIN_LEVELS", "LOGAMUFFIN_CATEGORIES",
    "LOG_CATEGORIES", "LOG_LEVELS"};

template <std::size_t N>
bool matches_any(std::string_view normalizedKey,
                 const std::array<std::string_view, N>& aliases) {
    for (const auto alias : aliases)
        if (normalizedKey == alias)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Level entry parsing ("NAME=VALUE:COLOR" and friends)
// ---------------------------------------------------------------------------
// Grammar: NAME (=|:) [VALUE[:COLOR] | COLOR | VALUE]. Value forms are
// tried before color forms so numeric strings bind to value, not color.

bool parse_level_entry(const std::string& rawEntry, std::string& outName,
                       std::optional<int>& outValue,
                       std::optional<std::string>& outColor,
                       std::string& err) {
    const std::string entry = trim(rawEntry);
    if (entry.empty()) {
        err = "empty log level entry";
        return false;
    }
    // Split name from rest at the first separator that precedes the other.
    const std::size_t eqPos = entry.find('=');
    const std::size_t colonPos = entry.find(':');
    std::string namePart;
    std::string restPart;
    if (eqPos != std::string::npos &&
        (colonPos == std::string::npos || eqPos < colonPos)) {
        namePart = entry.substr(0, eqPos);
        restPart = trim(entry.substr(eqPos + 1));
    } else if (colonPos != std::string::npos) {
        namePart = entry.substr(0, colonPos);
        restPart = trim(entry.substr(colonPos + 1));
    } else {
        namePart = entry;
        restPart = "";
    }
    outName = uppercase(trim(namePart));
    if (!valid_level_name(outName)) {
        err = "invalid log level name '" + trim(namePart) + "'";
        return false;
    }
    outValue.reset();
    outColor.reset();
    if (restPart.empty())
        return true;
    // VALUE:COLOR form — a second colon separates value from color.
    if (const std::size_t secondColon = restPart.find(':');
        secondColon != std::string::npos) {
        const std::string valuePart = trim(restPart.substr(0, secondColon));
        const std::string colorPart = trim(restPart.substr(secondColon + 1));
        int value = 0;
        if (!parse_int_strict(valuePart, value) || colorPart.empty()) {
            err = "invalid log level entry '" + entry +
                  "' (expected NAME=VALUE:COLOR)";
            return false;
        }
        std::string escape;
        std::string colorErr;
        if (!resolve_color(colorPart, escape, colorErr)) {
            err = "invalid log level entry '" + entry + "': " + colorErr;
            return false;
        }
        outValue = value;
        outColor = escape;
        return true;
    }
    // Single trailing token: numeric → value, otherwise → color.
    if (int value = 0; parse_int_strict(restPart, value)) {
        outValue = value;
        return true;
    }
    std::string escape;
    std::string colorErr;
    if (!resolve_color(restPart, escape, colorErr)) {
        err = "invalid log level entry '" + entry + "': " + colorErr;
        return false;
    }
    outColor = escape;
    return true;
}

// ---------------------------------------------------------------------------
// JSON scalar helpers
// ---------------------------------------------------------------------------

bool json_to_bool(const JsonValue& value, bool& out, std::string& err) {
    if (value.type == JsonValue::Type::Bool) {
        out = value.boolValue;
        return true;
    }
    if (value.type == JsonValue::Type::Number) {
        out = (value.numberValue != 0.0);
        return true;
    }
    if (value.type == JsonValue::Type::String) {
        if (parse_bool_string(value.stringValue, out))
            return true;
    }
    err = "expected a boolean (true/false)";
    return false;
}

bool json_to_color(const JsonValue& value, std::string& outEscape,
                   std::string& err) {
    if (value.type == JsonValue::Type::String)
        return resolve_color(value.stringValue, outEscape, err);
    if (value.type == JsonValue::Type::Number) {
        int code = 0;
        if (!json_number_to_int(value, code) || code < 0 || code > kMaxColorCode) {
            err = "invalid color code (expected 0-999)";
            return false;
        }
        outEscape = "\x1b[" + std::to_string(code) + "m";
        return true;
    }
    err = "expected a color string";
    return false;
}

bool apply_level_object_fields(const JsonValue& objectValue,
                               std::optional<int>& value,
                               std::optional<std::string>& colorEscape,
                               std::string& err) {
    value.reset();
    colorEscape.reset();
    for (const auto& kv : objectValue.objectValue) {
        const std::string key = normalize_key(kv.first);
        if (matches_any(key, kLevelValueFieldAliases)) {
            int intValue = 0;
            if (!json_scalar_to_int(kv.second, intValue)) {
                err = "level value must be an integer";
                return false;
            }
            value = intValue;
        } else if (matches_any(key, kColorFieldAliases)) {
            std::string escape;
            if (!json_to_color(kv.second, escape, err))
                return false;
            colorEscape = escape;
        } else if (key == "name") {
            continue; // name comes from the object key / "name" field
        } else {
            continue; // ignore unknown fields for forward compatibility
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Levels JSON (object form and array form)
// ---------------------------------------------------------------------------

// Applies one named level definition (number, string, or object).
bool apply_named_level_def(Config& cfg, const std::string& name,
                           const JsonValue& def, std::string& err) {
    if (def.type == JsonValue::Type::Number) {
        int intValue = 0;
        if (!json_number_to_int(def, intValue)) {
            err = "level '" + name + "' value must be an integer";
            return false;
        }
        return upsert_level(cfg.levels, name, intValue, std::nullopt, err);
    }
    if (def.type == JsonValue::Type::String) {
        if (int intValue = 0; parse_int_strict(def.stringValue, intValue)) {
            return upsert_level(cfg.levels, name, intValue, std::nullopt, err);
        }
        std::string escape;
        if (!resolve_color(def.stringValue, escape, err)) {
            err = "level '" + name + "': " + err;
            return false;
        }
        return upsert_level(cfg.levels, name, std::nullopt, escape, err);
    }
    if (def.type == JsonValue::Type::Object) {
        std::optional<int> intValue;
        std::optional<std::string> escape;
        if (!apply_level_object_fields(def, intValue, escape, err)) {
            err = "level '" + name + "': " + err;
            return false;
        }
        return upsert_level(cfg.levels, name, intValue, escape, err);
    }
    err = "level '" + name +
          "' must be a number, color string, or object with "
          "\"value\"/\"color\"";
    return false;
}

bool apply_levels_object(Config& cfg, const JsonValue& value,
                         std::string& err) {
    for (const auto& kv : value.objectValue) {
        const std::string name = uppercase(trim(kv.first));
        if (!valid_level_name(name)) {
            err = "invalid log level name '" + trim(kv.first) + "'";
            return false;
        }
        if (!apply_named_level_def(cfg, name, kv.second, err))
            return false;
    }
    return true;
}

// Applies one array string entry ("NAME=VALUE:COLOR").
bool apply_level_entry_string(Config& cfg, const JsonValue& element,
                              std::string& err) {
    std::string name;
    std::optional<int> entryValue;
    std::optional<std::string> entryColor;
    std::string entryErr;
    if (!parse_level_entry(element.stringValue, name, entryValue,
                           entryColor, entryErr)) {
        err = entryErr;
        return false;
    }
    return upsert_level(cfg.levels, name, entryValue, entryColor, err);
}

// Applies one array object entry ({"name": ..., "value": ..., "color": ...}).
bool apply_level_entry_object(Config& cfg, const JsonValue& element,
                              std::string& err) {
    const JsonValue* nameValue = nullptr;
    for (const auto& kv : element.objectValue) {
        if (normalize_key(kv.first) == "name") {
            nameValue = &kv.second;
            break;
        }
    }
    if (!nameValue || nameValue->type != JsonValue::Type::String) {
        err = "level object is missing a string \"name\"";
        return false;
    }
    const std::string name =
        uppercase(trim(nameValue->stringValue));
    if (!valid_level_name(name)) {
        err = "invalid log level name '" +
              trim(nameValue->stringValue) + "'";
        return false;
    }
    std::optional<int> intValue;
    std::optional<std::string> escape;
    if (!apply_level_object_fields(element, intValue, escape, err))
        return false;
    return upsert_level(cfg.levels, name, intValue, escape, err);
}

bool apply_levels_array(Config& cfg, const JsonValue& value,
                        std::string& err) {
    for (const auto& element : value.arrayValue) {
        if (element.type == JsonValue::Type::String) {
            if (!apply_level_entry_string(cfg, element, err))
                return false;
        } else if (element.type == JsonValue::Type::Object) {
            if (!apply_level_entry_object(cfg, element, err))
                return false;
        } else {
            err = "levels array entries must be strings or objects "
                  "with \"name\"/\"value\"/\"color\"";
            return false;
        }
    }
    return true;
}

bool apply_levels_json(Config& cfg, const JsonValue& value,
                       std::string& err) {
    if (value.type == JsonValue::Type::Object)
        return apply_levels_object(cfg, value, err);
    if (value.type == JsonValue::Type::Array)
        return apply_levels_array(cfg, value, err);
    err = "\"levels\" must be an object or array";
    return false;
}

// ---------------------------------------------------------------------------
// Config file JSON (top-level keys)
// ---------------------------------------------------------------------------

// Threshold key: numeric value wins, else name resolved after env load.
bool apply_threshold_key(const JsonValue& value,
                         std::optional<std::string>& pendingThresholdName,
                         std::optional<int>& pendingThresholdNumber,
                         std::string& err) {
    if (value.type == JsonValue::Type::Number) {
        int intValue = 0;
        if (!json_number_to_int(value, intValue)) {
            err = "\"level\" must be an integer or a level name";
            return false;
        }
        pendingThresholdNumber = intValue;
        pendingThresholdName.reset();
        return true;
    }
    if (value.type == JsonValue::Type::String) {
        if (int intValue = 0;
            parse_int_strict(value.stringValue, intValue)) {
            pendingThresholdNumber = intValue;
            pendingThresholdName.reset();
        } else {
            pendingThresholdName =
                uppercase(trim(value.stringValue));
            pendingThresholdNumber.reset();
        }
        return true;
    }
    err = "\"level\" must be an integer or a level name";
    return false;
}

bool apply_color_key(Config& cfg, const JsonValue& value, std::string& err) {
    bool colorValue = false;
    if (!json_to_bool(value, colorValue, err)) {
        err = "\"color\": " + err;
        return false;
    }
    cfg.colorEnabled = colorValue;
    return true;
}

bool apply_file_key(Config& cfg, const JsonValue& value, std::string& err) {
    if (value.type != JsonValue::Type::String ||
        trim(value.stringValue).empty()) {
        err = "\"file\" must be a non-empty path string";
        return false;
    }
    cfg.logFile = value.stringValue;
    return true;
}

bool apply_source_key(Config& cfg, const JsonValue& value, std::string& err) {
    if (value.type != JsonValue::Type::String ||
        trim(value.stringValue).empty()) {
        err = "\"source\" must be a non-empty string";
        return false;
    }
    cfg.source = value.stringValue;
    return true;
}

bool apply_file_json(Config& cfg, const JsonValue& root,
                     std::optional<std::string>& pendingThresholdName,
                     std::optional<int>& pendingThresholdNumber,
                     std::string& err) {
    if (!root.isObject()) {
        err = "root must be a JSON object";
        return false;
    }
    pendingThresholdName.reset();
    pendingThresholdNumber.reset();
    for (const auto& kv : root.objectValue) {
        const std::string key = normalize_key(kv.first);
        const JsonValue& value = kv.second;
        if (matches_any(key, kThresholdKeyAliases)) {
            if (!apply_threshold_key(value, pendingThresholdName,
                                     pendingThresholdNumber, err))
                return false;
        } else if (matches_any(key, kColorSettingKeyAliases)) {
            if (!apply_color_key(cfg, value, err))
                return false;
        } else if (matches_any(key, kFileKeyAliases)) {
            if (!apply_file_key(cfg, value, err))
                return false;
        } else if (matches_any(key, kSourceKeyAliases)) {
            if (!apply_source_key(cfg, value, err))
                return false;
        } else if (matches_any(key, kLevelsKeyAliases)) {
            if (!apply_levels_json(cfg, value, err))
                return false;
        } else {
            continue; // unknown top-level keys ignored
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Environment overrides (level lists + per-category vars)
// ---------------------------------------------------------------------------

std::vector<std::string> split_level_list(const std::string& raw) {
    std::vector<std::string> entries;
    std::string current;
    for (char c : raw) {
        if (c == ',' || c == ';') {
            entries.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    entries.push_back(current);
    return entries;
}

// Applies one env var that holds either a comma list or a JSON doc.
bool apply_single_level_list_env(Config& cfg, std::string_view envName,
                                 const char* raw, std::string& err) {
    const std::string trimmedRaw = trim(raw);
    // A JSON object/array is accepted as an alternative to the
    // comma-separated NAME=VALUE:COLOR list.
    if (!trimmedRaw.empty() &&
        (trimmedRaw.front() == '{' || trimmedRaw.front() == '[')) {
        JsonParser parser(trimmedRaw);
        JsonValue root;
        JsonParseError parseErr;
        if (!parser.parse(root, parseErr)) {
            err = std::string("Invalid ") + std::string(envName) + ": " +
                  parseErr.message + " at " +
                  json_location(trimmedRaw, parseErr.pos);
            return false;
        }
        const JsonValue* levels = nullptr;
        if (root.isObject()) {
            for (const auto& kv : root.objectValue) {
                if (matches_any(normalize_key(kv.first),
                                kEnvLevelsKeyAliases)) {
                    levels = &kv.second;
                    break;
                }
            }
            if (levels) {
                if (!apply_levels_json(cfg, *levels, err)) {
                    err = std::string("Invalid ") + std::string(envName) +
                          ": " + err;
                    return false;
                }
                return true;
            }
        }
        if (!apply_levels_json(cfg, root, err)) {
            err = std::string("Invalid ") + std::string(envName) + ": " + err;
            return false;
        }
        return true;
    }
    for (const auto& entryRaw : split_level_list(raw)) {
        const std::string entry = trim(entryRaw);
        if (entry.empty())
            continue;
        std::string name;
        std::optional<int> value;
        std::optional<std::string> colorEscape;
        std::string entryErr;
        if (!parse_level_entry(entry, name, value, colorEscape, entryErr)) {
            err = std::string("Invalid ") + std::string(envName) + ": " +
                  entryErr;
            return false;
        }
        if (!upsert_level(cfg.levels, name, value, colorEscape, err)) {
            err = std::string("Invalid ") + std::string(envName) + ": " + err;
            return false;
        }
    }
    return true;
}

bool apply_env_level_lists(Config& cfg, std::string& err) {
    for (const auto envName : kLevelListEnvVars) {
        const std::string envNameStr(envName);
        const char* raw = std::getenv(envNameStr.c_str());
        if (!raw || !*raw)
            continue;
        if (!apply_single_level_list_env(cfg, envName, raw, err))
            return false;
    }
    return true;
}

// Per-category overrides: LOG_LEVEL_VALUE_<NAME> / LOG_LEVEL_COLOR_<NAME>
// (and the LOG_LEVEL_<NAME>_VALUE / _COLOR spellings).
bool apply_env_per_category_overrides(Config& cfg, std::string& err) {
    std::vector<std::string> names;
    for (const auto& level : cfg.levels)
        names.push_back(level.name);
    for (const auto& name : names) {
        std::string suffix;
        for (char c : name)
            suffix += ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                          ? c : '_';
        const std::string valueVars[] = {
            "LOG_LEVEL_VALUE_" + suffix,
            "LOG_LEVEL_" + suffix + "_VALUE",
        };
        const std::string colorVars[] = {
            "LOG_LEVEL_COLOR_" + suffix,
            "LOG_LEVEL_" + suffix + "_COLOR",
        };
        for (const auto& var : valueVars) {
            const char* raw = std::getenv(var.c_str());
            if (!raw)
                continue;
            int value = 0;
            if (!parse_int_strict(raw, value)) {
                err = "Invalid " + var + ": '" + std::string(raw) +
                      "' (expected an integer)";
                return false;
            }
            if (!upsert_level(cfg.levels, name, value, std::nullopt, err)) {
                err = "Invalid " + var + ": " + err;
                return false;
            }
            break;
        }
        for (const auto& var : colorVars) {
            const char* raw = std::getenv(var.c_str());
            if (!raw)
                continue;
            std::string escape;
            std::string colorErr;
            if (!resolve_color(raw, escape, colorErr)) {
                err = "Invalid " + var + ": '" + std::string(raw) +
                      "' (" + colorErr + ")";
                return false;
            }
            if (!upsert_level(cfg.levels, name, std::nullopt, escape, err)) {
                err = "Invalid " + var + ": " + err;
                return false;
            }
            break;
        }
    }
    return true;
}

bool apply_env_levels(Config& cfg, std::string& err) {
    if (!apply_env_level_lists(cfg, err))
        return false;
    if (!apply_env_per_category_overrides(cfg, err))
        return false;
    sort_levels(cfg.levels);
    return true;
}

// ---------------------------------------------------------------------------
// Threshold + scalar resolution (env wins over file over defaults)
// ---------------------------------------------------------------------------

bool resolve_threshold_and_scalars(
    Config& cfg,
    const std::optional<std::string>& pendingThresholdName,
    const std::optional<int>& pendingThresholdNumber,
    std::string& err) {
    if (const char* envFile = std::getenv("LOG_FILE")) {
        if (*envFile)
            cfg.logFile = std::string(envFile);
    }
    if (const char* envSource = std::getenv("LOG_SOURCE")) {
        if (*envSource)
            cfg.source = std::string(envSource);
    }
    if (const char* envColor = std::getenv("LOG_COLOR")) {
        const std::string v = uppercase(trim(envColor));
        cfg.colorEnabled = is_truthy_token(v);
    }
    if (const char* envLevel = std::getenv("LOG_LEVEL")) {
        const std::string trimmed = trim(envLevel);
        int numeric = 0;
        if (parse_int_strict(trimmed, numeric)) {
            cfg.thresholdValue = numeric;
            return true;
        }
        const std::string name = uppercase(trimmed);
        if (name == "ALWAYS") {
            err = std::string("Invalid LOG_LEVEL: ") + envLevel;
            return false;
        }
        const LogLevel* found =
            find_level_with_alias(cfg.levels, name);
        if (!found) {
            err = std::string("Invalid LOG_LEVEL: ") + envLevel;
            return false;
        }
        cfg.thresholdValue = found->value;
        return true;
    }
    if (pendingThresholdNumber.has_value()) {
        cfg.thresholdValue = *pendingThresholdNumber;
        return true;
    }
    if (pendingThresholdName.has_value()) {
        const std::string& name = *pendingThresholdName;
        if (name == "ALWAYS") {
            err = "Invalid config level: ALWAYS cannot be used as the "
                  "minimum level";
            return false;
        }
        const LogLevel* found = find_level_with_alias(cfg.levels, name);
        if (!found) {
            err = "Invalid config level: unknown level '" + name + "'";
            return false;
        }
        cfg.thresholdValue = found->value;
        return true;
    }
    if (const LogLevel* info = find_level_exact(cfg.levels, "INFO"))
        cfg.thresholdValue = info->value;
    return true;
}

// ---------------------------------------------------------------------------
// Config file IO
// ---------------------------------------------------------------------------

bool read_config_file(const std::string& path, std::string& content,
                      std::string& err) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        // file_size fails when the file is missing; report as open failure.
        err = "cannot open config file '" + path + "'";
        return false;
    }
    if (size > kMaxConfigFileSizeBytes) {
        err = "config file '" + path + "' is too large (max 4 MiB)";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open config file '" + path + "'";
        return false;
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    if (in.bad()) {
        err = "failed to read config file '" + path + "'";
        return false;
    }
    content = stream.str();
    return true;
}

// Resolves the winning config path: explicit --config, then
// LOG_CONFIG/LOGAMUFFIN_CONFIG, then ./.logamuffin autodiscovery.
std::optional<std::string> resolve_config_path(
    const std::optional<std::string>& cliConfigPath) {
    if (cliConfigPath.has_value())
        return *cliConfigPath;
    if (const char* envConfig = std::getenv("LOG_CONFIG")) {
        if (*envConfig)
            return std::string(envConfig);
    }
    if (const char* envConfig = std::getenv("LOGAMUFFIN_CONFIG")) {
        if (*envConfig)
            return std::string(envConfig);
    }
    std::error_code ec;
    if (std::filesystem::exists(".logamuffin", ec))
        return std::string(".logamuffin");
    return std::nullopt;
}

// Reads and applies the JSON config file at path, if present.
bool apply_config_file(Config& cfg, const std::string& path,
                       std::optional<std::string>& pendingThresholdName,
                       std::optional<int>& pendingThresholdNumber,
                       std::string& err) {
    std::string content;
    if (!read_config_file(path, content, err))
        return false;
    JsonParser parser(content);
    JsonValue root;
    JsonParseError parseErr;
    if (!parser.parse(root, parseErr)) {
        err = "invalid config file '" + path + "': " +
              parseErr.message + " at " +
              json_location(content, parseErr.pos);
        return false;
    }
    if (!apply_file_json(cfg, root, pendingThresholdName,
                         pendingThresholdNumber, err)) {
        err = "invalid config file '" + path + "': " + err;
        return false;
    }
    sort_levels(cfg.levels);
    return true;
}

} // namespace

bool load_config(const std::optional<std::string>& cliConfigPath,
                 Config& cfg, std::string& err) {
    cfg.levels = default_levels();
    cfg.thresholdValue = 20;
    cfg.colorEnabled.reset();
    cfg.logFile.reset();
    cfg.source.reset();

    const std::optional<std::string> configPath =
        resolve_config_path(cliConfigPath);

    std::optional<std::string> pendingThresholdName;
    std::optional<int> pendingThresholdNumber;

    if (configPath.has_value()) {
        if (!apply_config_file(cfg, *configPath, pendingThresholdName,
                               pendingThresholdNumber, err))
            return false;
    }

    if (!apply_env_levels(cfg, err))
        return false;

    if (!resolve_threshold_and_scalars(cfg, pendingThresholdName,
                                       pendingThresholdNumber, err))
        return false;

    sort_levels(cfg.levels);
    return true;
}

} // namespace logamuffin
