import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import perf58  # noqa: E402


def passing_summary(log_path: Path) -> dict:
    metrics = {
        "average_fps": 61.0,
        "median_fps": 61.0,
        "one_percent_low_fps": 56.0,
        "p99_frame_ms": 18.0,
        "hitches_over_100_ms": 0,
    }
    segments = []
    for stage in perf58.REQUIRED_SEGMENTS:
        segment = dict(metrics)
        segment.update(
            stage_id=stage,
            success=True,
            sample_count=100,
            benchmark_spawned_enemy_count_peak=0 if stage == "TraversalStreaming" else 8,
            runtime_enemy_without_controller_count_peak=0,
            hidden_runtime_enemy_count_peak=0,
            collision_disabled_runtime_enemy_count_peak=0,
            enemy_mesh_tick_disabled_count_peak=0,
        )
        segments.append(segment)
    metadata = {key: "value" for key in perf58.REQUIRED_METADATA}
    metadata.update(
        resolution_x=1920,
        resolution_y=1080,
        rhi="D3D12",
        seed=42,
        quality_preset="Balanced58",
        source_commit="abc123",
        gpu_driver="1.2.3",
    )
    return {
        "benchmark_id": "DungeonAcceptance58",
        "success": True,
        "scenario_pass": True,
        "quality_preset": "Balanced58",
        "seed": 42,
        "enemy_spawn_count": 8,
        "active_enemy_count": 8,
        "runtime_budget_enabled": False,
        "acceptance_route_built": True,
        "acceptance_traversal_distance": 1000,
        "real_damage_event_count": 3,
        "real_attack_input_count": 12,
        "real_dodge_input_count": 3,
        "dirty_pawn_workload_steps": 6,
        "hud_opened": True,
        "benchmark_synthetic_frame_count": 0,
        "effective_cvar_hash": "abc",
        "metrics": metrics,
        "segments": segments,
        "metadata": metadata,
        "runtime_log_path": str(log_path),
    }


class Perf58Tests(unittest.TestCase):
    def test_standalone_disables_editor_only_python_runtime(self):
        standalone_args = perf58.mode_bootstrap_arguments("standalone")
        self.assertIn("-game", standalone_args)
        self.assertIn("-DisablePython", standalone_args)
        self.assertNotIn("-DisablePython", perf58.mode_bootstrap_arguments("packaged"))

    def test_natural_profile_has_bounded_defaults_and_no_enemy_override(self):
        profile, duration, timeout = perf58.PROFILES["natural"]
        self.assertEqual(profile, "DungeonNaturalGameplay58")
        self.assertEqual(duration, 60)
        self.assertEqual(timeout, 150)

        args = perf58.build_parser().parse_args(["run", "--profile", "natural"])
        self.assertIsNone(args.enemies)

    def test_natural_profile_rejects_benchmark_enemies(self):
        with self.assertRaisesRegex(perf58.Perf58Error, "does not accept benchmark enemies"):
            perf58.launch(
                executable=Path(sys.executable),
                profile_key="natural",
                preset="Performance58",
                mode="standalone",
                duration=60,
                seed=42,
                enemies=0,
                timeout=150,
                trace=False,
                output_roots=[],
            )

    def test_natural_profile_rejects_unbounded_duration(self):
        with self.assertRaisesRegex(perf58.Perf58Error, "more than 60 seconds"):
            perf58.launch(
                executable=Path(sys.executable),
                profile_key="natural",
                preset="Performance58",
                mode="standalone",
                duration=61,
                seed=42,
                enemies=None,
                timeout=150,
                trace=False,
                output_roots=[],
            )

    def test_passing_summary(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            log = root / "runtime.log"
            log.write_text("LogInit: Display: ready\n", encoding="utf-8")
            summary = root / "summary.json"
            summary.write_text(json.dumps(passing_summary(log)), encoding="utf-8")
            self.assertEqual(perf58.validate_summary(summary), [])

    def test_segment_failure_is_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            log = root / "runtime.log"
            log.write_text("ready\n", encoding="utf-8")
            data = passing_summary(log)
            data["segments"][0]["one_percent_low_fps"] = 40
            summary = root / "summary.json"
            summary.write_text(json.dumps(data), encoding="utf-8")
            self.assertTrue(any("one_percent_low_fps" in item for item in perf58.validate_summary(summary)))

    def test_compare_rejects_packaged_regression(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            standalone = root / "standalone.json"
            packaged = root / "packaged.json"
            base = {"effective_cvar_hash": "same", "metrics": {"average_fps": 100.0}}
            standalone.write_text(json.dumps(base), encoding="utf-8")
            slow = {"effective_cvar_hash": "same", "metrics": {"average_fps": 94.0}}
            packaged.write_text(json.dumps(slow), encoding="utf-8")
            self.assertFalse(perf58.compare_summaries(standalone, packaged)["pass"])


if __name__ == "__main__":
    unittest.main()
