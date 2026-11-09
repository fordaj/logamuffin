#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// "NAME=VALUE:COLOR" | "NAME=VALUE" | "NAME=COLOR" | "NAME:VALUE[:COLOR]" ...
bool parse_level_entry(const std::string& rawEntry, std::string& outName,
                       std::optional<int>& outValue,
                       std::optional<std::string>& outColor,
                       std::string& err) {
    const std::string entry = trim(rawEntry);
    if (entry.empty()) {
        err = "empty log level entry";
        return false;
    }
    const std::size_t eq = entry.find('=');
    const std::size_t colon = entry.find(':');
    std::string namePart;
    std::string rest;
    if (eq != std::string::npos && (colon == std::string::npos || eq < colon)) {
        namePart = entry.substr(0, eq);
        rest = trim(entry.substr(eq + 1));
    } else if (colon != std::string::npos) {
        namePart = entry.substr(0, colon);
        rest = trim(entry.substr(colon + 1));
    } else {
        namePart = entry;
        rest = "";
    }
    outName = uppercase(trim(namePart));
    if (!valid_level_name(outName)) {
        err = "invalid log level name '" + trim(namePart) + "'";
        return false;
    }
    outValue.reset();
    outColor.reset();
    if (rest.empty())
        return true;
    const std::size_t colon2 = rest.find(':');
    if (colon2 != std::string::npos) {
        const std::string left = trim(rest.substr(0, colon2));
        const std::string right = trim(rest.substr(colon2 + 1));
        int value = 0;
        if (!parse_int_strict(left, value) || right.empty()) {
            err = "invalid log level entry '" + entry +
                  "' (expected NAME=VALUE:COLOR)";
            return false;
        }
        std::string escape;
        std::string colorErr;
        if (!resolve_color(right, escape, colorErr)) {
            err = "invalid log level entry '" + entry + "': " + colorErr;
            return false;
        }
        outValue = value;
        outColor = escape;
        return true;
    }
    int value = 0;
    if (parse_int_strict(rest, value)) {
        outValue = value;
        return true;
    }
    std::string escape;
    std::string colorErr;
    if (!resolve_color(rest, escape, colorErr)) {
        err = "invalid log level entry '" + entry + "': " + colorErr;
        return false;
    }
    outColor = escape;
    return true;
}

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
        if (key == "value" || key == "level" || key == "num" ||
            key == "number" || key == "numeric" || key == "priority") {
            int intValue = 0;
            if (!json_scalar_to_int(kv.second, intValue)) {
                err = "level value must be an integer";
                return false;
            }
            value = intValue;
        } else if (key == "color" || key == "colour" || key == "ansi" ||
                   key == "escape" || key == "code") {
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

bool apply_levels_json(Config& cfg, const JsonValue& value,
                       std::string& err) {
    if (value.type == JsonValue::Type::Object) {
        for (const auto& kv : value.objectValue) {
            const std::string name = uppercase(trim(kv.first));
            if (!valid_level_name(name)) {
                err = "invalid log level name '" + trim(kv.first) + "'";
                return false;
            }
            const JsonValue& def = kv.second;
            if (def.type == JsonValue::Type::Number) {
                int intValue = 0;
                if (!json_number_to_int(def, intValue)) {
                    err = "level '" + name +
                          "' value must be an integer";
                    return false;
                }
                if (!upsert_level(cfg.levels, name, intValue,
                                  std::nullopt, err))
                    return false;
            } else if (def.type == JsonValue::Type::String) {
                int intValue = 0;
                if (parse_int_strict(def.stringValue, intValue)) {
                    if (!upsert_level(cfg.levels, name, intValue,
                                      std::nullopt, err))
                        return false;
                } else {
                    std::string escape;
                    if (!resolve_color(def.stringValue, escape, err)) {
                        err = "level '" + name + "': " + err;
                        return false;
                    }
                    if (!upsert_level(cfg.levels, name, std::nullopt,
                                      escape, err))
                        return false;
                }
            } else if (def.type == JsonValue::Type::Object) {
                std::optional<int> intValue;
                std::optional<std::string> escape;
                if (!apply_level_object_fields(def, intValue, escape, err)) {
                    err = "level '" + name + "': " + err;
                    return false;
                }
                if (!upsert_level(cfg.levels, name, intValue, escape, err))
                    return false;
            } else {
                err = "level '" + name +
                      "' must be a number, color string, or object with "
                      "\"value\"/\"color\"";
                return false;
            }
        }
        return true;
    }
    if (value.type == JsonValue::Type::Array) {
        for (const auto& element : value.arrayValue) {
            if (element.type == JsonValue::Type::String) {
                std::string name;
                std::optional<int> entryValue;
                std::optional<std::string> entryColor;
                std::string entryErr;
                if (!parse_level_entry(element.stringValue, name,
                                       entryValue, entryColor, entryErr)) {
                    err = entryErr;
                    return false;
                }
                if (!upsert_level(cfg.levels, name, entryValue,
                                  entryColor, err))
                    return false;
            } else if (element.type == JsonValue::Type::Object) {
                const JsonValue* nameValue = nullptr;
                for (const auto& kv : element.objectValue) {
                    if (normalize_key(kv.first) == "name") {
                        nameValue = &kv.second;
                        break;
                    }
                }
                if (!nameValue ||
                    nameValue->type != JsonValue::Type::String) {
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
                if (!apply_level_object_fields(element, intValue, escape,
                                               err))
                    return false;
                if (!upsert_level(cfg.levels, name, intValue, escape, err))
                    return false;
            } else {
                err = "levels array entries must be strings or objects "
                      "with \"name\"/\"value\"/\"color\"";
                return false;
            }
        }
        return true;
    }
    err = "\"levels\" must be an object or array";
    return false;
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
        if (key == "level" || key == "minlevel" || key == "threshold" ||
            key == "loglevel" || key == "minimumlevel" ||
            key == "minloglevel") {
            if (value.type == JsonValue::Type::Number) {
                int intValue = 0;
                if (!json_number_to_int(value, intValue)) {
                    err = "\"level\" must be an integer or a level name";
                    return false;
                }
                pendingThresholdNumber = intValue;
                pendingThresholdName.reset();
            } else if (value.type == JsonValue::Type::String) {
                int intValue = 0;
                if (parse_int_strict(value.stringValue, intValue)) {
                    pendingThresholdNumber = intValue;
                    pendingThresholdName.reset();
                } else {
                    pendingThresholdName =
                        uppercase(trim(value.stringValue));
                    pendingThresholdNumber.reset();
                }
            } else {
                err = "\"level\" must be an integer or a level name";
                return false;
            }
        } else if (key == "color" || key == "colors" || key == "ansi" ||
                   key == "colored" || key == "coloroutput") {
            bool colorValue = false;
            if (!json_to_bool(value, colorValue, err)) {
                err = "\"color\": " + err;
                return false;
            }
            cfg.colorEnabled = colorValue;
        } else if (key == "file" || key == "logfile" ||
                   key == "logfilepath" || key == "filepath" ||
                   key == "path" || key == "logpath" ||
                   key == "fileoutput") {
            if (value.type != JsonValue::Type::String ||
                trim(value.stringValue).empty()) {
                err = "\"file\" must be a non-empty path string";
                return false;
            }
            cfg.logFile = value.stringValue;
        } else if (key == "source" || key == "logsource" ||
                   key == "sourcename") {
            if (value.type != JsonValue::Type::String ||
                trim(value.stringValue).empty()) {
                err = "\"source\" must be a non-empty string";
                return false;
            }
            cfg.source = value.stringValue;
        } else if (key == "levels" || key == "categories" ||
                   key == "loglevels" || key == "logcategories" ||
                   key == "leveldefinitions" || key == "leveldefs" ||
                   key == "leveldef" || key == "logleveldefinitions") {
            if (!apply_levels_json(cfg, value, err))
                return false;
        } else {
            continue; // unknown top-level keys ignored
        }
    }
    return true;
}

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

bool apply_env_levels(Config& cfg, std::string& err) {
    // LOGAMUFFIN_* aliases, then LOG_CATEGORIES, then LOG_LEVELS wins.
    for (const char* envName : {"LOGAMUFFIN_LEVELS",
                                "LOGAMUFFIN_CATEGORIES",
                                "LOG_CATEGORIES", "LOG_LEVELS"}) {
        const char* raw = std::getenv(envName);
        if (!raw || !*raw)
            continue;
        const std::string trimmedRaw = trim(raw);
        // A JSON object/array is accepted as an alternative to the
        // comma-separated NAME=VALUE:COLOR list.
        if (!trimmedRaw.empty() &&
            (trimmedRaw.front() == '{' || trimmedRaw.front() == '[')) {
            JsonParser parser(trimmedRaw);
            JsonValue root;
            JsonParseError parseErr;
            if (!parser.parse(root, parseErr)) {
                err = std::string("Invalid ") + envName + ": " +
                      parseErr.message + " at " +
                      json_location(trimmedRaw, parseErr.pos);
                return false;
            }
            if (root.isObject()) {
                const JsonValue* levels = nullptr;
                for (const auto& kv : root.objectValue) {
                    const std::string key = normalize_key(kv.first);
                    if (key == "levels" || key == "categories" ||
                        key == "loglevels" || key == "logcategories") {
                        levels = &kv.second;
                        break;
                    }
                }
                if (levels) {
                    if (!apply_levels_json(cfg, *levels, err)) {
                        err = std::string("Invalid ") + envName + ": " + err;
                        return false;
                    }
                    continue;
                }
            }
            if (!apply_levels_json(cfg, root, err)) {
                err = std::string("Invalid ") + envName + ": " + err;
                return false;
            }
            continue;
        }
        for (const auto& entryRaw : split_level_list(raw)) {
            const std::string entry = trim(entryRaw);
            if (entry.empty())
                continue;
            std::string name;
            std::optional<int> value;
            std::optional<std::string> colorEscape;
            std::string entryErr;
            if (!parse_level_entry(entry, name, value, colorEscape,
                                   entryErr)) {
                err = std::string("Invalid ") + envName + ": " + entryErr;
                return false;
            }
            if (!upsert_level(cfg.levels, name, value, colorEscape, err)) {
                err = std::string("Invalid ") + envName + ": " + err;
                return false;
            }
        }
    }
    // Per-category overrides: LOG_LEVEL_VALUE_<NAME> / LOG_LEVEL_COLOR_<NAME>
    // (and the LOG_LEVEL_<NAME>_VALUE / _COLOR spellings).
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
    sort_levels(cfg.levels);
    return true;
}

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
        cfg.colorEnabled =
            (v == "1" || v == "TRUE" || v == "YES" || v == "ON");
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

} // namespace

bool load_config(const std::optional<std::string>& cliConfigPath,
                 Config& cfg, std::string& err) {
    cfg.levels = default_levels();
    cfg.thresholdValue = 20;
    cfg.colorEnabled.reset();
    cfg.logFile.reset();
    cfg.source.reset();

    std::optional<std::string> configPath;
    if (cliConfigPath.has_value()) {
        configPath = *cliConfigPath;
    } else if (const char* envConfig = std::getenv("LOG_CONFIG")) {
        if (*envConfig)
            configPath = std::string(envConfig);
    } else if (const char* envConfig =
                   std::getenv("LOGAMUFFIN_CONFIG")) {
        if (*envConfig)
            configPath = std::string(envConfig);
    } else {
        std::error_code ec;
        if (std::filesystem::exists(".logamuffin", ec))
            configPath = ".logamuffin";
    }

    std::optional<std::string> pendingThresholdName;
    std::optional<int> pendingThresholdNumber;

    if (configPath.has_value()) {
        std::string content;
        if (!read_config_file(*configPath, content, err))
            return false;
        JsonParser parser(content);
        JsonValue root;
        JsonParseError parseErr;
        if (!parser.parse(root, parseErr)) {
            err = "invalid config file '" + *configPath + "': " +
                  parseErr.message + " at " +
                  json_location(content, parseErr.pos);
            return false;
        }
        if (!apply_file_json(cfg, root, pendingThresholdName,
                             pendingThresholdNumber, err)) {
            err = "invalid config file '" + *configPath + "': " + err;
            return false;
        }
        sort_levels(cfg.levels);
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
