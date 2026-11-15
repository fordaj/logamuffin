# Repository script tooling (`scripts/`)

The `scripts/` directory carries the repository's build/release/docs tooling: an environment cascade the other scripts source, plus one entry point per task. Everything here is bash; nothing requires a global install beyond what each script documents.

Vernacular used in the headers (`log INFO`, `$REPO_ROOT_DIR`, `VERSIONER_BIN`): read [entry.md](entry.md) first, then [env.md](env.md).

## The cascade (why the dotfiles exist)

- `scripts/entry.sh` — sources `scripts/env.sh` and defines `log()` (logamuffin, with a printf fallback). Sourced by everything else. See [entry.md](entry.md).
- `scripts/env.sh` — locates and sources the cascade below, then anchors the repository-relative `*_BIN` tool paths to absolute paths. See [env.md](env.md).
- `scripts/.common.env`, `scripts/.<os>-<arch>.env` — the cascade proper. Hidden files, so barnacle does not render them as pages; their contract (pinned versions, platform quirks) is documented inside [env.md](env.md) instead. `.common.env` pins `VERSIONER v0.0.3.0.5`, `LOGAMUFFIN v0.1.3.0.35`, `RUNNER_SPAWNER v0.0.1.2.6`, `BARNACLE v0.0.1.0.5` and holds the upstream `fordaj/*` slugs; each platform file sets `PROJECT_PLATFORM` and the `*_BIN` paths for its `<os>-<arch>`.

## Task scripts

- [build.md](build.md) — cross-compile the six release targets (Zig + native clang++) into `build/<mode>/<platform>/`.
- [release.md](release.md) — version, cross-compile, pack archives, tag, and publish the GitHub release.
- [version.md](version.md) — print the repository version computed by versioner (`--format text|shell|json`).
- [docs.md](docs.md) — generate the documentation site you are reading, with the bundled barnacle.
- [entry.md](entry.md) / [env.md](env.md) — the sourced cascade and `log()` helper (not entry points).

`ci/format_check.bash` at `ci/` is the check-only counterpart of the root [format.bash](../format.md) used by CI; `run_tests.sh` at the root is the test entry point ([../run_tests.md](../run_tests.md)).

## Related pages

- [../tools/README.md](../tools/README.md) — the bundles selected by the `*_BIN` variables
- [../AGENTS.md](../AGENTS.md)
- [../README.md](../README.md)
