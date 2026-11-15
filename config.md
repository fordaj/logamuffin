# Configuration pipeline (`config.cpp` + `config.hpp`)

`config.md` documents `config.cpp` and `config.hpp`. The module orchestrates how a [Config](log_levels.md) is built for each invocation: start from defaults, overlay the winning JSON file, overlay environment variables, then resolve scalar thresholds. Level/color/JSON primitives live in their own modules — this one only sequences them.

## Interface

```cpp
bool load_config(const std::optional<std::string>& cliConfigPath, Config& cfg, std::string& err);
```

`cliConfigPath` carries the `--config` value when present; otherwise `LOG_CONFIG`/`LOGAMUFFIN_CONFIG` and then `./.logamuffin` autodiscovery decide which file is read. An explicitly selected but missing or malformed file is an error; a missing autodiscovered file silently keeps defaults; a malformed autodiscovered file is also an error. On failure, returns `false` with a user-facing message in `err`.

## Pipeline order

1. `default_levels()` from [log_levels.md](log_levels.md).
2. The JSON file — top-level keys `level`, `color`, `file`, `source`, `levels` (plus documented aliases), parsed with the [json_parser.md](json_parser.md) module; `--config` beats `LOG_CONFIG`, which beats `./.logamuffin`.
3. Environment overrides — `LOG_LEVELS`/`LOG_CATEGORIES` (and `LOGAMUFFIN_*` equivalents), per-category `LOG_LEVEL_VALUE_<NAME>`/`LOG_LEVEL_COLOR_<NAME>` (both documented spellings), then scalars `LOG_LEVEL`, `LOG_COLOR`, `LOG_FILE`, `LOG_SOURCE`.
4. Threshold resolution — thresholds may name a category or be numeric; named thresholds resolve against the configured values.

Colors pass through [color.md](color.md)'s `resolve_color()`; integers through [text_utils.md](text_utils.md)'s strict parsers.

## Where it is used

`run()` in [logamuffin.md](logamuffin.md) calls `load_config()` once per invocation; a failure prints the message and exits with status 2.

## Related pages

- [log_levels.md](log_levels.md) — the `Config` model this module fills in
- [json_parser.md](json_parser.md) — the JSON reader used for config files
- [color.md](color.md) — color specification resolution
- [text_utils.md](text_utils.md) — shared scalar parsers
- [logamuffin.md](logamuffin.md) — the CLI entry point that calls `load_config()`
- [README.md](README.md) — user-facing configuration reference
