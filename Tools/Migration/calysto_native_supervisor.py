"""Bounded supervisor and strict report audit for the protected native test driver."""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(r"D:/Projects UE5/NoShellForWinter").resolve()
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)
ERROR_MARKERS = ("Fatal error:", "Assertion failed:", "Unhandled Exception:",
                 "=== Critical error:", "Ensure condition failed", "Blueprint Runtime Error", "Accessed None")
ERROR_CATEGORY = re.compile(r"(?:^|\])Log[A-Za-z0-9_]{1,128}: Error:")


def bounded_text(path: Path, limit: int) -> str:
    if path.stat().st_size > limit:
        raise ValueError(f"Evidence exceeds its bounded byte limit: {path.name}")
    return path.read_text(encoding="utf-8-sig", errors="replace")


def audit(expected: list[str], report: Path, log: Path) -> tuple[list[str], int]:
    failures: list[str] = []
    actual_count = 0
    if not report.is_file():
        failures.append("Automation report missing.")
    else:
        try:
            result = json.loads(bounded_text(report, 16 * 1024 * 1024))
            # UE exports camelCase; accept the same fields regardless of casing.
            result = {k.lower(): v for k, v in result.items()}
            tests = [{k.lower(): v for k, v in t.items()} for t in result["tests"]]
            actual_count = len(tests)
            names = [t["fulltestpath"] for t in tests]
            if sorted(names) != sorted(expected) or len(set(names)) != len(names):
                failures.append("Automation inventory differs from the reviewed exact expected set.")
            for test in tests:
                if test["state"] != "Success" or test["errors"] != 0 or test["warnings"] != 0:
                    failures.append("Test did not pass cleanly: " + str(test["fulltestpath"]))
            if result["succeeded"] != len(expected) or any(result[k] != 0 for k in
                    ("succeededwithwarnings", "failed", "notrun", "inprocess")):
                failures.append("Aggregate Automation results violate the complete exact inventory.")
        except (ValueError, TypeError, KeyError, OSError) as error:
            failures.append("Automation report is invalid or incomplete: " + str(error))
    if not log.is_file():
        failures.append("Native process log missing.")
    else:
        try:
            started = time.monotonic()
            for number, line in enumerate(bounded_text(log, 64 * 1024 * 1024).splitlines(), 1):
                if time.monotonic() - started > 10:
                    raise ValueError("Native log audit exceeded ten seconds.")
                if len(line) > 1024 * 1024:
                    raise ValueError("Native log contains an unsupported line larger than one MiB.")
                if any(marker in line for marker in ERROR_MARKERS) or ERROR_CATEGORY.search(line):
                    failures.append(f"Native.log:{number}: {line[:2048]}")
                    if len(failures) >= 100:
                        failures.append("Error receipt capped at 100 entries; inspect the retained full log.")
                        break
        except (ValueError, OSError) as error:
            failures.append(str(error))
    return failures, actual_count


def stop_owned_editor(project: Path, log: Path) -> list[str]:
    # Data is single-quoted as PowerShell literals; no expression interpolation.
    literal = lambda value: "'" + str(value).replace("'", "''") + "'"
    command = ("$ErrorActionPreference='Stop'; Get-CimInstance Win32_Process "
               "-Filter \"Name = 'UnrealEditor.exe' OR Name = 'UnrealEditor-Cmd.exe'\" "
               "-OperationTimeoutSec 2 | Where-Object { $_.CommandLine -and "
               f"$_.CommandLine.Contains({literal(project)}) -and $_.CommandLine.Contains({literal(log)}) "
               "} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    try:
        result = subprocess.run([shutil.which("powershell.exe") or "powershell.exe", "-NoProfile", "-Command", command],
                                capture_output=True, text=True, timeout=4, creationflags=HIDDEN)
        return [] if result.returncode == 0 else ["Exact owned editor cleanup failed: " + result.stderr[:2048]]
    except (OSError, subprocess.TimeoutExpired) as error:
        return ["Exact owned editor cleanup did not complete: " + str(error)]


def run_driver(driver: Path, budget: float, on_timeout, report_stage) -> tuple[int | None, bool, list[str]]:
    """Own one child; separate from report parsing so both paths can be tested without Unreal."""
    process = subprocess.Popen([shutil.which("powershell.exe") or "powershell.exe", "-NoProfile",
        "-ExecutionPolicy", "Bypass", "-File", str(driver)], cwd=ROOT,
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=HIDDEN)
    report_stage("driver_running", driver_pid=process.pid)
    try:
        return process.wait(timeout=budget), False, []
    except subprocess.TimeoutExpired:
        failures = [f"Native driver exceeded {budget} seconds."]
        failures.extend(on_timeout())
        try:
            return process.wait(timeout=5), True, failures
        except subprocess.TimeoutExpired:
            process.kill()
            try:
                return process.wait(timeout=2), True, failures
            except subprocess.TimeoutExpired:
                failures.append("Owned driver did not exit during bounded cleanup.")
                return None, True, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--budget", type=int, default=180)
    parser.add_argument("--test-prefix", default="NoShellForWinter.CalystoDungeon.")
    parser.add_argument("--startup-map", default="/Engine/Maps/Entry")
    parser.add_argument("--postprocess-only", action="store_true")
    parser.add_argument("--launcher-exit-code", type=int)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "Saved/Migration/CalystoDungeonDirectorV7") or not 30 <= args.budget <= 300:
        parser.error("Output must be owned V7 evidence; budget must be 30 to 300 seconds.")
    if not args.expected.resolve().is_relative_to(ROOT):
        parser.error("Expected inventory must belong to this target workspace.")
    if not re.fullmatch(r"NoShellForWinter[.]CalystoDungeon[.][A-Za-z0-9_.]*", args.test_prefix):
        parser.error("Unsupported test prefix.")
    expected = [line for line in bounded_text(args.expected, 128 * 1024).splitlines() if line.strip()]
    if not 1 <= len(expected) <= 512 or len(set(expected)) != len(expected) or any(
            not name.startswith(args.test_prefix) for name in expected):
        parser.error("Expected inventory must be a reviewed nonempty unique set of supported test names.")
    project = ROOT / "NoShellForWinter.uproject"
    log, report = output / "Native.log", output / "Report/index.json"
    started = time.monotonic()
    failures: list[str] = []
    exit_code = args.launcher_exit_code
    timed_out = False

    def stage(name: str, **values: object) -> None:
        (output / "SupervisorStatus.json").write_text(json.dumps({"stage": name,
            "elapsed_seconds": time.monotonic() - started, **values}, indent=2), encoding="utf-8")

    try:
        if not args.postprocess_only:
            exit_code, timed_out, supervision_failures = run_driver(output / "Driver.ps1", args.budget,
                lambda: stop_owned_editor(project, log), stage)
            failures.extend(supervision_failures)
        elif exit_code is None:
            raise ValueError("Postprocessing requires the recorded actual launcher exit code.")
        stage("driver_exited", launcher_exit_code=exit_code, timed_out=timed_out)
        if exit_code != 0:
            failures.append(f"Protected launcher process exit must be 0; actual={exit_code}")
        stage("report_and_log_audit")
        audit_failures, actual_count = audit(expected, report, log)
        failures.extend(audit_failures)
    except (ValueError, OSError) as error:
        failures.append(str(error))
        actual_count = 0
    summary = {"status": "FAIL" if failures else "PASS", "gate": "NATIVE_TESTS_ONLY",
        "gameplay_verified": False, "project": str(project), "startup_map": args.startup_map, "test_prefix": args.test_prefix,
        "elapsed_seconds": time.monotonic() - started, "timeout_seconds": args.budget,
        "timed_out": timed_out, "launcher_exit_code": exit_code, "expected_tests": expected,
        "actual_test_count": actual_count, "log": str(log), "report": str(report), "failures": failures}
    (output / "StrictSummary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    stage("complete", status=summary["status"])
    print(json.dumps({k: v for k, v in summary.items() if k not in ("expected_tests", "failures")}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
