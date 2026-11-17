# Test runner (`scripts/run_tests.sh`)

```mermaid
flowchart TD
    RT("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>run_tests.sh") --> Src("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>sources.sh<br/>UNIT_SOURCES / SOURCES")
    Gen("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>generate_version.sh<br/>-I.version") --> Unit
    Src --> Unit("<img src='barnacle:binary.svg' width='20' alt=''/><br/>unit binary<br/>tests/unit/test_logamuffin.cpp")
    Src --> Dev("<img src='barnacle:binary.svg' width='20' alt=''/><br/><u>fresh dev binary<br/>SOURCES</u>")
    Unit --> UT("<img src='barnacle:check.svg' width='20' alt=''/><br/><u>doctest suite</u>")
    Dev --> IT("<img src='barnacle:file-code.svg' width='20' alt=''/><br/><u>python3<br/>tests/integration/test_cli.py</u>")
    UT --> Sum("<img src='barnacle:clipboard-check.svg' width='20' alt=''/><br/>pass/fail summary<br/>exit 0 only if both pass")
    IT --> Sum
    click UT href "../docs/TESTING.md"
    click IT href "../docs/TESTING.md"
    click Dev href "../logamuffin.cpp.md"
    class RT,Src,Gen shFile
    class Unit,Dev goFile
    class UT checkNode
    class IT goFile
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef checkNode fill:#9fe8c9,stroke:#0f766e,color:#052e22
```

`scripts/run_tests.sh` builds and runs the complete logamuffin test suite — C++ unit tests (doctest) and cross-platform CLI integration tests (Python standard library) — and is the one command CI and contributors use. See [docs/TESTING.md](../docs/TESTING.md) for the testing architecture, coverage matrix, and how to add tests.

## Usage

```bash
./scripts/run_tests.sh                      # full suite (fresh dev build)
./scripts/run_tests.sh --skip-unit          # integration only
./scripts/run_tests.sh --skip-integration   # unit only
./scripts/run_tests.sh --clean              # remove previous test outputs first
./scripts/run_tests.sh --executable ./build/release/macos-arm64/logamuffin  # test a release binary
./scripts/run_tests.sh --verbose            # verbose integration output
```

Exit code is `0` only if both suites pass; individual failures are reported without stopping the suite.

## How it works

1. Sources `tools/sources.sh.md` for the translation-unit list and native toolchain (`clang++`, falling back to `c++`; override with `CXX`).
2. Regenerates `.version/version.hpp` via `tools/generate_version.sh` so the unit binary and dev binary include `version.hpp` with `-I.version`.
3. Builds the unit-test binary from `tests/unit/test_logamuffin.cpp` + `UNIT_SOURCES` against `tools/doctest/v2.5.3/doctest.h` (see `tools/doctest/README.md`), runs it.
4. Builds a fresh native dev binary from `SOURCES` (or uses `--executable`), then runs `tests/integration/test_cli.py` against it. The integration suite can also target any binary directly via `LOG_BIN=/path/to/logamuffin python3 tests/integration/test_cli.py`.

Requirements: a C++20 compiler, Python 3 (stdlib only), Bash — and the vendored doctest header (checked for explicitly).

## Related pages

- [docs/TESTING.md](../docs/TESTING.md) — testing architecture and conventions
- [scripts/test.sh.md](test.sh.md) — deprecated shim forwarding here with `--skip-unit`
- `tools/sources.sh.md` — `SOURCES`/`UNIT_SOURCES` used here
- `tools/generate_version.sh.md` — version header regeneration
- [README.md](../README.md) — "Testing" section
