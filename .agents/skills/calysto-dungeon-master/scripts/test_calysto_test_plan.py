"""Test schedule safety and coverage, not the unimplemented V7 runtime."""

import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest

import calysto_test_plan as planner


def step(plan, step_id):
    return next(item for item in plan["steps"] if item["id"] == step_id)


class PlanBehaviorTests(unittest.TestCase):
    def test_quick_is_exactly_three_consecutive_real_door_floors_in_one_run(self):
        plan = planner.build_plan("quick")
        trilogy = step(plan, "real_door_trilogy")
        self.assertEqual(trilogy["accepted_floor_count"], 3)
        self.assertTrue(trilogy["same_run_required"])
        self.assertEqual(trilogy["start_location"], "HUB")
        self.assertEqual([item["floor_number"] for item in trilogy["floors"]], [1, 2, 3])
        self.assertEqual([item["trigger"] for item in trilogy["floors"]],
                         ["real_DoorToLevel", "real_floor_door", "real_floor_door"])
        self.assertEqual(sum(item.get("accepted_floor_count", 0) for item in plan["steps"]), 3)

    def test_original_failure_seeds_are_topology_fixtures_not_run_identity(self):
        plan = planner.build_plan("quick")
        trilogy = step(plan, "real_door_trilogy")
        regression_seeds = [item["initial_topology_seed"] for item in trilogy["floors"]
                            if item["is_failure_regression_seed"]]
        self.assertEqual(regression_seeds, [1779679224, 1190737158])
        self.assertNotIn(trilogy["run_seed"], regression_seeds)
        self.assertEqual(trilogy["run_seed"], plan["seed_policy"]["run_seed"])

    def test_single_request_deadline_covers_all_four_attempts_and_cleanup(self):
        plan = planner.build_plan("quick")
        request = step(plan, "real_door_trilogy")["each_floor_request"]
        self.assertEqual(request["shared_deadline_seconds"], 30)
        self.assertEqual(request["max_total_attempts"], 4)
        self.assertTrue(request["no_deadline_reset_on_retry"])
        self.assertFalse(request["pending_consumes_attempt"])
        self.assertIn("rejected_attempt_cleanup", request["includes"])
        self.assertIn("pending_observation", request["includes"])
        self.assertEqual(plan["timing_policy"]["max_active_generation_attempts"], 1)

    def test_probability_laws_get_large_in_memory_samples_without_world_soak(self):
        probability = step(planner.build_plan("quick"), "probability_suite")
        laws = {item["id"]: item["min_trials"] for item in probability["laws"]}
        required_laws = {
            "additional_room_theme_presence", "conditional_theme_weights", "guaranteed_theme_room_count",
            "conditional_fixed_amount", "conditional_uniform_amount", "conditional_triangular_amount",
            "populated_rarity_tiers_including_winter", "eligible_content_chance", "eligible_entry_weights",
            "eligible_style_weights", "continuous_triangular_layout", "conditional_container_contents",
        }
        self.assertTrue(required_laws.issubset(laws))
        self.assertTrue(all(count >= 100000 for count in laws.values()))
        self.assertEqual(probability["requested_world_floor_count"], 0)
        self.assertEqual(probability["computation_target_seconds"], 15)
        self.assertEqual(probability["hard_cap_seconds"], 180)
        self.assertEqual(probability["report_populations"],
                         ["unconstrained_requested", "feasible_reserved", "accepted_realized"])

    def test_deterministic_edge_cases_are_not_replaced_by_statistics(self):
        cases = set(step(planner.build_plan("quick"), "probability_suite")["deterministic_cases"])
        self.assertTrue({
            "curve_exact_endpoints_and_linear_interpolation",
            "array_reorder_preserves_stable_decisions",
            "display_name_change_preserves_identity_and_behavior",
            "no_eligible_room_rejects_before_content_commitment",
            "disabled_adaptation_has_exactly_zero_influence",
            "incompatible_surface_and_unavailable_footprint",
            "exhausted_cooldown_and_budget",
        }.issubset(cases))

    def test_release_retention_requires_smoke_first_and_stops_at_25(self):
        plan = planner.build_plan("release")
        retention = step(plan, "retention_soak")
        self.assertIn("real_door_trilogy", retention["prerequisites"])
        self.assertEqual(retention["accepted_floor_count"], 25)
        self.assertEqual(retention["max_floor_requests"], 25)
        self.assertTrue(retention["same_run_required"])
        self.assertTrue(retention["stop_on_first_failed_request"])
        self.assertEqual(retention["hard_cap_seconds"], 900)
        total_floor_requests = sum(item.get("accepted_floor_count", 0)
                                   + item.get("max_additional_floor_requests", 0)
                                   for item in plan["steps"])
        self.assertLessEqual(total_floor_requests, 46)

    def test_expensive_hash_capture_has_its_own_budget(self):
        plan = planner.build_plan("quick")
        self.assertEqual(step(plan, "readonly_context")["hard_cap_seconds"], 20)
        evidence = step(plan, "freeze_evidence")
        self.assertEqual(evidence["hard_cap_seconds"], 180)
        self.assertIn("protected_hashes_and_prior_discrepancies", evidence["preserve"])

    def test_release_has_finite_build_and_package_caps(self):
        plan = planner.build_plan("release")
        for target in ("editor", "development", "shipping"):
            self.assertEqual(step(plan, f"cold_build_{target}")["hard_cap_seconds"], 900)
        for target in ("development", "shipping"):
            package = step(plan, f"cook_package_{target}")
            self.assertEqual(package["hard_cap_seconds"], 2400)
            self.assertEqual(package["no_progress_cap_seconds"], 180)
            self.assertEqual(step(plan, f"packaged_trilogy_{target}")["accepted_floor_count"], 3)

    def test_all_prerequisites_are_earlier_steps_without_dangling_names(self):
        for mode in ("quick", "release"):
            known = set()
            for item in planner.build_plan(mode)["steps"]:
                self.assertNotIn(item["id"], known)
                self.assertTrue(set(item["prerequisites"]).issubset(known))
                self.assertGreater(item["hard_cap_seconds"], 0)
                known.add(item["id"])

    def test_plan_never_claims_execution_gameplay_verification_or_bound_commands(self):
        for mode in ("quick", "release"):
            plan = planner.build_plan(mode)
            self.assertEqual(plan["status"], "PLANNED")
            self.assertFalse(plan["execution_performed"])
            self.assertFalse(plan["gameplay_verified"])
            self.assertFalse(plan["timing_policy"]["hard_caps_enforced_by_this_helper"])
            for item in plan["steps"]:
                self.assertEqual(item["status"], "PLANNED")
                if "runner" in item:
                    self.assertEqual(item["runner"]["status"], "REQUIRED_NOT_IMPLEMENTED")
                    self.assertIsNone(item["runner"]["command"])

    def test_generated_plans_are_deterministic_independent_and_json_serializable(self):
        first = planner.build_plan("quick")
        original = copy.deepcopy(first)
        step(first, "freeze_evidence")["preserve"].append("caller-owned-mutation")
        self.assertEqual(planner.build_plan("quick"), original)
        self.assertEqual(json.loads(json.dumps(original)), original)
        with self.assertRaises(ValueError):
            planner.build_plan("1000_floors")

    def test_cli_stdout_and_explicit_file_output_are_identical(self):
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(planner.main(["plan", "--mode", "quick"]), 0)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "plan.json"
            self.assertEqual(planner.main(["plan", "--mode", "quick", "--output", str(output)]), 0)
            self.assertEqual(output.read_text(encoding="utf-8"), stdout.getvalue())
            self.assertEqual([path.name for path in Path(directory).iterdir()], ["plan.json"])

    def test_cli_rejects_non_json_output_without_creating_it(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "DA_Director.uasset"
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                planner.main(["plan", "--output", str(output)])
            self.assertEqual(caught.exception.code, 2)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
