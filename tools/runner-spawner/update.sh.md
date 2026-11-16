# The runner-spawner bundle (`tools/runner-spawner/`)

```mermaid
flowchart LR
    Up[update.sh<br/>thin wrapper] --> Core["../update-bundle.sh<br/>shared implementation"]
    Core --> Tag{resolve tag<br/>latest release or --tag}
    Tag --> DL["gh release download"]
    DL --> Sum{checksums.txt<br/>shipped?}
    Sum -->|yes| Verify["verify SHA256"]
    Sum -->|no| Die["fatal —<br/>refusing unverified binaries"]
    Verify --> Stage["stage into v…/platform/<br/>binary only"]
    Stage --> Swap["swap in,<br/>remove superseded"]
    Swap --> Smoke["host binary reports<br/>its version"]
    click Core href "../update-bundle.sh.md"
```

[runner-spawner](https://github.com/fordaj/runner-spawner) manages self-hosted CI runners. This repository uses it to manage the runners its GitHub Actions workflow (`.github/workflows/ci.yml`) runs on; runner configuration lives in `.runner-spawner.json` (`fordaj/logamuffin`, runners `runner01`/`runner02`, group `Default`, runner version `2.338.2`, state dir `.runners/`).

## Layout

```text
tools/runner-spawner/
  update.sh                     # refresh from fordaj/runner-spawner releases (wrapper)
  v0.0.1.2.6/
    macos-arm64/runner-spawner
    macos-x86_64/runner-spawner
    linux-arm64/runner-spawner
    linux-x86_64/runner-spawner
    windows-arm64/runner-spawner.exe
    windows-x86_64/runner-spawner.exe
```

Pinned as `RUNNER_SPAWNER_VERSION_DIR=tools/runner-spawner/v0.0.1.2.6` in `scripts/.common.env`; selected via `RUNNER_SPAWNER_BIN` in the platform env files (see [../../scripts/env.sh.md](../../scripts/env.sh.md)). Note: runner-spawner reports unsupported on Linux (macOS + Windows only) — the Linux binaries ship for a uniform layout but `scripts/.linux-*.env` records that caveat.

## `update.sh` in detail

`update.sh` is a thin wrapper that exports `TOOL_*` configuration and execs [../update-bundle.sh.md](../update-bundle.sh.md), the shared implementation for all bundles. Runner-spawner specifics: no `TOOL_ASSET_SUFFIX`, no extra files, and — deliberately different from the other bundles — `TOOL_CHECKSUMS_REQUIRED=1`, so a missing checksums file is fatal (it refuses to install unverified binaries). The repo-slug env var is `RUNNER_SPAWNER_TOOL_REPO` (via `TOOL_REPO_ENV`); the tag env var is `RUNNER_SPAWNER_TAG`.

The shared script:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads via `gh`, then verifies the downloaded archives against upstream `checksums.txt`/`SHA256SUMS.txt` (`shasum -a 256 -c` or `sha256sum -c`), looking up each asset by name. A missing checksums file is fatal here; a mismatch is fatal in all cases.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` fallback for legacy `.7z`), copies the binary into `v<tag>/<platform>/`, and makes it executable. Only the binary is kept.
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`runner-spawner --version`).

Flags: `--dry-run`, `--tag TAG`, `--repo REPO`. Environment overrides: `RUNNER_SPAWNER_TOOL_REPO` (default `fordaj/runner-spawner`), `RUNNER_SPAWNER_TAG`.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../update-bundle.sh.md](../update-bundle.sh.md) — the shared implementation
- [../../scripts/env.sh.md](../../scripts/env.sh.md) — cascade and pins
- [../../README.md](../../README.md) — "Continuous integration" section
