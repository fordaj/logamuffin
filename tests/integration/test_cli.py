#!/usr/bin/env python3
"""Cross-platform CLI integration tests for logamuffin.

Uses only the Python standard library so the same tests run on macOS,
Linux, and Windows against either a development build or a packaged
release binary. Individual test failures are reported without
terminating the whole suite (via unittest), unlike a Bash script with
`set -e`.

How to run:
    # Against the native build produced by ./run_tests.sh
    python3 tests/integration/test_cli.py --executable build/logamuffin

    # Against any explicit binary (e.g. a release archive on PATH)
    LOG_BIN=/path/to/logamuffin python3 tests/integration/test_cli.py
    LOG_BIN=/path/to/logamuffin python3 -m unittest tests.integration.test_cli -v

    # Or via the single entry point
    ./run_tests.sh

Each test runs in an isolated temporary directory (created fresh in
setUp and removed via addCleanup) and explicitly sets LOG_FILE /
LOG_COLOR / LOG_LEVEL / LOG_SOURCE, so host environment variables can
never leak into results.

This suite preserves the coverage of the legacy Bash test.sh and adds
the missing cases from issue #6: argument validation, severity aliases,
environment defaults/overrides/invalid values, stdout/stderr routing,
file creation/appending/error handling, ANSI behavior, multiline and
multi-argument messages, concurrent writes, and exit codes.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def _candidate_executable() -> Path | None:
    env_bin = os.environ.get("LOG_BIN")
    if env_bin:
        # Resolve to absolute so tests that change cwd (isolated temp
        # dirs) can still find the binary.
        return Path(env_bin).expanduser().resolve()

    suffix = ".exe" if sys.platform.startswith("win") else ""
    candidates = [
        REPO_ROOT / "build" / f"logamuffin{suffix}",
        REPO_ROOT / "build" / "native" / f"logamuffin{suffix}",
        REPO_ROOT / "macos-arm64" / f"logamuffin{suffix}",
        REPO_ROOT / "macos-x86_64" / f"logamuffin{suffix}",
        REPO_ROOT / "linux-arm64" / f"logamuffin{suffix}",
        REPO_ROOT / "linux-x86_64" / f"logamuffin{suffix}",
        REPO_ROOT / "windows-x86_64" / f"logamuffin{suffix}",
        REPO_ROOT / "windows-arm64" / f"logamuffin{suffix}",
    ]
    for path in candidates:
        if path.is_file():
            return path.resolve()

    located = shutil.which(f"logamuffin{suffix}") or shutil.which("logamuffin")
    return Path(located).resolve() if located else None


EXE: Path | None = _candidate_executable()

# Matches "<YYYY-MM-DD HH:MM:SS,mmm> <host> [<LEVEL>] <source>: <message>"
FILE_LINE_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3} \S+ \[[A-Z]+\] \S+: .+$"
)


class LogamuffinTestCase(unittest.TestCase):
    """Base class with isolated temp dir and a subprocess runner."""

    def setUp(self) -> None:
        if EXE is None or not EXE.is_file():
            self.fail(
                "logamuffin executable not found. Build it with ./run_tests.sh "
                "or point LOG_BIN / --executable at a binary, e.g. "
                "python3 tests/integration/test_cli.py --executable build/logamuffin"
            )
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.log_file = self.dir / "test.log"
        self.exe = str(EXE)

    def base_env(self, **overrides: str | None) -> dict[str, str]:
        env = {k: v for k, v in os.environ.items() if not k.startswith("LOG_")}
        # Deterministic defaults: plain output + isolated log file.
        env["LOG_FILE"] = str(self.log_file)
        env["LOG_COLOR"] = "0"
        for key, value in overrides.items():
            if value is None:
                env.pop(key, None)
            else:
                env[key] = value
        return env

    def run_bin(
        self,
        args: list[str],
        cwd: Path | str | None = None,
        **env_overrides: str | None,
    ) -> subprocess.CompletedProcess:
        env = self.base_env(**env_overrides)
        return subprocess.run(
            [self.exe, *args],
            capture_output=True,
            text=True,
            timeout=15,
            env=env,
            cwd=str(cwd) if cwd is not None else None,
        )

    def read_log(self) -> str:
        if not self.log_file.is_file():
            return ""
        return self.log_file.read_text(encoding="utf-8", errors="replace")


class TestCLIArguments(LogamuffinTestCase):
    def test_version_reports_version_and_exits_zero(self) -> None:
        result = self.run_bin(["--version"])
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr!r}")
        self.assertIn("logamuffin", result.stdout)
        self.assertRegex(result.stdout, r"\d+\.\d+\.\d+")
        self.assertEqual(result.stderr, "")

    def test_missing_arguments_returns_usage_and_exit_2(self) -> None:
        result = self.run_bin([])
        self.assertEqual(result.returncode, 2)
        self.assertIn("Usage: logamuffin", result.stderr)
        self.assertIn("LEVEL", result.stderr)

    def test_level_without_message_returns_usage_and_exit_2(self) -> None:
        result = self.run_bin(["INFO"])
        self.assertEqual(result.returncode, 2)
        self.assertIn("Usage: logamuffin", result.stderr)

    def test_unknown_level_is_rejected_with_exit_2_and_lists_valid_levels(self) -> None:
        result = self.run_bin(["BOGUS", "message"])
        self.assertEqual(result.returncode, 2)
        self.assertIn("Unknown log level: BOGUS", result.stderr)
        for level in ("TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL", "ALWAYS"):
            self.assertIn(level, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_version_with_extra_arg_is_treated_as_unknown_level(self) -> None:
        result = self.run_bin(["--version", "extra"])
        self.assertEqual(result.returncode, 2)
        self.assertIn("Unknown log level: --version", result.stderr)

    def test_level_names_are_case_insensitive(self) -> None:
        for name in ("info", "Info", "iNfO"):
            with self.subTest(level=name):
                result = self.run_bin([name, f"case-{name}"])
                self.assertEqual(result.returncode, 0)
                self.assertIn(f"case-{name}", result.stdout)
                self.assertIn("[INFO]", result.stdout)

    def test_empty_message_argument_is_accepted(self) -> None:
        result = self.run_bin(["INFO", ""])
        self.assertEqual(result.returncode, 0)
        self.assertIn("[INFO]", result.stdout)
        self.assertIn("[INFO]", self.read_log())


class TestSeverityAliasesAndFiltering(LogamuffinTestCase):
    def test_all_documented_levels_are_accepted(self) -> None:
        for level in ("ALWAYS", "TRACE", "DEBUG", "INFO", "WARN", "WARNING", "ERROR", "CRITICAL", "FATAL"):
            with self.subTest(level=level):
                result = self.run_bin([level, f"lvl-{level}"], LOG_LEVEL="TRACE", LOG_COLOR="0")
                self.assertEqual(result.returncode, 0, f"stderr: {result.stderr!r}")
                self.assertIn(f"lvl-{level}", self.read_log())

    def test_warn_and_warning_aliases_produce_same_label(self) -> None:
        for alias in ("WARN", "WARNING", "warn", "warning"):
            with self.subTest(alias=alias):
                log = self.dir / f"warn-{alias}.log"
                result = self.run_bin(
                    [alias, f"warn-{alias}"],
                    LOG_LEVEL="TRACE", LOG_COLOR="0", LOG_FILE=str(log),
                )
                self.assertEqual(result.returncode, 0)
                content = log.read_text(encoding="utf-8", errors="replace")
                self.assertIn("[WARN]", content)
                self.assertIn(f"warn-{alias}", content)

    def test_fatal_and_critical_aliases_produce_same_label(self) -> None:
        for alias in ("FATAL", "CRITICAL", "fatal", "critical"):
            with self.subTest(alias=alias):
                log = self.dir / f"crit-{alias}.log"
                result = self.run_bin(
                    [alias, f"crit-{alias}"],
                    LOG_LEVEL="TRACE", LOG_COLOR="0", LOG_FILE=str(log),
                )
                self.assertEqual(result.returncode, 0)
                content = log.read_text(encoding="utf-8", errors="replace")
                self.assertIn("[CRITICAL]", content)

    def test_default_threshold_filters_below_info(self) -> None:
        # No LOG_LEVEL override: the default is INFO.
        trace = self.run_bin(["TRACE", "filtered-trace"], LOG_LEVEL=None)
        debug = self.run_bin(["DEBUG", "filtered-debug"], LOG_LEVEL=None)
        info = self.run_bin(["INFO", "kept-info"], LOG_LEVEL=None)
        for result, marker in ((trace, "filtered-trace"), (debug, "filtered-debug")):
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            self.assertNotIn(marker, self.read_log())
        self.assertEqual(info.returncode, 0)
        self.assertIn("kept-info", self.read_log())

    def test_log_level_override_controls_filtering(self) -> None:
        cases = [
            ("DEBUG", "TRACE", False),
            ("DEBUG", "DEBUG", True),
            ("DEBUG", "INFO", True),
            ("WARNING", "INFO", False),
            ("WARNING", "WARNING", True),
            ("CRITICAL", "ERROR", False),
            ("CRITICAL", "CRITICAL", True),
        ]
        for threshold, level, expected_kept in cases:
            with self.subTest(threshold=threshold, level=level):
                log = self.dir / f"filt-{threshold}-{level}.log"
                result = self.run_bin(
                    [level, f"m-{threshold}-{level}"],
                    LOG_LEVEL=threshold, LOG_COLOR="0", LOG_FILE=str(log),
                )
                self.assertEqual(result.returncode, 0)
                content = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
                if expected_kept:
                    self.assertIn(f"m-{threshold}-{level}", content)
                else:
                    self.assertNotIn(f"m-{threshold}-{level}", content)

    def test_always_bypasses_any_threshold(self) -> None:
        log = self.dir / "always.log"
        result = self.run_bin(
            ["ALWAYS", "always-bypass"],
            LOG_LEVEL="CRITICAL", LOG_COLOR="0", LOG_FILE=str(log),
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("always-bypass", result.stdout)
        self.assertIn("always-bypass", log.read_text(encoding="utf-8", errors="replace"))

    def test_filtered_messages_still_exit_zero(self) -> None:
        result = self.run_bin(["TRACE", "filtered"], LOG_LEVEL="INFO")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")


class TestEnvironmentConfiguration(LogamuffinTestCase):
    def test_invalid_log_level_is_rejected(self) -> None:
        result = self.run_bin(["INFO", "x"], LOG_LEVEL="BOGUS")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Invalid LOG_LEVEL: BOGUS", result.stderr)

    def test_log_level_always_is_rejected_as_threshold(self) -> None:
        result = self.run_bin(["INFO", "x"], LOG_LEVEL="ALWAYS")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Invalid LOG_LEVEL: ALWAYS", result.stderr)

    def test_log_level_is_case_insensitive_and_accepts_aliases(self) -> None:
        for threshold in ("debug", "DEBUG", "Debug", "WARN", "warn", "warning"):
            with self.subTest(threshold=threshold):
                # At DEBUG threshold DEBUG emits; at WARN threshold INFO is
                # filtered but WARNING emits. Pick a level that must emit.
                level = "DEBUG" if "debug" in threshold.lower() else "WARNING"
                result = self.run_bin(
                    [level, f"env-{threshold}"],
                    LOG_LEVEL=threshold, LOG_COLOR="0",
                )
                self.assertEqual(result.returncode, 0, f"stderr: {result.stderr!r}")
                self.assertIn(f"env-{threshold}", result.stdout)

    def test_default_log_file_is_created_relative_to_cwd(self) -> None:
        result = self.run_bin(
            ["INFO", "default-file-msg"],
            LOG_FILE=None,
            cwd=self.dir,
        )
        self.assertEqual(result.returncode, 0)
        default_log = self.dir / ".logs" / "log.log"
        self.assertTrue(default_log.is_file(), "default .logs/log.log should be created")
        self.assertIn("default-file-msg", default_log.read_text(encoding="utf-8", errors="replace"))

    def test_empty_log_file_falls_back_to_default(self) -> None:
        result = self.run_bin(
            ["INFO", "empty-logfile-msg"],
            LOG_FILE="",
            cwd=self.dir,
        )
        self.assertEqual(result.returncode, 0)
        default_log = self.dir / ".logs" / "log.log"
        self.assertTrue(default_log.is_file())
        self.assertIn("empty-logfile-msg", default_log.read_text(encoding="utf-8", errors="replace"))

    def test_log_source_overrides_source_name(self) -> None:
        result = self.run_bin(
            ["INFO", "src-test"],
            LOG_SOURCE="/some/dir/custom.sh", LOG_COLOR="0",
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("custom.sh", result.stdout)
        self.assertIn("custom.sh", self.read_log())

    def test_log_source_unset_uses_executable_name(self) -> None:
        result = self.run_bin(["INFO", "src-default"], LOG_SOURCE=None, LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        exe_name = Path(self.exe).name
        self.assertIn(exe_name, result.stdout)


class TestOutputStreams(LogamuffinTestCase):
    def test_info_and_lower_go_to_stdout(self) -> None:
        for level in ("TRACE", "DEBUG", "INFO", "WARNING"):
            with self.subTest(level=level):
                result = self.run_bin([level, f"s-{level}"], LOG_LEVEL="TRACE", LOG_COLOR="0")
                self.assertEqual(result.returncode, 0)
                self.assertIn(f"s-{level}", result.stdout)
                self.assertEqual(result.stderr, "")

    def test_error_and_critical_go_to_stderr(self) -> None:
        for level in ("ERROR", "CRITICAL", "FATAL"):
            with self.subTest(level=level):
                result = self.run_bin([level, f"s-{level}"], LOG_LEVEL="TRACE", LOG_COLOR="0")
                self.assertEqual(result.returncode, 0)
                self.assertIn(f"s-{level}", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_always_goes_to_stdout(self) -> None:
        result = self.run_bin(["ALWAYS", "s-ALWAYS"], LOG_LEVEL="TRACE", LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertIn("s-ALWAYS", result.stdout)
        self.assertEqual(result.stderr, "")


class TestFileHandling(LogamuffinTestCase):
    def test_log_file_is_created_and_appended_across_invocations(self) -> None:
        first = self.run_bin(["INFO", "first-msg"], LOG_COLOR="0")
        second = self.run_bin(["INFO", "second-msg"], LOG_COLOR="0")
        self.assertEqual(first.returncode, 0)
        self.assertEqual(second.returncode, 0)
        lines = self.read_log().splitlines()
        self.assertGreaterEqual(len(lines), 2)
        joined = "\n".join(lines)
        self.assertIn("first-msg", joined)
        self.assertIn("second-msg", joined)
        # Append order is preserved: first line first.
        self.assertLess(joined.index("first-msg"), joined.index("second-msg"))

    def test_log_file_parent_directories_are_created(self) -> None:
        nested = self.dir / "deep" / "nest" / "log.txt"
        result = self.run_bin(["INFO", "nested-msg"], LOG_FILE=str(nested), LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertTrue(nested.is_file())
        self.assertIn("nested-msg", nested.read_text(encoding="utf-8", errors="replace"))

    def test_filtered_messages_do_not_create_log_file(self) -> None:
        result = self.run_bin(["TRACE", "filtered"], LOG_LEVEL="INFO")
        self.assertEqual(result.returncode, 0)
        self.assertFalse(self.log_file.exists())

    def test_log_file_stays_plain_text_when_color_enabled(self) -> None:
        result = self.run_bin(["INFO", "color-file-msg"], LOG_COLOR="1")
        self.assertEqual(result.returncode, 0)
        content = self.read_log()
        self.assertIn("color-file-msg", content)
        self.assertNotIn("\033", content)

    def test_log_file_is_directory_warns_but_console_succeeds(self) -> None:
        adir = self.dir / "adir"
        adir.mkdir()
        result = self.run_bin(["INFO", "dirfile-msg"], LOG_FILE=str(adir), LOG_COLOR="0")
        # Console output still succeeds; only the file append fails.
        self.assertEqual(result.returncode, 0)
        self.assertIn("dirfile-msg", result.stdout)
        self.assertIn("failed to append", result.stderr)

    def test_version_does_not_write_log_file(self) -> None:
        result = self.run_bin(["--version"], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertFalse(self.log_file.exists())

    def test_log_file_line_matches_documented_format(self) -> None:
        result = self.run_bin(["INFO", "format-msg"], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        lines = self.read_log().splitlines()
        self.assertEqual(len(lines), 1)
        line = lines[0]
        self.assertRegex(line, FILE_LINE_RE)
        self.assertIn("[INFO]", line)
        self.assertTrue(line.endswith("format-msg"))


class TestColorBehavior(LogamuffinTestCase):
    def test_log_color_zero_produces_plain_output(self) -> None:
        result = self.run_bin(["INFO", "plain-msg"], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("\033", result.stdout)

    def test_log_color_one_produces_ansi_output(self) -> None:
        result = self.run_bin(["INFO", "color-msg"], LOG_COLOR="1")
        self.assertEqual(result.returncode, 0)
        self.assertIn("\033", result.stdout)
        self.assertIn("color-msg", result.stdout)

    def test_log_color_truthy_and_falsy_variants(self) -> None:
        for value in ("1", "TRUE", "true", "True", "YES", "yes", "ON", "on"):
            with self.subTest(value=value, expect_ansi=True):
                result = self.run_bin(["INFO", f"c-{value}"], LOG_COLOR=value)
                self.assertEqual(result.returncode, 0)
                self.assertIn("\033", result.stdout)
        for value in ("0", "FALSE", "false", "NO", "no", "OFF", "off", "bogus", ""):
            with self.subTest(value=value, expect_ansi=False):
                result = self.run_bin(["INFO", f"c-{value}"], LOG_COLOR=value)
                self.assertEqual(result.returncode, 0)
                self.assertNotIn("\033", result.stdout)

    def test_unset_log_color_with_captured_output_is_plain(self) -> None:
        # Captured (non-TTY) output defaults to plain text.
        result = self.run_bin(["INFO", "c-default"], LOG_COLOR=None)
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("\033", result.stdout)


class TestMessageHandling(LogamuffinTestCase):
    def test_multi_argument_message_is_joined_with_single_spaces(self) -> None:
        result = self.run_bin(["INFO", "hello", "world", "foo"], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertIn("hello world foo", result.stdout)
        self.assertIn("hello world foo", self.read_log())

    def test_multiline_message_preserves_line_breaks(self) -> None:
        message = "MULTI_FIRST\nMULTI_SECOND\nMULTI_THIRD"
        result = self.run_bin(["INFO", message], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        for segment in ("MULTI_FIRST", "MULTI_SECOND", "MULTI_THIRD"):
            self.assertIn(segment, result.stdout)
            self.assertIn(segment, self.read_log())

    def test_message_with_carriage_returns_is_cleaned(self) -> None:
        result = self.run_bin(["INFO", "a\r\nb\r"], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("\r", result.stdout)
        self.assertNotIn("\r", self.read_log())
        self.assertIn("a\nb", result.stdout)

    def test_message_with_special_characters_is_preserved(self) -> None:
        message = "quote: 'x' \"y\" dollar:$HOME star:* tab:[x]"
        result = self.run_bin(["INFO", message], LOG_COLOR="0")
        self.assertEqual(result.returncode, 0)
        self.assertIn(message, result.stdout)
        self.assertIn(message, self.read_log())


class TestConcurrentWrites(LogamuffinTestCase):
    def test_concurrent_writes_all_appear_in_file(self) -> None:
        count = 10
        with ThreadPoolExecutor(max_workers=count) as pool:
            futures = [
                pool.submit(
                    self.run_bin, ["INFO", f"concurrent-{i}"], LOG_COLOR="0",
                )
                for i in range(count)
            ]
            results = [f.result() for f in futures]

        for result in results:
            self.assertEqual(result.returncode, 0)
        content = self.read_log()
        for i in range(count):
            self.assertIn(f"concurrent-{i}", content)
        # Each concurrent invocation contributes exactly one file line.
        self.assertEqual(len(content.splitlines()), count)


class TestExitCodes(LogamuffinTestCase):
    def test_success_exit_code_is_zero(self) -> None:
        self.assertEqual(self.run_bin(["INFO", "ok"]).returncode, 0)
        self.assertEqual(self.run_bin(["ERROR", "ok"]).returncode, 0)
        self.assertEqual(self.run_bin(["--version"]).returncode, 0)

    def test_failure_exit_codes(self) -> None:
        self.assertEqual(self.run_bin([]).returncode, 2)
        self.assertEqual(self.run_bin(["BOGUS", "x"]).returncode, 2)
        self.assertEqual(
            self.run_bin(["INFO", "x"], LOG_LEVEL="BOGUS").returncode, 2,
        )


def _main() -> None:
    # Allow `python3 test_cli.py --executable <path> -v ...` by translating
    # --executable into LOG_BIN and stripping it before unittest sees argv.
    argv = sys.argv
    stripped = [argv[0]]
    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == "--executable" and i + 1 < len(argv):
            os.environ["LOG_BIN"] = argv[i + 1]
            i += 2
            continue
        if arg.startswith("--executable="):
            os.environ["LOG_BIN"] = arg.split("=", 1)[1]
            i += 1
            continue
        stripped.append(arg)
        i += 1

    global EXE
    EXE = _candidate_executable()

    if EXE is None or not EXE.is_file():
        print(
            "ERROR: logamuffin executable not found.\n"
            "Build it with ./run_tests.sh or pass "
            "--executable <path> / LOG_BIN=<path>.",
            file=sys.stderr,
        )
        sys.exit(2)

    sys.argv = stripped
    unittest.main(verbosity=2)


if __name__ == "__main__":
    _main()
