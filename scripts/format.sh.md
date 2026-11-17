# Formatting in place (`scripts/format.sh`)

```mermaid
flowchart LR
    Fmt("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>format.sh") --> UV("<img src='barnacle:square-terminal.svg' width='20' alt=''/><br/>uv run formattington all")
    UV --> Md("<img src='barnacle:file-text.svg' width='20' alt=''/><br/>prettier<br/>markdown")
    UV --> Cpp("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>clang-format<br/>c · h · cpp · hpp")
    UV --> Sh("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>shfmt -i 4<br/>shell")
    UV --> Scope("<img src='barnacle:braces.svg' width='20' alt=''/><br/>.formattington.json +<br/>.clang-format / .prettierrc")
    Ci("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>ci/format_check.sh — check-only") --> UV
    class Fmt,Sh,Ci shFile
    class Md mdFile
    class Cpp goFile
    class Scope jsonFile
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef mdFile fill:#dcd6ce,stroke:#6b6255,color:#292524
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef jsonFile fill:#cdb9f7,stroke:#6d28d9,color:#2e1065
```

`scripts/format.sh` formats the whole repository with [formattington](https://github.com/fordaj/formattington): Markdown via prettier, C/C++ via clang-format, shell via `shfmt -i 4`, as scoped by `.formattington.json` and styled by `.clang-format`/`.prettierrc`. The vendored header under `tools/doctest/` is skipped (as are `build/` and other generated locations).

## Usage

```bash
./scripts/format.sh              # format everything in place
./scripts/format.sh --series     # format sequentially (ordered output)
./scripts/format.sh --verbose    # show which formatter executables were resolved
```

Extra flags are forwarded to `formattington` before `all`. `--config` is managed by the script and must not be passed. CI uses the check-only counterpart `ci/format_check.sh` (same config, `--check`); see "Formatting" in [README.md](../README.md).

## Prerequisites and pinning

- `uv` (https://docs.astral.sh/uv/) — the script `exec`s `uv run formattington …`; formattington itself is pinned in `pyproject.toml` / `uv.lock` and runs on Python 3.14 (see `.python-version`). `uv run` creates `.venv/` automatically.
- `prettier`, `clang-format`, `shfmt` on `PATH` (formattington exits 127 if one is missing).

To bump formattington, update the commit in `pyproject.toml` and run `uv lock` and `uv sync`.

## Related pages

- [README.md](../README.md) — "Formatting" section
- [AGENTS.md](../AGENTS.md) — repository layout and formatting conventions
