# The barnacle bundle (`tools/barnacle/`)

[Barnacle](https://github.com/fordaj/barnacle) is a documentation-site generator: it renders the `.md` and `.mmd` files scattered through a repository into a static, navigable HTML site. This repository uses it to render *this* documentation; the site you may be reading was built by it.

## Layout

```text
tools/barnacle/
  update.sh                     # refresh from fordaj/barnacle releases
  v0.0.1.0.5/
    macos-arm64/barnacle
    macos-x86_64/barnacle
    linux-arm64/barnacle
    linux-x86_64/barnacle
    windows-arm64/barnacle.exe
    windows-x86_64/barnacle.exe
```

Pinned as `BARNACLE_VERSION_DIR=tools/barnacle/v0.0.1.0.5` in `scripts/.common.env`; selected via `BARNACLE_BIN` in the platform env files (see [../../scripts/env.md](../../scripts/env.md)).

## `update.sh` in detail

Assets are `barnacle-<platform>.tar.gz` (macOS/Linux) and `barnacle-<platform>.zip` (Windows), each carrying the binary at the archive root or inside a single top-level directory. The script:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads via `gh`, then verifies the downloaded archives against the upstream `checksums.txt`/`SHA256SUMS.txt` (`shasum -a 256 -c` or `sha256sum -c`), looking up each downloaded asset by name. A missing checksums file only warns; a mismatch is fatal.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` as a fallback for legacy `.7z` assets), copies the binary into `v<tag>/<platform>/`, and makes it executable. LICENSE, if present in an archive, is copied alongside the binary into the per-platform directory.
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`barnacle --version`).

Flags: `--dry-run` lists the assets without touching anything; `--tag TAG` pins a release; `--repo REPO` overrides the slug. Environment overrides: `BARNACLE_REPO`, `BARNACLE_TAG`. Only the binary is kept per platform.

## How the repository uses it

[scripts/docs.md](../../scripts/docs.md) resolves `BARNACLE_BIN` through the [env cascade](../../scripts/env.md) and generates `./site/` from the repo root; the output is git-ignored.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../../scripts/docs.md](../../scripts/docs.md) — the docs generator that runs this binary
- [../../scripts/README.md](../../scripts/README.md) — the scripts that consume the bundle
