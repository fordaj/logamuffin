# Testing Logamuffin

Logamuffin has two test suites that together cover correctness, CLI
behavior, and cross-platform compatibility — without adding any runtime
dependency for Logamuffin users.

| Suite                     | Location                         | Framework                                                      | What it tests                                                                                 |
| ------------------------- | -------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| **C++ unit tests**        | `tests/unit/test_logamuffin.cpp` | [doctest](https://github.com/doctest/doctest) 2.5.3 (vendored) | Severity parsing, filtering, configuration, message formatting, output formatting             |
| **CLI integration tests** | `tests/integration/test_cli.py`  | Python standard library (`unittest`)                           | Arguments, aliases, env vars, stdout/stderr, files, colors, messages, concurrency, exit codes |

There is **one command** to run everything:

```bash
./run_tests.sh
```

## Quick start

### Prerequisites

- A C++ compiler with C++20 support (defaults to `clang++`, falls back to `c++`).
- Python 3 (standard library only — no `pip install` needed).
- Bash (the `run_tests.sh` entry point is POSIX; the integration suite itself is cross-platform).
- `git` and the bundled Versioner binary under `tools/versioner/`
  (`run_tests.sh` regenerates `.version/version.hpp` from git history
  via `tools/generate_version.bash` before compiling; the `.version/`
  directory is git-ignored).

No other dependencies. The doctest header is already vendored at
`tools/doctest/v2.5.3/doctest.h`.

### Run everything

```bash
./run_tests.sh
```

This builds:

1. The unit-test binary: `build/debug/tests/unit/test_logamuffin`
2. A fresh native `logamuffin` binary: `build/debug/logamuffin`
   (compiled directly with the native compiler — not the full
   six-target `build.sh` cross-compile, which needs Zig).

Then it runs both suites and prints a summary. The exit code is `0`
only if both suites pass; individual failures are reported without
stopping the suite.

### Useful variants

```bash
./run_tests.sh --skip-unit          # integration only
./run_tests.sh --skip-integration   # unit only
./run_tests.sh --verbose            # verbose integration output
./run_tests.sh --clean              # drop previous test build outputs first
./run_tests.sh --help

# Test a packaged release binary instead of a fresh dev build:
./run_tests.sh --executable ./build/release/macos-arm64/logamuffin
./run_tests.sh --executable /path/to/logamuffin.exe
```

The integration suite can also be run directly, on any platform:

```bash
# Explicit binary (same as --executable):
python3 tests/integration/test_cli.py --executable build/debug/logamuffin

# Via environment variable (also works with unittest discovery):
LOG_BIN=/path/to/logamuffin python3 tests/integration/test_cli.py
LOG_BIN=/path/to/logamuffin python3 -m unittest tests.integration.test_cli -v

# Windows:
LOG_BIN=logamuffin.exe python tests\integration\test_cli.py
```

If no binary is specified, the integration suite searches (in order):
`build/debug/logamuffin`, `build/release/<platform>/logamuffin`
and `build/debug/<platform>/logamuffin` (where `<platform>` is
`macos-arm64`, `macos-x86_64`, `linux-arm64`, `linux-x86_64`,
`windows-x86_64`, `windows-arm64`), legacy `build/logamuffin` /
`build/native/logamuffin` and repo-root platform directories, then
`PATH`.

## Architecture

### Testable core: `logamuffin.hpp` + `logamuffin_core.cpp`

The executable originally kept all logic in an anonymous namespace
inside `logamuffin.cpp`, which made the pure logic untestable without
subprocess indirection.

The pure helpers live in a compiled core module (`namespace logamuffin`,
declarations in `logamuffin.hpp`, definitions in `logamuffin_core.cpp`):

- `Level`, `LevelInfo`, `levels` and `info()` — severity model.
- `uppercase()`, `basename()` — string helpers.
- `parse_level()` — severity names and aliases (`WARN`/`WARNING`,
  `FATAL`/`CRITICAL`), case-insensitive.
- `env_enabled()` — `LOG_COLOR` truthiness (`1`/`TRUE`/`YES`/`ON`).
- `get_source()` — `LOG_SOURCE` override handling.
- `clean_message()` — strips `\r`, preserves `\n`.
- `should_emit()` — filtering predicate (`ALWAYS` bypasses).
- `join_message()` — multi-argument joining.
- `format_file_entry()` / `format_console_output()` — plain-text file
  lines and console lines (color + terminal-width branches).

`logamuffin.cpp` used to own platform/process concerns (`hostname()`,
TTY detection, terminal width, locked file append, `run()`/`main()`) and
called the core for everything testable. There was **no observable
behavior change**: same messages, exit codes, streams, and file format.

The unit tests `#include "logamuffin.hpp"` directly and link
`logamuffin_core.cpp` + `text_utils.cpp`, so they exercise the exact
code the executable runs.

### Modular executable (issue #8)

The former 1,955-line `logamuffin.cpp` (extensible categories, colors,
JSON/env config, platform I/O, and `run()`/`main()` in one anonymous
namespace) is now split by responsibility into `namespace logamuffin`
modules. Each module has a single cohesive responsibility and a minimal
public header; helpers shared only inside a module stay file-local.

- `logamuffin.cpp` — thin executable entry: help/usage text, config-aware
  source resolution, `run()` (argument/config wiring, filtering,
  console + file output), and `main()`.
- `logamuffin.hpp` / `logamuffin_core.cpp` — testable core (pure
  helpers, declarations + compiled definitions).
- `text_utils.hpp/.cpp` — string helpers (`lowercase`, `trim`,
  `normalize_key`, `parse_int_strict`, `parse_bool_string`,
  truthy/falsy token tables).
- `json_parser.hpp/.cpp` — self-contained JSON parser (`JsonValue`,
  `JsonParser`, location + scalar helpers). No other internal
  dependencies.
- `color.hpp/.cpp` — color resolution (`resolve_color()` is public;
  name tables and escape helpers are file-local).
- `log_levels.hpp/.cpp` — level registry (`LogLevel`, `Config`,
  defaults, lookup/alias, validation, `upsert_level`,
  `is_error_level`).
- `config.hpp/.cpp` — configuration pipeline (`load_config()` is
  public; JSON/env field application and file reading are file-local).
- `platform.hpp/.cpp` — OS surface (`hostname()`, `terminal()`,
  `terminal_width()`, `append_locked()`).

`build.sh` and `run_tests.sh` compile the `SOURCES` list (all eight
translation units, including `logamuffin_core.cpp`). There is **no
observable behavior change**: same messages, exit codes, streams, and
file format. Unit tests need `logamuffin.hpp` plus the core sources.

### Why doctest for unit tests

- Single header, vendored — no Conan/vcpkg/CMake prerequisite for a
  one-file C++ tool.
- Lightweight enough to match the project's philosophy; no mocking
  framework needed for pure helpers.
- Descriptive `TEST_CASE` / `SUBCASE` names and expression
  decomposition give useful failure messages for free.

### Why Python stdlib for integration tests

- **Cross-platform execution** with zero runtime/test dependencies
  for Logamuffin users (test deps stay separate from the shipped
  binary, which remains a standalone executable).
- `unittest` reports every failure without aborting the suite, unlike
  a failing `set -e` Bash chain.
- Tests target an **explicitly specified executable** and run each
  case in an **isolated temporary directory** (`tempfile`, cleaned up
  via `addCleanup`), so they work against both development builds and
  packaged release binaries and never touch the repo state.
- List-argument `subprocess` (no shell) so arguments with spaces,
  newlines, and special characters are passed exactly.

### What is covered where

**Unit tests** (`tests/unit/test_logamuffin.cpp`) test _pure helpers_:
case conversion, path basenames, all level names + aliases +
case-insensitivity, numeric built-in values (`-1`, `5`, `10`, `20`,
`30`, `40`, `50`), invalid-name rejection, numeric ordering,
display names/colors, labels (`WARN`, `CRITICAL`), `\r` cleaning,
`LOG_COLOR` truthiness, `LOG_SOURCE` fallback, filtering truth
table, message joining, file and console formatting (including the
120/180 width thresholds and ANSI branches), and the
`YYYY-MM-DD HH:MM:SS,mmm` timestamp shape.

**Integration tests** (`tests/integration/test_cli.py`, many with
`subTest`s) test _observable behavior_:

- Arguments & validation: `--version`, missing/incomplete args,
  unknown level (lists valid levels and numeric hint), `--version
extra`, lowercase levels, empty message.
- Numeric levels (issue #3): builtin numbers resolve to categories,
  unknown numbers are ad-hoc `[25]` levels, custom categories resolve
  by number, numeric filtering/streams, `-1`/`-- -1`/`+50` handling,
  out-of-range rejection.
- Severity aliases & filtering: every documented level, `WARN` vs
  `WARNING`, `FATAL` vs `CRITICAL`, default `INFO` threshold,
  `LOG_LEVEL` overrides, `ALWAYS` bypass, filtered exit `0`.
- Bash `log` helper (issue #2): word/numeric forwarding, per-call
  `LOG_SOURCE` file/function/line, exit-code preservation, and
  caller-environment respect.
- Environment: invalid `LOG_LEVEL`, `LOG_LEVEL=ALWAYS` rejected,
  case/alias thresholds, default + empty `LOG_FILE` fallback to
  `.logs/log.log`, `LOG_SOURCE` override vs executable-name default.
- Streams: `TRACE`..`WARNING` → stdout, `ERROR`/`CRITICAL` → stderr,
  `ALWAYS` → stdout.
- Files: creation, append order, parent-directory creation, filtered
  does-not-create, plain-text file even with `LOG_COLOR=1`,
  directory-as-`LOG_FILE` warns but console succeeds, `--version`
  writes nothing, file-line format regex.
- Colors: `LOG_COLOR=0` plain, `=1` ANSI, all truthy/falsy variants,
  unset + captured output is plain.
- Messages: multi-argument single-space join, multiline preserved,
  `\r` cleaned, special characters preserved verbatim.
- Concurrency: 10 parallel writers → all 10 lines in the file.
- Exit codes: `0` success/filtered/version, `2` usage/level/env errors.

The legacy Bash suite `test.sh` is **deprecated**: it is now a thin
shim forwarding to `run_tests.sh --skip-unit`. The Python suite
supersedes it — it covers everything `test.sh` did (version, usage,
invalid severity, all severities, `INFO` filtering, `ALWAYS`,
invalid `LOG_LEVEL`, stream routing, multiline) plus the missing
cases above. `run_tests.sh` uses the Python suite.

## CI

CI (GitHub Actions, multi-OS/arch, sanitizers, debug/release matrix)
is **intentionally out of scope for this ticket** and will be
handled as a separate follow-up. Nothing in the test layout blocks
it: `run_tests.sh` is the single entry point a CI job would call,
and the integration suite already accepts an explicit binary for
release verification.

## Adding tests

### Unit tests

Add a `TEST_CASE("descriptive name")` in
`tests/unit/test_logamuffin.cpp` and assert with `CHECK` /
`REQUIRE`. Prefer testing observable helper output (parsed level,
emitted/not-emitted, formatted string) over implementation details.
If a helper needs environment access, wrap it in the existing
`EnvGuard` pattern so variables are restored.

### Integration tests

Add a `test_` method to the appropriate class in
`tests/integration/test_cli.py` (or a new `TestCase` subclass).
Use `self.run_bin([...], KEY=value)` — it strips host `LOG_*`
variables, defaults `LOG_COLOR=0` and an isolated `LOG_FILE`, and
returns `(returncode, stdout, stderr)`. Read the log with
`self.read_log()`. Use `subTest` for parameterized cases. Keep tests
deterministic: never depend on hostname, exact timestamps, or terminal
width; assert with `assertIn` / regex instead.

## Layout

```
logamuffin.hpp                     testable core declarations
logamuffin_core.cpp                testable core definitions
logamuffin.cpp                     executable entry (run/main)
text_utils.hpp/.cpp                string helpers
json_parser.hpp/.cpp               JSON parser
color.hpp/.cpp                     color resolution
log_levels.hpp/.cpp                level registry (LogLevel/Config)
config.hpp/.cpp                    config pipeline (load_config)
platform.hpp/.cpp                  platform/process helpers
run_tests.sh                       one-command entry point
tests/unit/test_logamuffin.cpp     doctest unit tests
tests/integration/test_cli.py      Python stdlib CLI tests
tools/doctest/v2.5.3/doctest.h     vendored doctest
test.sh                            deprecated shim -> run_tests.sh
log.sh                             Bash `log` helper (source for `log`)
docs/TESTING.md                    this file
build/                             ignored outputs: build/debug/,
                                 build/release/<platform>/
```
