# JSON parser (`json_parser.cpp` + `json_parser.hpp`)

```mermaid
flowchart TD
    Text("<img src='barnacle:braces.svg' width='20' alt=''/><br/>.logamuffin text") --> Bom("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>strip BOM,<br/>allow comments + trailing commas")
    Bom --> RD("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>JsonParser<br/>recursive descent · depth-limited")
    RD -->|malformed| Err("<img src='barnacle:circle-exclamation-point.svg' width='20' alt=''/><br/>JsonParseError<br/>message + byte offset")
    RD -->|valid| Tree("<img src='barnacle:braces.svg' width='20' alt=''/><br/><u>JsonValue tree</u>")
    Tree --> Find("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>JsonValue::find(key)")
    Tree --> Num("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>json_number_to_int()")
    Tree --> Loc("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>json_location()<br/>offset → line:column")
    click Tree href "config.cpp.md"
    class Text,Tree jsonFile
    class Bom,RD,Find,Num,Loc goFile
    class Err errNode
    classDef jsonFile fill:#cdb9f7,stroke:#6d28d9,color:#2e1065
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef errNode fill:#f6b8b8,stroke:#b91c1c,color:#450a0a
```

`json_parser.cpp.md` documents `json_parser.cpp` and `json_parser.hpp`. A minimal, dependency-free JSON reader used for `.logamuffin` config files. Config semantics live in [config.cpp.md](config.cpp.md); this module only produces a `JsonValue` tree.

## Interface

```cpp
struct JsonValue;        // Null/Bool/Number/String/Array/Object variant
struct JsonParseError;   // message + byte offset
class JsonParser {       // explicit JsonParser(std::string text); bool parse(JsonValue&, JsonParseError&); };

std::string json_location(const std::string& text, std::size_t pos);
bool json_number_to_int(const JsonValue& value, int& out);
bool json_scalar_to_int(const JsonValue& value, int& out);
```

`JsonValue::find(key)` looks up an object member (used for every top-level key in the config file). `json_location()` renders an offset as line:column for error messages.

## Supported syntax

Recursive-descent, depth-limited parser supporting objects, arrays, strings (with `\uXXXX` escapes and surrogate pairs), numbers, booleans, null, `//` and `/* */` comments, a leading UTF-8 BOM, and trailing commas — a deliberately forgiving superset of strict JSON for hand-written config files. Malformed input fails with `JsonParseError` (message plus byte offset); duplicate handling and unknown-key policy are the caller's business.

## Where it is used

[config.cpp.md](config.cpp.md) is the only consumer: it parses the selected config file and walks the resulting tree (`levels` as object or array, aliased top-level keys). Nothing else in the binary reads JSON.

## Related pages

- [config.cpp.md](config.cpp.md) — the only caller; maps `JsonValue` onto `Config`
- [text_utils.cpp.md](text_utils.cpp.md) — shares `hex_digit_value()` with the parser
- [logamuffin.cpp.md](logamuffin.cpp.md)
