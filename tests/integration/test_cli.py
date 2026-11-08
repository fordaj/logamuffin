#!/usr/bin/env python3
"""Logamuffin CLI integration tests (Python standard library only).

These tests exercise the compiled logamuffin executable as a black box:
argument parsing, severity filtering, environment configuration, stream
routing, file logging, ANSI colors, and concurrency. They mirror and
extend the coverage of the legacy Bash suite (test.sh) while running
cross-platform (macOS, Linux, Windows) without extra dependencies.

Usage:
    # Build first (one of), then point LOG_BIN at the executable:
    ./build.sh
    LOG_BIN=macos-arm64/logamuffin python3 tests/integration/test_cli.py

    # Or pass the executable explicitly:
    python3 tests/integration/test_cli.py --executable ./build/logamuffin

    # Against a packaged release binary:
    LOG_BIN=./logamuffin python3 tests/integration/test_cli.py

Environment:
    LOG_BIN: Explicit path to the logamuffin executable to test.
             If unset, candidates are tried in order (see find_executable).

Each test runs in an isolated temporary directory with its own LOG_FILE,
and cleans up afterwards. Individual failures are reported by unittest
without aborting the whole suite.
"""

from __future__ import annotations

import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
TIMESTAMP_RE = re.compile(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3}")


def find_executable() -> Path:
    """Locate the logamuffin executable to test.

    Priority:
      1. $LOG_BIN (explicit).
      2. Platform-specific build output directories (./build.sh outputs).
      3. ./build/logamuffin (CI / local native build).
      4. logamuffin / logamuffin.exe on PATH.
    """
    env_bin = os.environ.get("LOG_BIN", "").strip()
    if env_bin:
        p = Path(env_bin)
        if not p.is_absolute():
            p = (Path.cwd() / p).resolve()
        return p

    parts = list(Path.cwd().parts)
    # Candidates keyed by any (system, machine) pair.
    candidates: list[Path] = []
    sys_name = platform.system()
    machine = platform.machine().lower()
    if sys_name == "Darwin":
        if machine in ("arm64", "aarch64"):
            candidates.append(REPO_ROOT / "macos-arm64" / "logamuffin")
        else:
            candidates.append(REPO_ROOT / "macos-x86_64" / "logamuffin")
        candidates.append(REPO_ROOT / "macos-arm64" / "logamuffin")
        candidates.append(REPO_ROOT / "macos-x86_64" / "logamuffin")
    elif sys_name == "Windows":
        candidates.append(REPO_ROOT / "windows-x86_64" / "logamuffin.exe")
        candidates.append(REPO_ROOT / "windows-arm64" / "logamuffin.exe")
    elif sys_name == "Linux":
        if machine in ("aarch64", "arm64"):
            candidates.append(REPO_ROOT / "linux-arm64" / "logamuffin")
        else:
            candidates.append(REPO_ROOT / "linux-x86_64" / "logamuffin")
        candidates.append(REPO_ROOT / "linux-arm64" / "logamuffin")
        candidates.append(REPO_ROOT / "linux-x86_64" / "logamuffin")

    candidates.extend(
        [
            REPO_ROOT / "build" / "logamuffin",
            REPO_ROOT / "build" / "logamuffin.exe",
            REPO_ROOT / "logamuffin",
            REPO_ROOT / "logamuffin.exe",
        ]
    )
    for c in candidates:
        if c.is_file():
            return c

    exe_name = "logamuffin.exe" if sys_name == "Windows" else "logamuffin"
    found = shutil.which(exe_name)
    if found:
        return Path(found)

    raise RuntimeError(
        "Could not find logamuffin executable. "
        "Set LOG_BIN to an explicit path, or build it first "
        "(./build.sh) or with a native compile (see README Testing)."
    )


def parse_executable_arg(argv: list[str]) -> tuple[list[str], str | None]:
    """Extract '--executable PATH' from argv before unittest parses."""
    remaining: list[str] = []
    explicit: str | None = None
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--executable":
            if i + 1 >= len(argv):
                raise SystemExit("--executable requires a path argument")
            explicit = argv[i + 1]
            i += 2
        elif arg.startswith("--executable="):
            explicit = arg.split("=", 1)[1]
            i += 1
        else:
            remaining.append(arg)
            i += 1
    return remaining, explicit


class LogamuffinCLITest(unittest.TestCase):
    """Black-box CLI tests. Each test gets an isolated temp directory."""

    exe: Path

    @classmethod
    def setUpClass(cls) -> None:
        exe = os.environ.get("LOG_BIN", "").strip()
        if exe:
            p = Path(exe)
            if not p.is_absolute():
                p = (Path.cwd() / p).resolve()
            cls.exe = p
        else:
            cls.exe = find_executable()
        if not cls.exe.is_file():
            raise RuntimeError(f"Logamuffin executable not found: {cls.exe}")
        # Make sure it is executable/runnable.
        try:
            result = subprocess.run(
                [str(cls.exe), "--version"],
                capture_output=True,
                text=True,
                timeout=10,
            )
        except Exception as exc:  # pragma: no cover - manual debugging
            raise RuntimeError(
                f"Failed to run logamuffin at {cls.exe}: {exc}"
            ) from exc
        if result.returncode != 0:
            raise RuntimeError(
                f"logamuffin --version failed (exit {result.returncode}): "
                f"{result.stderr!r} {result.stdout!r}"
            )

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="logamuffin_test_")
        self.addCleanup(self._tmp.cleanup)
        self.tmpdir = Path(self._tmp.name)
        self.log_file = self.tmpdir / "test.log"
        # Purge any inherited log env so tests control it explicitly.
        self._base_env = dict(os.environ)
        for k in ("LOG_LEVEL", "LOG_COLOR", "LOG_FILE", "LOG_SOURCE"):
            self._base_env.pop(k, None)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def run_logamuffin(
        self,
        args: list[str],
        env_overrides: dict[str, str | None] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        env = dict(self._base_env)
        env["LOG_FILE"] = str(self.log_file)
        env["LOG_COLOR"] = "0"
        if env_overrides:
            for k, v in env_overrides.items():
                if v is None:
                    env.pop(k, None)
                else:
                    env[k] = v
        # cwd is the isolated temp dir so the default .logs/log.log
        # stays contained.
        return subprocess.run(
            [str(self.exe), *args],
            capture_output=True,
            text=True,
            env=env,
            cwd=str(self.tmpdir),
            timeout=10,
        )

    def read_log(self) -> str:
        if not self.log_file.is_file():
            return ""
        return self.log_file.read_text(encoding="utf-8", errors="replace")

    def truncate_log(self) -> None:
        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        self.log_file.write_text("", encoding="utf-8")

    # ------------------------------------------------------------------
    # Version and usage / argument parsing
    # ------------------------------------------------------------------

    def test_version_succeeds(self) -> None:
        r = self.run_logamuffin(["--version"])
        self.assertEqual(r.returncode, 0, f"stdout={r.stdout!r} stderr={r.stderr!r}")
        self.assertIn("logamuffin", r.stdout)

    def test_version_output_format(self) -> None:
        r = self.run_logamuffin(["--version"])
        self.assertEqual(r.returncode, 0)
        # e.g. "logamuffin 0.1.3"
        self.assertRegex(r.stdout.strip(), r"^logamuffin \d+\.\d+\.\d+$")

    def test_missing_args_exit_2(self) -> None:
        r = self.run_logamuffin([])
        self.assertEqual(r.returncode, 2, f"stdout={r.stdout!r} stderr={r.stderr!r}")
        self.assertIn("Usage", r.stderr)

    def test_missing_message_exit_2(self) -> None:
        r = self.run_logamuffin(["INFO"])
        self.assertEqual(r.returncode, 2)
        self.assertIn("Usage", r.stderr)

    def test_unknown_level_exit_2(self) -> None:
        r = self.run_logamuffin(["INVALID", "hello"])
        self.assertEqual(r.returncode, 2)
        self.assertIn("Unknown log level", r.stderr)

    def test_help_treated_as_unknown_level(self) -> None:
        # No --help flag; it is parsed as a level and rejected.
        r = self.run_logamuffin(["--help", "hello"])
        self.assertEqual(r.returncode, 2)

    def test_version_with_extra_arg_is_unknown_level(self) -> None:
        # Only the exact '--version' (argc==2) is special.
        r = self.run_logamuffin(["--version", "extra"])
        self.assertEqual(r.returncode, 2)

    # ------------------------------------------------------------------
    # Severity aliases and labels
    # ------------------------------------------------------------------

    def test_all_levels_write_to_file(self) -> None:
        self.truncate_log()
        for level in ("TRACE", "DEBUG", "INFO", "WARN", "WARNING", "ERROR", "CRITICAL", "FATAL"):
            with self.subTest(level=level):
                r = self.run_logamuffin([level, f"TEST_{level}"],
                                        {"LOG_LEVEL": "TRACE"})
                self.assertEqual(r.returncode, 0, f"stderr={r.stderr!r}")
                self.assertIn(f"TEST_{level}", self.read_log())

    def test_always_writes_to_file(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["ALWAYS", "TEST_ALWAYS"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertEqual(r.returncode, 0)
        self.assertIn("TEST_ALWAYS", self.read_log())

    def test_warn_and_warning_share_warn_label(self) -> None:
        for alias in ("WARN", "WARNING"):
            with self.subTest(alias=alias):
                self.truncate_log()
                self.run_logamuffin([alias, "alias_check"],
                                    {"LOG_LEVEL": "TRACE"})
                content = self.read_log()
                self.assertIn("[WARN]", content, content)

    def test_fatal_and_critical_share_critical_label(self) -> None:
        for alias in ("FATAL", "CRITICAL"):
            with self.subTest(alias=alias):
                self.truncate_log()
                self.run_logamuffin([alias, "alias_check"],
                                    {"LOG_LEVEL": "TRACE"})
                content = self.read_log()
                self.assertIn("[CRITICAL]", content, content)

    def test_level_names_in_output(self) -> None:
        cases = [
            ("TRACE", "[TRACE]"),
            ("DEBUG", "[DEBUG]"),
            ("INFO", "[INFO]"),
            ("WARN", "[WARN]"),
            ("ERROR", "[ERROR]"),
            ("CRITICAL", "[CRITICAL]"),
            ("ALWAYS", "[ALWAYS]"),
        ]
        for level, label in cases:
            with self.subTest(level=level):
                self.truncate_log()
                self.run_logamuffin([level, "label_check"],
                                    {"LOG_LEVEL": "TRACE"})
                self.assertIn(label, self.read_log())

    def test_case_insensitive_levels(self) -> None:
        for variant in ("info", "Info", "INFO", "iNfO"):
            with self.subTest(variant=variant):
                self.truncate_log()
                r = self.run_logamuffin([variant, "case_check"],
                                        {"LOG_LEVEL": "TRACE"})
                self.assertEqual(r.returncode, 0)
                self.assertIn("case_check", self.read_log())

    # ------------------------------------------------------------------
    # Filtering
    # ------------------------------------------------------------------

    def test_default_threshold_is_info(self) -> None:
        # Unset LOG_LEVEL: TRACE/DEBUG filtered, INFO kept.
        self.truncate_log()
        self.run_logamuffin(["TRACE", "FILTER_TRACE"])
        self.assertNotIn("FILTER_TRACE", self.read_log())

        self.truncate_log()
        self.run_logamuffin(["DEBUG", "FILTER_DEBUG"])
        self.assertNotIn("FILTER_DEBUG", self.read_log())

        self.truncate_log()
        r = self.run_logamuffin(["INFO", "FILTER_INFO"])
        self.assertEqual(r.returncode, 0)
        self.assertIn("FILTER_INFO", self.read_log())

    def test_trace_threshold_shows_all(self) -> None:
        self.truncate_log()
        for level in ("TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"):
            self.run_logamuffin([level, f"keep_{level}"],
                                {"LOG_LEVEL": "TRACE"})
        content = self.read_log()
        for level in ("TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"):
            self.assertIn(f"keep_{level}", content)

    def test_warning_threshold_filters_info(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["INFO", "filtered_info"],
                                {"LOG_LEVEL": "WARNING"})
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("filtered_info", self.read_log())

        self.truncate_log()
        self.run_logamuffin(["WARNING", "kept_warning"],
                            {"LOG_LEVEL": "WARNING"})
        self.assertIn("kept_warning", self.read_log())

    def test_error_threshold_filters_warning(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["WARN", "filtered_warn"],
                            {"LOG_LEVEL": "ERROR"})
        self.assertNotIn("filtered_warn", self.read_log())

        self.truncate_log()
        self.run_logamuffin(["ERROR", "kept_error"],
                            {"LOG_LEVEL": "ERROR"})
        self.assertIn("kept_error", self.read_log())

    def test_always_bypasses_threshold(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["ALWAYS", "FILTER_ALWAYS"],
                                {"LOG_LEVEL": "CRITICAL"})
        self.assertEqual(r.returncode, 0)
        self.assertIn("FILTER_ALWAYS", self.read_log())

    def test_filtered_still_exit_0(self) -> None:
        r = self.run_logamuffin(["TRACE", "filtered_msg"],
                                {"LOG_LEVEL": "INFO"})
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("filtered_msg", self.read_log())

    def test_filtered_produces_no_stream_output(self) -> None:
        r = self.run_logamuffin(["TRACE", "filtered_stream"],
                                {"LOG_LEVEL": "INFO"})
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("filtered_stream", r.stdout)
        self.assertNotIn("filtered_stream", r.stderr)

    # ------------------------------------------------------------------
    # Environment variables
    # ------------------------------------------------------------------

    def test_invalid_log_level_exit_2(self) -> None:
        r = self.run_logamuffin(["INFO", "test"],
                                {"LOG_LEVEL": "BOGUS"})
        self.assertEqual(r.returncode, 2)
        self.assertIn("Invalid LOG_LEVEL", r.stderr)

    def test_log_level_always_invalid_as_threshold(self) -> None:
        # ALWAYS is a message level but not a valid threshold.
        r = self.run_logamuffin(["INFO", "test"],
                                {"LOG_LEVEL": "ALWAYS"})
        self.assertEqual(r.returncode, 2)
        self.assertIn("Invalid LOG_LEVEL", r.stderr)

    def test_log_level_case_insensitive(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["DEBUG", "debug_msg"],
                                {"LOG_LEVEL": "debug"})
        self.assertEqual(r.returncode, 0)
        self.assertIn("debug_msg", self.read_log())

    def test_log_level_aliases_as_threshold(self) -> None:
        # WARN alias works as threshold.
        self.truncate_log()
        self.run_logamuffin(["WARN", "warn_kept"],
                            {"LOG_LEVEL": "WARN"})
        self.assertIn("warn_kept", self.read_log())

        # FATAL alias works as threshold.
        self.truncate_log()
        self.run_logamuffin(["ERROR", "error_filtered"],
                            {"LOG_LEVEL": "FATAL"})
        self.assertNotIn("error_filtered", self.read_log())

    def test_log_source_custom(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["INFO", "sourced_msg"],
                            {"LOG_SOURCE": "/tmp/myapp.sh"})
        self.assertIn("myapp.sh", self.read_log())

    def test_log_source_windows_path(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["INFO", "sourced_msg"],
                            {"LOG_SOURCE": r"C:\tools\worker.exe"})
        self.assertIn("worker.exe", self.read_log())

    def test_log_source_fallback_contains_executable_basename(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["INFO", "fallback_msg"])
        content = self.read_log()
        expected = Path(self.exe).name
        self.assertIn(expected, content, content)

    def test_default_log_file_in_dot_logs(self) -> None:
        # With LOG_FILE unset, messages go to ./.logs/log.log under cwd.
        env = dict(self._base_env)
        env["LOG_COLOR"] = "0"
        env.pop("LOG_FILE", None)
        r = subprocess.run(
            [str(self.exe), "INFO", "default_file_msg"],
            capture_output=True,
            text=True,
            env=env,
            cwd=str(self.tmpdir),
            timeout=10,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        default_log = self.tmpdir / ".logs" / "log.log"
        self.assertTrue(default_log.is_file(),
                        f"Expected {default_log} to exist")
        self.assertIn("default_file_msg",
                      default_log.read_text(encoding="utf-8"))

    # ------------------------------------------------------------------
    # stdout / stderr routing
    # ------------------------------------------------------------------

    def test_info_to_stdout(self) -> None:
        r = self.run_logamuffin(["INFO", "STREAM_INFO"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_INFO", r.stdout)
        self.assertNotIn("STREAM_INFO", r.stderr)

    def test_debug_to_stdout(self) -> None:
        r = self.run_logamuffin(["DEBUG", "STREAM_DEBUG"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_DEBUG", r.stdout)

    def test_trace_to_stdout(self) -> None:
        r = self.run_logamuffin(["TRACE", "STREAM_TRACE"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_TRACE", r.stdout)

    def test_warning_to_stdout(self) -> None:
        r = self.run_logamuffin(["WARN", "STREAM_WARN"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_WARN", r.stdout)

    def test_error_to_stderr(self) -> None:
        r = self.run_logamuffin(["ERROR", "STREAM_ERROR"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_ERROR", r.stderr)
        self.assertNotIn("STREAM_ERROR", r.stdout)

    def test_critical_to_stderr(self) -> None:
        r = self.run_logamuffin(["CRITICAL", "STREAM_CRITICAL"],
                                {"LOG_LEVEL": "TRACE"})
        self.assertIn("STREAM_CRITICAL", r.stderr)

    def test_always_to_stdout(self) -> None:
        r = self.run_logamuffin(["ALWAYS", "STREAM_ALWAYS"],
                                {"LOG_LEVEL": "CRITICAL"})
        self.assertIn("STREAM_ALWAYS", r.stdout)

    # ------------------------------------------------------------------
    # File logging
    # ------------------------------------------------------------------

    def test_file_creation_and_append(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["INFO", "line_one"])
        self.run_logamuffin(["INFO", "line_two"])
        content = self.read_log()
        self.assertIn("line_one", content)
        self.assertIn("line_two", content)
        # Order preserved.
        self.assertLess(content.index("line_one"), content.index("line_two"))

    def test_file_append_preserves_existing(self) -> None:
        self.truncate_log()
        self.log_file.write_text("PREEXISTING\n", encoding="utf-8")
        self.run_logamuffin(["INFO", "new_message"])
        content = self.read_log()
        self.assertIn("PREEXISTING", content)
        self.assertIn("new_message", content)

    def test_file_creates_nested_directories(self) -> None:
        nested = self.tmpdir / "deep" / "nested" / "log.log"
        r = self.run_logamuffin(["INFO", "nested_msg"],
                                {"LOG_FILE": str(nested)})
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(nested.is_file())
        self.assertIn("nested_msg",
                      nested.read_text(encoding="utf-8"))

    def test_file_contains_timestamp_and_level(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["INFO", "stamp_check"])
        content = self.read_log()
        self.assertRegex(content, TIMESTAMP_RE)
        self.assertIn("[INFO]", content)
        self.assertIn("stamp_check", content)

    def test_empty_log_file_env_uses_default(self) -> None:
        # LOG_FILE="" falls back to .logs/log.log.
        env = dict(self._base_env)
        env["LOG_COLOR"] = "0"
        env["LOG_FILE"] = ""
        r = subprocess.run(
            [str(self.exe), "INFO", "empty_env_msg"],
            capture_output=True,
            text=True,
            env=env,
            cwd=str(self.tmpdir),
            timeout=10,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        default_log = self.tmpdir / ".logs" / "log.log"
        self.assertTrue(default_log.is_file())
        self.assertIn("empty_env_msg",
                      default_log.read_text(encoding="utf-8"))

    def test_log_file_parent_is_file_exit_1(self) -> None:
        # Parent path exists as a file, so create_directories fails.
        blocker = self.tmpdir / "blocker"
        blocker.write_text("not a dir", encoding="utf-8")
        bad_log = blocker / "child.log"
        r = self.run_logamuffin(["INFO", "should_fail"],
                                {"LOG_FILE": str(bad_log)})
        self.assertEqual(r.returncode, 1, f"stderr={r.stderr!r}")
        self.assertIn("failed to create log directory", r.stderr)

    def test_log_file_is_directory_warns_but_succeeds(self) -> None:
        # LOG_FILE points at an existing directory: append fails,
        # but the console message still succeeds (exit 0).
        r = self.run_logamuffin(["INFO", "dir_target_msg"],
                                {"LOG_FILE": str(self.tmpdir)})
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("dir_target_msg", r.stdout)
        self.assertIn("failed to append log file", r.stderr)

    # ------------------------------------------------------------------
    # ANSI colors
    # ------------------------------------------------------------------

    def test_no_color_when_log_color_0(self) -> None:
        r = self.run_logamuffin(["INFO", "plain_msg"],
                                {"LOG_COLOR": "0"})
        self.assertNotIn("\x1b[", r.stdout)

    def test_color_when_log_color_1(self) -> None:
        r = self.run_logamuffin(["INFO", "color_msg"],
                                {"LOG_COLOR": "1"})
        self.assertIn("\x1b[", r.stdout)

    def test_color_truthy_variants(self) -> None:
        for value in ("TRUE", "true", "YES", "ON", "1"):
            with self.subTest(value=value):
                r = self.run_logamuffin(["INFO", "color_var"],
                                        {"LOG_COLOR": value})
                self.assertIn("\x1b[", r.stdout,
                              f"LOG_COLOR={value!r} stdout={r.stdout!r}")

    def test_no_color_falsy_variants(self) -> None:
        for value in ("0", "FALSE", "false", "NO", "OFF", ""):
            with self.subTest(value=value):
                r = self.run_logamuffin(["INFO", "plain_var"],
                                        {"LOG_COLOR": value})
                self.assertNotIn("\x1b[", r.stdout,
                                 f"LOG_COLOR={value!r} stdout={r.stdout!r}")

    def test_file_plain_text_even_with_color(self) -> None:
        self.truncate_log()
        self.run_logamuffin(["ERROR", "color_file_msg"],
                            {"LOG_COLOR": "1"})
        content = self.read_log()
        self.assertIn("color_file_msg", content)
        self.assertNotIn("\x1b[", content)

    # ------------------------------------------------------------------
    # Message formatting
    # ------------------------------------------------------------------

    def test_multi_argument_message_joined(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["INFO", "hello", "world", "again"])
        self.assertEqual(r.returncode, 0)
        self.assertIn("hello world again", self.read_log())
        self.assertIn("hello world again", r.stdout)

    def test_multiline_message_preserved(self) -> None:
        self.truncate_log()
        msg = "MULTI_FIRST\nMULTI_SECOND\nMULTI_THIRD"
        self.run_logamuffin(["INFO", msg])
        content = self.read_log()
        self.assertIn("MULTI_FIRST", content)
        self.assertIn("MULTI_SECOND", content)
        self.assertIn("MULTI_THIRD", content)

    def test_carriage_return_stripped(self) -> None:
        self.truncate_log()
        r = self.run_logamuffin(["INFO", "a\r\nb\r"])
        content = self.read_log()
        self.assertNotIn("\r", content)
        self.assertNotIn("\r", r.stdout)
        self.assertIn("a\nb", content)

    def test_message_with_spaces_and_special_chars(self) -> None:
        self.truncate_log()
        msg = "hello   world  !@#$%^&*()  spaces"
        self.run_logamuffin(["INFO", msg])
        self.assertIn(msg, self.read_log())

    # ------------------------------------------------------------------
    # Concurrency
    # ------------------------------------------------------------------

    def test_concurrent_writes_all_present(self) -> None:
        self.truncate_log()
        tokens = [f"conc_token_{i:03d}" for i in range(30)]
        env = dict(self._base_env)
        env["LOG_FILE"] = str(self.log_file)
        env["LOG_COLOR"] = "0"
        env["LOG_LEVEL"] = "TRACE"

        def run_one(token: str) -> int:
            return subprocess.run(
                [str(self.exe), "INFO", token],
                capture_output=True,
                text=True,
                env=env,
                cwd=str(self.tmpdir),
                timeout=10,
            ).returncode

        with ThreadPoolExecutor(max_workers=10) as pool:
            results = list(pool.map(run_one, tokens))

        for code in results:
            self.assertEqual(code, 0)
        content = self.read_log()
        for token in tokens:
            self.assertIn(token, content, f"Missing token {token}")
        # Each message occupies its own line ending in \n.
        lines = [ln for ln in content.splitlines() if ln]
        self.assertGreaterEqual(len(lines), len(tokens))

    # ------------------------------------------------------------------
    # Exit codes
    # ------------------------------------------------------------------

    def test_success_exit_0(self) -> None:
        r = self.run_logamuffin(["INFO", "ok_msg"])
        self.assertEqual(r.returncode, 0)


def main() -> int:
    argv, explicit = parse_executable_arg(sys.argv[1:])
    if explicit:
        os.environ["LOG_BIN"] = explicit
    # unittest would otherwise treat our custom flag as a test name.
    unittest_argv = [sys.argv[0], *argv]
    program = unittest.main(argv=unittest_argv, module="__main__", exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
