#pragma once

// Configuration loading: JSON file + environment variables.
//
// Orchestrates the configuration pipeline (issue #4): start from
// defaults, apply the JSON file (explicit --config/LOG_CONFIG wins over
// ./.logamuffin autodiscovery), then apply environment overrides, then
// resolve scalar thresholds. Level/color/JSON primitives live in their
// own modules; this module only sequences them.

#include <optional>
#include <string>

#include "log_levels.hpp"

namespace logamuffin
{

  // Loads configuration into cfg. cliConfigPath is the --config value when
  // present; otherwise LOG_CONFIG/LOGAMUFFIN_CONFIG/ ./.logamuffin is
  // probed. Returns false with a user-facing message in err on failure.
  bool load_config(const std::optional<std::string>& cliConfigPath, Config& cfg, std::string& err);

} // namespace logamuffin
