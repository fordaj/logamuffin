# Single source for build translation units (sourced by build.sh + run_tests.sh).
# Add a translation unit here only; UNIT_SOURCES is derived as intersection.
# shellcheck shell=bash
SOURCES=(
  logamuffin.cpp
  logamuffin_core.cpp
  text_utils.cpp
  json_parser.cpp
  color.cpp
  log_levels.cpp
  config.cpp
  platform.cpp
)
# Core units needed by the doctest suite (subset of SOURCES).
UNIT_SOURCES=(
  logamuffin_core.cpp
  text_utils.cpp
  log_levels.cpp
)
# Native compiler + flags for dev/test builds (run_tests.sh).
CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    CXX="c++"
fi
CXXFLAGS=(-std=c++20 -O0 -g3 -DDEBUG)
