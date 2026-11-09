#pragma once

// Platform / process helpers: hostname, TTY detection, terminal width,
// and locked file appends.
//
// The only module that touches OS-specific APIs (Windows vs POSIX). Pure
// logic lives elsewhere so this file stays the single cross-platform
// surface.

#include <cstdio>
#include <filesystem>
#include <string>

namespace logamuffin
{

  std::string hostname();
  bool terminal(FILE* stream);
  int terminal_width();

  // Appends entry to path with an exclusive lock so concurrent writers do
  // not interleave. Returns false on open/lock/write/close failure.
  bool append_locked(const std::filesystem::path& path, const std::string& entry);

} // namespace logamuffin
