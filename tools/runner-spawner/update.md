# The runner-spawner bundle (`tools/runner-spawner/`)

[runner-spawner](https://github.com/fordaj/runner-spawner) manages self-hosted CI runners. This repository uses it to manage the runners its GitHub Actions workflow (`.github/workflows/ci.yml`) runs on; runner configuration lives in `.runner-spawner.json` (`fordaj/logamuffin`, runners `runner01`/`runner02`, group `Default`, runner version `2.338.2`, state dir `.runners/`).

## Layout

```text
tools/runner-spawner/
  update.bash                     # refresh from fordaj/runner-spawner releases
  v0.0.1.2.6/
    macos-arm64/runner-spawner
    macos-x86_64/runner-spawner
    linux-arm64/runner-spawner
    linux-x86_64/runner-spawner
    windows-arm64/runner-spawner.exe
    windows-x86_64/runner-spawner.exe
```

Pinned as `RUNNER_SPAWNER_VERSION_DIR=tools/runner-spawner/v0.0.1.2.6` in `scripts/.common.env`; selected via `RUNNER_SPAWNER_BIN` in the platform env files (see [../../scripts/env.md](../../scripts/env.md)). Note: runner-spawner reports unsupported on Linux (macOS + Windows only) — the Linux binaries ship for a uniform layout but `scripts/.linux-*.env` records that caveat.

## `update.bash` in detail

Same shape as the other bundles, with one deliberate difference:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads via `gh`, then verifies the downloaded archives against upstream `checksums.txt`/`SHA256SUMS.txt` (`shasum -a 256 -c` or `sha256sum -c`), looking up each asset by name. **A missing checksums file is fatal** — runner-spawner refuses to install unverified binaries — unlike the other bundles, which only warn. A mismatch is fatal in all cases.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` fallback for legacy `.7z`), copies the binary into `v<tag>/<platform>/`, and makes it executable. Only the binary is kept.
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`runner-spawner --version`).

Flags: `--dry-run`, `--tag TAG`, `--repo REPO`. Environment overrides: `RUNNER_SPAWNER_TOOL_REPO` (default `fordaj/runner-spawner`), `RUNNER_SPAWNER_TAG`.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../../scripts/env.md](../../scripts/env.md) — cascade and pins
- [../../README.md](../../README.md) — "Continuous integration" section
