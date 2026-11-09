#pragma once

// Color resolution for log levels.
//
// Translates user-supplied color specifications (names, hex, SGR codes,
// raw ANSI sequences) into ANSI escape strings. Only resolve_color() is
// public; name tables and escape helpers are file-local in color.cpp.

#include <string>

namespace logamuffin
{

  // Resolves specRaw ("blue", "#ff8800", "31", "[0;31m", "\033[0;31m", ...)
  // into an ANSI escape in outEscape. Returns false with a short message
  // in err when the specification is unknown or malformed.
  bool resolve_color(const std::string& specRaw, std::string& outEscape, std::string& err);

} // namespace logamuffin
