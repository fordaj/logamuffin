# Deprecated test shim (`test.sh`)

`test.sh` is a deprecated shim kept for compatibility. The canonical suite is [run_tests.md](run_tests.md) (`./run_tests.sh`, unit + integration); the Python CLI integration suite supersedes this legacy Bash suite with isolated tmp dirs, per-test reporting, and broader coverage.

## What it does

Prints a deprecation notice to stderr, then `exec`s `run_tests.sh --skip-unit "$@"` — i.e. only the CLI integration suite, with the same exit code.

```bash
./test.sh    # == ./run_tests.sh --skip-unit
```

New work should call `./run_tests.sh` (or `tests/integration/test_cli.py` directly) rather than this wrapper.

## Related pages

- [run_tests.md](run_tests.md) — the canonical entry point
- [docs/TESTING.md](docs/TESTING.md) — testing architecture
- [README.md](README.md)
