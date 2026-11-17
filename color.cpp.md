# Color resolution (`color.cpp` + `color.hpp`)

```mermaid
flowchart TD
    Spec("<img src='barnacle:file-input.svg' width='20' alt=''/><br/>specRaw<br/>user-written color") --> Norm("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>normalize<br/>case + separators")
    Norm --> Named{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>named color?"}}
    Named -->|yes| Const("<img src='barnacle:file-code.svg' width='20' alt=''/><br/><u>escape from<br/>the ansi_colors.hpp table</u>")
    Named -->|no| Sgr{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>bare SGR code?"}}
    Sgr -->|yes| Pass("<img src='barnacle:check.svg' width='20' alt=''/><br/>use the code as-is")
    Sgr -->|no| Seq{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>full escape sequence?"}}
    Seq -->|yes| Pass2("<img src='barnacle:check.svg' width='20' alt=''/><br/>validate and use")
    Seq -->|no| Hex{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>hex truecolor?"}}
    Hex -->|yes| RGB("<img src='barnacle:palette.svg' width='20' alt=''/><br/>24-bit truecolor escape")
    Hex -->|no| Err("<img src='barnacle:circle-exclamation-point.svg' width='20' alt=''/><br/>false + err<br/>caller surfaces a config error")
    Const --> Out("<img src='barnacle:monitor.svg' width='20' alt=''/><br/>outEscape")
    Pass --> Out
    Pass2 --> Out
    RGB --> Out
    click Const href "ansi_colors.hpp.md"
    class Norm,Const goFile
    class Named,Sgr,Seq,Hex hexNode
    class Pass,Pass2 checkNode
    class Err errNode
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef checkNode fill:#9fe8c9,stroke:#0f766e,color:#052e22
    classDef errNode fill:#f6b8b8,stroke:#b91c1c,color:#450a0a
```

`color.cpp.md` documents `color.cpp` and `color.hpp`. The pair translates user-supplied color specifications into ANSI escape strings. Only `resolve_color()` is public; the named tables and the per-form escape resolvers are file-local in `color.cpp`. `resolve_color()` walks a fixed precedence list of form resolvers (raw ESC sequence → literal `\033`/`\e`/`\x1b` text → `[0;31m` bracket form → `#rrggbb`/`#rgb` hex → bare SGR code); the first form to claim the spec decides the outcome, and anything unclaimed falls through to the named-color table.

## Interface

```cpp
bool resolve_color(const std::string& specRaw, std::string& outEscape, std::string& err);
```

`specRaw` is resolved case-insensitively, with hyphens, underscores, and spaces treated as separators. Accepted forms:

- Named colors — every entry mirrored from [ansi_colors.hpp](ansi_colors.hpp.md) (`black`…`white`, `reset`, plus `bold-`, `underline-`, `bg-`/`background-`, `hi-`/`bright-`, `bold-hi-`, and `bg-hi-` prefixes), with `gray`/`grey` as aliases for `hi-black` and `silver` for `hi-white`.
- A bare SGR code (`"31"`, `"0;91"`).
- A full escape sequence (`"\033[0;31m"` or `"[0;31m"`).
- Hex truecolor (`"#rrggbb"` or `"#rgb"`).

Returns `false` with a short message in `err` when the specification is unknown or malformed — callers surface this as a config error rather than guessing.

## Where it is used

[config.cpp](config.cpp.md) calls `resolve_color()` while applying `LOG_LEVELS`/`LOG_LEVEL_COLOR_<NAME>` environment overrides and the JSON `levels` object, so every color path in the effective configuration funnels through this one resolver.

## Related pages

- [config.cpp.md](config.cpp.md) — the configuration pipeline that consumes resolved colors
- [ansi_colors.hpp.md](ansi_colors.hpp.md) — the escape constants named colors map onto
- [log_levels.cpp.md](log_levels.cpp.md) — where resolved colors are stored per category
- [logamuffin.cpp.md](logamuffin.cpp.md)
