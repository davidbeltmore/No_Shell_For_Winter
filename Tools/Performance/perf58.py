#!/usr/bin/env python3
"""Bounded UE 5.8 dungeon benchmark runner for NoShellForWinter."""

from __future__ import annotations

import argparse
import csv
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[2]
PROJECT_FILE = PROJECT_ROOT / "NoShellForWinter.uproject"
DEFAULT_EDITOR = Path(r"D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe")
DEFAULT_OUTPUT = PROJECT_ROOT / "Saved" / "Automation" / "Performance58"
PROFILES = {
    "smoke": ("DungeonSmoke58", 30, 120),
    "natural": ("DungeonNaturalGameplay58", 60, 150),
    "acceptance": ("DungeonAcceptance58", 120, 240),
    "diagnostic": ("DungeonFullStackDiagnostic58", 120, 240),
}
PRESETS = {"Balanced58", "Performance58", "Current", "QualityCurrent"}
REQUIRED_SEGMENTS = {"TraversalStreaming", "CombatEight", "CombatDirtyPawnHud"}
THRESHOLDS = {
    "average_fps": (60.0, "min"),
    "median_fps": (60.0, "min"),
    "one_percent_low_fps": (55.0, "min"),
    "p99_frame_ms": (18.2, "max"),
    "hitches_over_100_ms": (0, "max"),
}
REQUIRED_METADATA = {
    "project_name",
    "engine_version",
    "platform",
    "configuration",
    "map",
    "cpu",
    "gpu",
    "ram_mb",
    "resolution_x",
    "resolution_y",
    "rhi",
    "source_commit",
    "seed",
    "quality_preset",
    "effective_cvar_hash",
    "effective_cvars",
    "gpu_driver",
}
BAD_LOG_MARKERS = {
    "texture_pool": ("texture streaming pool over", "texture pool is over"),
    "shader_compile": ("shaders left to compile", "shader compilation during benchmark"),
    "blueprint_error": ("blueprint runtime error", "script stack"),
}


class Perf58Error(RuntimeError):
    pass


@dataclass(frozen=True)
class LaunchResult:
    summary: Path
    log: Path
    elapsed_seconds: float


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def read_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise Perf58Error(f"No se pudo leer JSON válido: {path}: {exc}") from exc


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def current_commit() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "UNKNOWN"


def windows_hardware() -> dict[str, str]:
    data = {
        "runner_cpu": platform.processor() or "UNKNOWN",
        "runner_gpu": "UNKNOWN",
        "gpu_driver": "UNKNOWN",
        "runner_ram_bytes": "UNKNOWN",
    }
    if os.name != "nt":
        return data
    query = (
        "$ErrorActionPreference='Stop';"
        "$cpu=(Get-CimInstance Win32_Processor|Select-Object -First 1 -Expand Name);"
        "$gpu=(Get-CimInstance Win32_VideoController|Sort-Object AdapterRAM -Descending|Select-Object -First 1);"
        "$ram=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;"
        "[pscustomobject]@{cpu=$cpu;gpu=$gpu.Name;driver=$gpu.DriverVersion;ram=$ram}|ConvertTo-Json -Compress"
    )
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", query],
            capture_output=True,
            text=True,
            timeout=15,
            check=True,
        )
        raw = json.loads(result.stdout)
        data.update(
            runner_cpu=str(raw.get("cpu") or data["runner_cpu"]),
            runner_gpu=str(raw.get("gpu") or "UNKNOWN"),
            gpu_driver=str(raw.get("driver") or "UNKNOWN"),
            runner_ram_bytes=str(raw.get("ram") or "UNKNOWN"),
        )
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        pass
    return data


def summary_candidates(extra_roots: Iterable[Path] = ()) -> list[Path]:
    roots = [DEFAULT_OUTPUT, *extra_roots]
    found: list[Path] = []
    for root in roots:
        if root.is_file() and root.name == "summary.json":
            found.append(root)
        elif root.exists():
            found.extend(root.glob("*/summary.json"))
            found.extend(root.glob("Saved/Automation/Performance58/*/summary.json"))
    return sorted(set(found), key=lambda item: item.stat().st_mtime_ns)


def terminate_process_tree(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            capture_output=True,
            timeout=20,
            check=False,
        )
    else:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()


def mode_bootstrap_arguments(mode: str) -> list[str]:
    if mode == "standalone":
        # UnrealEditor -game still loads the uncooked-only Python plugin when it
        # is enabled by editor tooling. Packaged Test/Shipping never contains
        # that module, and its pre-GC callback materially distorts hitch data.
        return [str(PROJECT_FILE), "-game", "-DisablePython"]
    return []


def launch(
    *,
    executable: Path,
    profile_key: str,
    preset: str,
    mode: str,
    duration: int | None,
    seed: int,
    enemies: int | None,
    timeout: int | None,
    trace: bool,
    output_roots: list[Path],
) -> LaunchResult:
    if profile_key not in PROFILES:
        raise Perf58Error(f"Perfil inválido: {profile_key}")
    if preset not in PRESETS:
        raise Perf58Error(f"Preset inválido: {preset}")
    if profile_key == "natural" and enemies is not None:
        raise Perf58Error("Natural gameplay does not accept benchmark enemies")
    if enemies is None and profile_key != "natural":
        enemies = 8
    if enemies != 8 and profile_key in {"smoke", "acceptance"}:
        raise Perf58Error("Smoke y acceptance requieren exactamente 8 enemigos")
    if not executable.is_file():
        raise Perf58Error(f"Ejecutable no encontrado: {executable}")

    profile, default_duration, hard_timeout = PROFILES[profile_key]
    duration = duration or default_duration
    timeout = timeout or hard_timeout
    if timeout > hard_timeout:
        raise Perf58Error(f"Timeout {timeout}s excede el límite del perfil ({hard_timeout}s)")
    if profile_key == "smoke" and duration > 30:
        raise Perf58Error("Smoke no puede medir más de 30 segundos")
    if profile_key == "natural" and duration > 60:
        raise Perf58Error("Natural gameplay cannot measure more than 60 seconds")
    if profile_key != "smoke" and duration > 120:
        raise Perf58Error("Una ejecución normal no puede medir más de 120 segundos")
    if trace and duration > 45:
        raise Perf58Error("Una traza diagnóstica está limitada a 45 segundos")

    runner_dir = DEFAULT_OUTPUT / "Runner" / f"{profile}_{preset}_{utc_stamp()}"
    runner_dir.mkdir(parents=True, exist_ok=True)
    log_path = runner_dir / "runtime.log"
    trace_path = runner_dir / "capture.utrace"
    before = {path.resolve() for path in summary_candidates(output_roots)}
    commit = current_commit()

    args = [str(executable), *mode_bootstrap_arguments(mode)]
    args.extend(
        [
            "-dx12",
            "-sm6",
            "-ResX=1920",
            "-ResY=1080",
            "-Windowed",
            "-NoVSync",
            "-NoSplash",
            "-Unattended",
            "-ProjectPerfStrictScenarioFailures",
            "-ProjectPerfQuitOnFinish",
            f"-ProjectPerfProfile={profile}",
            f"-ProjectPerfDuration={duration}",
            f"-ProjectPerfSeed={seed}",
            f"-ProjectPerfQualityPreset={preset}",
            f"-ProjectPerfCommit={commit}",
            f"-abslog={log_path}",
        ]
    )
    if enemies is not None:
        args.append(f"-ProjectPerfEnemyCount={enemies}")
    if trace:
        args.extend(["-trace=cpu,gpu,frame,bookmark,loadtime", f"-tracefile={trace_path}"])

    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    started = time.monotonic()
    process = subprocess.Popen(
        args,
        cwd=executable.parent if mode == "packaged" else PROJECT_ROOT,
        text=True,
        creationflags=flags,
    )
    try:
        return_code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        terminate_process_tree(process)
        raise Perf58Error(f"TIMEOUT: {profile} superó {timeout}s") from exc
    elapsed = time.monotonic() - started
    if return_code != 0:
        raise Perf58Error(f"El proceso terminó con código {return_code}; log: {log_path}")

    new_candidates = [
        path for path in summary_candidates([executable.parent, *output_roots])
        if path.resolve() not in before and path.stat().st_mtime >= time.time() - elapsed - 10
    ]
    if not new_candidates:
        raise Perf58Error(f"No apareció summary.json después de la ejecución; log: {log_path}")
    summary_path = new_candidates[-1]
    summary = read_json(summary_path)
    metadata = summary.setdefault("metadata", {})
    hardware = windows_hardware()
    metadata.update(hardware)
    metadata["gpu_driver"] = hardware["gpu_driver"]
    metadata["runner_mode"] = mode
    metadata["runner_executable"] = str(executable)
    metadata["python_runtime_disabled"] = mode == "standalone"
    metadata["runner_timeout_seconds"] = timeout
    metadata["runner_elapsed_seconds"] = round(elapsed, 3)
    metadata["source_commit"] = metadata.get("source_commit") or commit
    summary["runtime_log_path"] = str(log_path)
    if trace:
        summary["trace_path"] = str(trace_path)
    write_json(summary_path, summary)
    shutil.copy2(log_path, summary_path.parent / "runtime.log")
    return LaunchResult(summary_path, log_path, elapsed)


def metric_failures(metrics: dict[str, Any], prefix: str) -> list[str]:
    failures: list[str] = []
    for name, (limit, direction) in THRESHOLDS.items():
        value = metrics.get(name)
        if not isinstance(value, (int, float)):
            failures.append(f"{prefix}: falta métrica {name}")
        elif direction == "min" and value < limit:
            failures.append(f"{prefix}: {name}={value:.3f} < {limit}")
        elif direction == "max" and value > limit:
            failures.append(f"{prefix}: {name}={value:.3f} > {limit}")
    return failures


def inspect_log(path: Path) -> list[str]:
    if not path.is_file():
        return [f"Falta log runtime: {path}"]
    text = path.read_text(encoding="utf-8", errors="replace").lower()
    failures: list[str] = []
    for label, markers in BAD_LOG_MARKERS.items():
        if any(marker in text for marker in markers):
            failures.append(f"Log contiene {label}")
    if "error: " in text and "automation test succeeded" not in text:
        failures.append("Log contiene errores runtime")
    return failures


def validate_summary(path: Path, expected_preset: str = "Balanced58") -> list[str]:
    summary = read_json(path)
    failures: list[str] = []
    metadata = summary.get("metadata")
    if not isinstance(metadata, dict):
        return ["Falta objeto metadata"]
    missing = sorted(key for key in REQUIRED_METADATA if metadata.get(key) in (None, "", "UNKNOWN", "PENDING_RUNNER_METADATA"))
    if missing:
        failures.append("Metadatos ausentes: " + ", ".join(missing))
    if summary.get("benchmark_id") != "DungeonAcceptance58":
        failures.append(f"Perfil no es DungeonAcceptance58: {summary.get('benchmark_id')}")
    if summary.get("quality_preset") != expected_preset or metadata.get("quality_preset") != expected_preset:
        failures.append("El preset cambió o no coincide con el solicitado")
    if metadata.get("resolution_x") != 1920 or metadata.get("resolution_y") != 1080:
        failures.append("Resolución distinta de 1920x1080")
    if "D3D12" not in str(metadata.get("rhi", "")).upper():
        failures.append(f"RHI no es DX12: {metadata.get('rhi')}")
    if metadata.get("seed") != 42 or summary.get("seed") != 42:
        failures.append("Seed distinta de 42")
    if not summary.get("success") or not summary.get("scenario_pass"):
        failures.append(f"Escenario falló: {summary.get('scenario_failure_reason') or summary.get('reason')}")
    if summary.get("enemy_spawn_count") != 8 or summary.get("active_enemy_count") != 8:
        failures.append("No hubo exactamente ocho enemigos activos")
    if summary.get("runtime_budget_enabled"):
        failures.append("ProjectPerformanceBudgetSubsystem estaba activo")
    if not summary.get("acceptance_route_built") or summary.get("acceptance_traversal_distance", 0) < 600:
        failures.append("Recorrido determinista incompleto")
    if summary.get("real_damage_event_count", 0) <= 0:
        failures.append("No se observaron eventos reales de daño")
    if summary.get("real_attack_input_count", 0) <= 0 or summary.get("real_dodge_input_count", 0) <= 0:
        failures.append("No se ejercitaron los inputs reales de ataque y esquiva")
    if summary.get("dirty_pawn_workload_steps", 0) < 6 or not summary.get("hud_opened"):
        failures.append("Carga DirtyPawn/HUD incompleta")
    if summary.get("benchmark_synthetic_frame_count", 0) != 0:
        failures.append("La medición contiene trabajo sintético")
    failures.extend(metric_failures(summary.get("metrics", {}), "total"))

    segments = summary.get("segments", [])
    by_id = {item.get("stage_id"): item for item in segments if isinstance(item, dict)}
    if set(by_id) != REQUIRED_SEGMENTS:
        failures.append(f"Segmentos inválidos: {sorted(by_id)}")
    for stage_id in sorted(REQUIRED_SEGMENTS):
        segment = by_id.get(stage_id)
        if not segment:
            continue
        if not segment.get("success") or segment.get("sample_count", 0) <= 0:
            failures.append(f"{stage_id}: segmento incompleto")
        failures.extend(metric_failures(segment, stage_id))
        if stage_id != "TraversalStreaming":
            if segment.get("benchmark_spawned_enemy_count_peak") != 8:
                failures.append(f"{stage_id}: no mantuvo ocho enemigos")
            for forbidden in (
                "runtime_enemy_without_controller_count_peak",
                "hidden_runtime_enemy_count_peak",
                "collision_disabled_runtime_enemy_count_peak",
                "enemy_mesh_tick_disabled_count_peak",
            ):
                if segment.get(forbidden, 0) != 0:
                    failures.append(f"{stage_id}: {forbidden}={segment.get(forbidden)}")

    log_value = summary.get("runtime_log_path")
    log_path = Path(log_value) if log_value else path.parent / "runtime.log"
    failures.extend(inspect_log(log_path))
    return failures


def compare_summaries(standalone_path: Path, packaged_path: Path) -> dict[str, Any]:
    standalone = read_json(standalone_path)
    packaged = read_json(packaged_path)
    failures: list[str] = []
    standalone_hash = standalone.get("effective_cvar_hash")
    packaged_hash = packaged.get("effective_cvar_hash")
    if not standalone_hash or standalone_hash != packaged_hash:
        failures.append("Los hashes de CVars no coinciden")
    standalone_fps = standalone.get("metrics", {}).get("average_fps", 0)
    packaged_fps = packaged.get("metrics", {}).get("average_fps", 0)
    ratio = packaged_fps / standalone_fps if standalone_fps else 0.0
    if ratio < 0.95:
        failures.append(f"Empaquetado rinde {(1.0 - ratio) * 100.0:.2f}% peor que Standalone")
    return {
        "pass": not failures,
        "standalone": str(standalone_path),
        "packaged": str(packaged_path),
        "standalone_average_fps": standalone_fps,
        "packaged_average_fps": packaged_fps,
        "packaged_to_standalone_ratio": ratio,
        "failures": failures,
    }


def add_launch_options(parser: argparse.ArgumentParser, *, default_profile: str) -> None:
    parser.add_argument("--executable", type=Path, default=DEFAULT_EDITOR)
    parser.add_argument("--mode", choices=("standalone", "packaged"), default="standalone")
    parser.add_argument("--profile", choices=tuple(PROFILES), default=default_profile)
    parser.add_argument("--preset", choices=tuple(sorted(PRESETS)), default="Balanced58")
    parser.add_argument("--duration", type=int)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--enemies", type=int)
    parser.add_argument("--timeout", type=int)
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--output-root", type=Path, action="append", default=[])


def launch_from_args(args: argparse.Namespace) -> LaunchResult:
    return launch(
        executable=args.executable.resolve(),
        profile_key=args.profile,
        preset=args.preset,
        mode=args.mode,
        duration=args.duration,
        seed=args.seed,
        enemies=args.enemies,
        timeout=args.timeout,
        trace=args.trace,
        output_roots=[path.resolve() for path in args.output_root],
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    smoke = commands.add_parser("smoke", help="10 s warmup + 30 s medidos, timeout 120 s")
    add_launch_options(smoke, default_profile="smoke")
    smoke.set_defaults(profile="smoke")

    run = commands.add_parser("run", help="Ejecuta una sola prueba acotada")
    add_launch_options(run, default_profile="acceptance")

    gate = commands.add_parser("gate", help="Valida exactamente dos gates empaquetados")
    gate.add_argument("--summary", type=Path, action="append")
    gate.add_argument("--report", type=Path, default=DEFAULT_OUTPUT / "gate_report.json")
    add_launch_options(gate, default_profile="acceptance")
    gate.set_defaults(profile="acceptance")

    compare = commands.add_parser("compare", help="Compara Standalone y empaquetado con el mismo hash")
    compare.add_argument("standalone", type=Path)
    compare.add_argument("packaged", type=Path)
    compare.add_argument("--report", type=Path, default=DEFAULT_OUTPUT / "compare_report.json")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command in {"smoke", "run"}:
            result = launch_from_args(args)
            print(result.summary)
            summary = read_json(result.summary)
            if not summary.get("success") or not summary.get("scenario_pass"):
                print(
                    f"ERROR: escenario falló: "
                    f"{summary.get('scenario_failure_reason') or summary.get('reason')}",
                    file=sys.stderr,
                )
                return 2
            return 0
        if args.command == "compare":
            result = compare_summaries(args.standalone.resolve(), args.packaged.resolve())
            write_json(args.report.resolve(), result)
            print(json.dumps(result, indent=2, ensure_ascii=False))
            return 0 if result["pass"] else 2
        if args.command == "gate":
            if args.profile != "acceptance":
                raise Perf58Error("The formal gate only accepts the acceptance profile")
            if args.summary:
                summaries = [path.resolve() for path in args.summary]
                if len(summaries) != 2:
                    raise Perf58Error("El gate exige exactamente dos summary.json")
            else:
                if args.mode != "packaged":
                    raise Perf58Error("El gate ejecutable debe usar --mode packaged")
                summaries = [launch_from_args(args).summary for _ in range(2)]
            runs = []
            for summary_path in summaries:
                failures = validate_summary(summary_path, args.preset)
                runs.append({"summary": str(summary_path), "pass": not failures, "failures": failures})
            report = {
                "suite": "Performance58",
                "preset": args.preset,
                "required_run_count": 2,
                "pass": all(run["pass"] for run in runs),
                "runs": runs,
            }
            write_json(args.report.resolve(), report)
            print(json.dumps(report, indent=2, ensure_ascii=False))
            return 0 if report["pass"] else 2
        raise Perf58Error(f"Comando no implementado: {args.command}")
    except (Perf58Error, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
