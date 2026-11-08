# Logamuffin Testing

Status: implemented for #6 (initial suite). This document records the
recommended testing architecture and how the suite maps to the issue
requirements.

## Recommended architecture

Two layers, both runnable with a single command (`./run_tests.sh`):

| Layer | Location | Framework | What it tests |
| ----- | -------- | --------- | ------------- |
| Unit tests | `tests/unit/test_logamuffin.cpp` | doctest v2.5.3 (vendored single header) | Pure helpers in `logamuffin.cpp`: severity parsing, `uppercase`/`basename`, `env_enabled`, `clean_message`, timestamp format, `get_source`, ordering, `append_locked`. Fast, in-process, no subprocess. |
| CLI integration tests | `tests/integration/test_cli.py` | Python standard library `unittest` only | Black-box behavior of the compiled executable: argument/usage/exit codes, aliases, filtering, `LOG_LEVEL`/`LOG_COLOR`/`LOG_FILE`/`LOG_SOURCE`, stdout/stderr routing, file creation/appending/errors, ANSI colors, multiline/multi-arg messages, concurrent writes. |
| Legacy Bash suite | `test.sh` | Bash (no deps) | Preserved for backward compatibility. Its individual checks are reproduced (and extended) by the Python suite. Prefer `./run_tests.sh`; use `bash test.sh` with `LOG_BIN` set if you need the original harness. |

### Why this shape

- **doctest for units.** The issue asks for a lightweight C++ framework
  (preferably doctest). The repo already vendors `tools/doctest/v2.5.3/doctest.h`
  (MIT, single header, no build system, no network fetch), so the suite
  reuses it. Test dependencies remain separate from the runtime binary.
- **Minimal production change.** `logamuffin.cpp` only gains a
  `LOGAMUFFIN_NO_MAIN` guard around `main()` (plus `[[maybe_unused]]` on
  `run()`) so the unit-test translation unit can include the .cpp file
  directly and call its internal helpers. No header split or larger
  refactor was needed, per the "avoid unnecessary architectural changes"
  guideline. The helpers are testable because they already live in a
  single translation unit; the guard makes them reachable from tests
  without altering observable behavior.
- **Python stdlib for integration.** The issue asks to evaluate moving
  CLI tests from Bash to Python for cross-platform execution. Python's
  standard library alone (`unittest`, `subprocess`, `tempfile`,
  `concurrent.futures`) covers everything Bash did and adds native
  support for Windows paths, concurrent workers, and structured
  assertions — with no runtime dependency added for Logamuffin users.
  The suite targets an explicitly specified executable (`LOG_BIN` or
  `--executable`), uses an isolated temporary directory per test, and
  cleans up afterwards, so it runs against dev builds and packaged
  release binaries identically. Individual failures are reported by
  `unittest` without terminating the suite.
- **Two layers are complementary.** Units guard fast, precise regression
  checks; integration guards observable CLI contracts (streams, files,
  exit codes, colors) across platforms. Together they satisfy
  "prefer testing observable behavior over implementation details"
  without duplicating logic.

### Requirement mapping (issue #6)

1. **C++ unit tests** — `tests/unit/test_logamuffin.cpp` covers severity
   parsing (aliases, case-insensitivity, invalid), filtering semantics
   (numeric ordering + `ALWAYS` bypass), configuration (`env_enabled`,
   `get_source`), message formatting (`clean_message`, timestamp), and
   file output (`append_locked`).
2. **CLI integration tests** — `tests/integration/test_cli.py` preserves
   every check from `test.sh` and adds: argument validation
   (`--version` variants, missing args, unknown levels), severity
   aliases as message + threshold, env defaults/overrides/invalid
   (`LOG_LEVEL` incl. `ALWAYS`-as-threshold, `LOG_COLOR` truthy/falsy,
   `LOG_FILE` incl. empty + nested dirs, `LOG_SOURCE`), stdout/stderr
   routing per level, file creation/appending/append-failure
   (`LOG_FILE` as directory, parent-as-file), ANSI on/off, multiline and
   multi-argument joining, `\r` stripping, concurrent writes (30 tokens
   via thread pool), and exit codes (0 / 1 / 2).
3. **Cross-platform** — only the Python stdlib; explicit executable
   selection; `tempfile.TemporaryDirectory` isolation; no Bash-only
   idioms in the primary suite.
4. **Continuous integration** — `.github/workflows/ci.yml` (see below).

## Running the suite

### Complete suite (recommended)

```bash
./run_tests.sh
```

This builds the native executable and unit tests, runs both suites,
and exits non-zero on failure.

Variants:

```bash
BUILD_MODE=debug ./run_tests.sh        # debug flags (-O0 -g3 -DDEBUG)
SANITIZE=1 ./run_tests.sh              # ASan + UBSan
./run_tests.sh --only-unit             # unit tests only
./run_tests.sh --only-integration      # integration tests only
./run_tests.sh --skip-unit             # integration only (older alias)
./run_tests.sh --skip-integration      # unit only
CXX=clang++ ./run_tests.sh             # choose compiler
PYTHON=python3 ./run_tests.sh          # choose Python
LOG_BIN=./logamuffin ./run_tests.sh --only-integration
```

### Piecewise

```bash
# Unit tests only
mkdir -p build/tests
clang++ -std=c++20 -O2 -I. tests/unit/test_logamuffin.cpp -o build/tests/test_logamuffin
./build/tests/test_logamuffin

# Integration tests only (after building the executable)
clang++ -std=c++20 -O2 logamuffin.cpp -o build/logamuffin
LOG_BIN=./build/logamuffin python3 tests/integration/test_cli.py
python3 tests/integration/test_cli.py --executable ./build/logamuffin -v
```

Pass `-v` for verbose unittest output and a test name to run a single
case (e.g. `... -v test_version_succeeds`).

### Legacy Bash suite

```bash
LOG_BIN=./build/logamuffin bash test.sh
```

## Continuous integration

`.github/workflows/ci.yml` runs `./run_tests.sh` on `push` and
`pull_request`:

- **Matrix:** `ubuntu-latest`, `macos-latest`, `windows-latest` x
  `release`/`debug` (`BUILD_MODE`), via the native compiler
  auto-detected by the runner script (clang++/g++/c++).
- **Sanitizers:** a separate Linux job builds and runs with
  `SANITIZE=1` (ASan + UBSan).
- Tests run against the freshly built native binary on each runner.

If the CI harness changes, keep `.github/workflows/ci.yml` and
`./run_tests.sh` in sync: CI should invoke `run_tests.sh` (with
`BUILD_MODE` / `SANITIZE` as appropriate) rather than duplicating build
logic.

## Maintainer notes

- Keep unit tests in `tests/unit/` and integration tests in
  `tests/integration/`. Add descriptive names and useful failure
  messages; prefer observable CLI behavior in integration tests.
- `build/` is the default output directory (git-ignored). Do not commit
  built binaries.
- If a new severity, env var, or log-file behavior is added, extend both
  the unit helpers tests and the integration coverage (aliases,
  filtering, streams, file, ANSI) and update the requirement table here.
