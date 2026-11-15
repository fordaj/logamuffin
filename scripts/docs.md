# The docs generator (`scripts/docs.sh`)

Generates the documentation site you are reading: it runs the pinned [barnacle](../tools/barnacle/update.md) binary against this repository.

Barnacle scans the working tree for Markdown (`.md`) and Mermaid (`.mmd`) files, renders each as a page, and keeps relative links between them. Generated output is static HTML; it is written to `./site/` by default and is never committed (it is in `.gitignore`).

## Usage

```bash
scripts/docs.sh                       # generate into ./site/
scripts/docs.sh --site ./public       # forward --site to barnacle
scripts/docs.sh --config ./docs/barnacle.json
scripts/docs.sh --help                # usage for this script
```

Any arguments other than `--help`/`-h` are forwarded to barnacle verbatim; see `barnacle --help` for `--config`/`--site` semantics.

## How it locates barnacle

Sources [entry.md](entry.md); `BARNACLE_BIN` comes from the [cascade](env.md) (`tools/barnacle/v<version>/<platform>/barnacle[.exe]`). If the bundle is missing or not executable, the script fails with instructions to run `tools/barnacle/update.sh` (or set `BARNACLE_BIN` explicitly). It then runs barnacle with the repository root as the working directory, so barnacle's auto-discovered `./.barnacle.json` (optional, absent here) and its default `./site/` output land at the repository root regardless of the caller's cwd.

## What the site contains

Every `.md`/`.mmd` in the tree, including this one — see the [root README](../README.md) and [AGENTS.md](../AGENTS.md). `.git`, `node_modules`, `vendor`, `dist`, and the output directory are always excluded by barnacle; hidden files and directories (the cascade's `.common.env` et al.) are skipped by default.

Every documented source file has a sibling `.md` page — see the "Documentation files (barnacle)" section in [../AGENTS.md](../AGENTS.md).

## Related pages

- [../tools/barnacle/update.md](../tools/barnacle/update.md) — the pinned binaries and how to refresh them
- [entry.md](entry.md) · [env.md](env.md)
- [../AGENTS.md](../AGENTS.md)
