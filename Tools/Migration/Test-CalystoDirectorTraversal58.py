"""Stdlib checks for traversal receipt validation; this does not execute Unreal or PIE."""
from __future__ import annotations

import ast
import copy
import math
from pathlib import Path
import re
from typing import Any
import unittest

SOURCE = Path(__file__).with_name("Validate-CalystoDirectorTraversal58.py")
TREE = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
PURE = [n for n in TREE.body if isinstance(n, ast.FunctionDef) and n.name in
        {"validate_floor_evidence", "validate_entry_walk_route", "entry_walk_satisfied"}]
SCOPE = {"math": math, "re": re, "Any": Any, "OWNER_REQUEST_SECONDS": 30.0,
         "MAXIMUM_ATTEMPTS": 4, "ENTRY_TOLERANCE_CM": 35.0}
exec(compile(ast.Module(body=PURE, type_ignores=[]), str(SOURCE), "exec"), SCOPE)
VALIDATE = SCOPE["validate_floor_evidence"]
VALIDATE_ROUTE = SCOPE["validate_entry_walk_route"]
WALK_SATISFIED = SCOPE["entry_walk_satisfied"]


def fixture():
    snapshot = {"state": "ready", "floor_number": 1, "last_committed_floor_number": 1,
                "run_epoch": 1, "last_committed_run_epoch": 1, "run_seed": 709202607,
                "reroll_index": 0, "native_floor_verified": True, "gameplay_verified": False,
                "request_elapsed_seconds": 12.0, "attempt_count": 1, "topology_seed": 1779679224}
    diagnostic = {"schema_version": 1, "floor_number": 1, "run_epoch": 1, "run_seed": 709202607,
                  "request_id": "a" * 32, "attempt_id": "b" * 32,
                  "attempts": [{"attempt_id": "b" * 32, "topology_seed": 1779679224,
                                "root_generation_requests": 1, "actual_root_generation_requests": 1,
                                "accepted": True, "cleanup_verified": False}],
                  "entry_location": [1.0, 2.0, 92.0]}
    for key in ("unique_owned_start_and_end", "blocking_floor_and_capsule_clearance",
                "complete_relevant_navigation_route", "room_theme_contract_verified", "materials_verified"):
        diagnostic[key] = True
    return snapshot, diagnostic


class ReceiptChecks(unittest.TestCase):
    def check(self, snapshot, diagnostic, floor=1, mode="native_parity", player=None):
        return VALIDATE(snapshot, diagnostic, floor, mode, [1.0, 2.0, 92.0] if player is None else player)

    def test_valid_fixture_and_finite_recovery(self):
        s, d = fixture()
        self.check(s, d)
        s["attempt_count"] = 2
        rejected = copy.deepcopy(d["attempts"][0])
        rejected.update(attempt_id="c" * 32, accepted=False, cleanup_verified=True, topology_seed=1190737158)
        d["attempts"].insert(0, rejected)
        self.check(s, d)

    def test_missing_native_postconditions_fail(self):
        for key in ("unique_owned_start_and_end", "blocking_floor_and_capsule_clearance",
                    "complete_relevant_navigation_route", "room_theme_contract_verified", "materials_verified", "entry_location"):
            with self.subTest(key=key):
                s, d = fixture()
                del d[key]
                with self.assertRaises(ValueError):
                    self.check(s, d)

    def test_duplicate_generation_missing_cleanup_and_schema_fail(self):
        for mutation in ("duplicate_generation", "missing_cleanup", "bad_guid", "boolean_count", "bad_schema", "wrong_seed"):
            with self.subTest(mutation=mutation):
                s, d = fixture()
                if mutation == "duplicate_generation":
                    d["attempts"][0]["root_generation_requests"] = 2
                elif mutation == "missing_cleanup":
                    s["attempt_count"] = 2
                    rejected = copy.deepcopy(d["attempts"][0])
                    rejected.update(attempt_id="c" * 32, accepted=False, cleanup_verified=False)
                    d["attempts"].insert(0, rejected)
                elif mutation == "bad_guid":
                    d["request_id"] = "-" + "a" * 32
                elif mutation == "boolean_count":
                    d["attempts"][0]["root_generation_requests"] = True
                elif mutation == "bad_schema":
                    d["schema_version"] = True
                elif mutation == "wrong_seed":
                    d["attempts"][0]["topology_seed"] = 1190737158
                with self.assertRaises(ValueError):
                    self.check(s, d)

    def test_duplicates_deadline_and_false_gameplay_claim_fail(self):
        s, d = fixture()
        for floor, mode in ((2, "native_parity"), (1, "full")):
            with self.assertRaises(ValueError):
                self.check(s, d, floor, mode)
        s["request_elapsed_seconds"] = 30.001
        with self.assertRaises(ValueError):
            self.check(s, d)
        s["request_elapsed_seconds"] = 12
        s["gameplay_verified"] = True
        with self.assertRaises(ValueError):
            self.check(s, d)
        self.check(s, d, mode="full")

    def test_authorization_alone_never_proves_native_generation(self):
        for count in (None, 0, 2, True):
            with self.subTest(actual=count):
                s, d = fixture()
                d["attempts"][0]["actual_root_generation_requests"] = count
                with self.assertRaises(ValueError):
                    self.check(s, d)

    def test_moved_player_and_invalid_entry_fail(self):
        s, d = fixture()
        with self.assertRaises(ValueError):
            self.check(s, d, player=[1000.0, 2.0, 92.0])
        d["entry_location"][0] = float("nan")
        with self.assertRaises(ValueError):
            self.check(s, d)

    def test_runner_never_publishes_ready_or_restarts_the_run(self):
        forbidden = {"request_start_new_run", "request_start_new_run_with_seed", "request_advance_floor",
                     "request_retry", "notify_generation_ready", "confirm_player_release", "quit_editor",
                     "editor_request_begin_play", "load_level", "save_asset", "save_current_level"}
        for n in ast.walk(TREE):
            if isinstance(n, ast.Call):
                if isinstance(n.func, ast.Attribute):
                    self.assertNotIn(n.func.attr, forbidden)
                if isinstance(n.func, ast.Name) and n.func.id == "method" and len(n.args) > 1 and isinstance(n.args[1], ast.Constant):
                    self.assertNotIn(n.args[1].value, forbidden)
        constants = {n.targets[0].id: ast.literal_eval(n.value) for n in TREE.body
                     if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
                     and n.targets[0].id in {"TOTAL_DEADLINE_SECONDS", "EXPECTED_FLOORS", "REQUEST_OBSERVATION_SECONDS"}}
        self.assertEqual(constants, {"TOTAL_DEADLINE_SECONDS": 150.0, "EXPECTED_FLOORS": [1, 2, 3], "REQUEST_OBSERVATION_SECONDS": 35.0})

    def test_entry_route_handles_a_corner_without_another_projection(self):
        VALIDATE_ROUTE([[10, 20, 0], [30, 20, 0], [30, 120, 0]], [10, 20, 92], 34)

    def test_entry_route_rejects_short_distant_nonfinite_or_missing_points(self):
        for points in ([], [[0, 0, 0]], [[0, 0, 0], [20, 0, 0]],
                       [[100, 0, 0], [300, 0, 0]], [[0, 0, 0], [float("nan"), 100, 0]]):
            with self.subTest(points=points), self.assertRaises(ValueError):
                VALIDATE_ROUTE(points, [0, 0, 92], 34)

    def test_entry_walk_requires_horizontal_progress_grounded_time_and_real_input(self):
        start = [0, 0, 92]
        self.assertTrue(WALK_SATISFIED(start, [55, 0, 92], 34, 0.4, 20))
        for end, seconds, inputs in ((start, 1, 20), ([0, 0, 200], 1, 20),
                                      ([50, 0, 92], 1, 20), ([55, 0, 92], 0.1, 20),
                                      ([55, 0, 92], 1, 0)):
            with self.subTest(end=end, seconds=seconds, inputs=inputs):
                self.assertFalse(WALK_SATISFIED(start, end, 34, seconds, inputs))
        with self.assertRaises(ValueError):
            WALK_SATISFIED(start, [float("nan"), 0, 92], 34, 1, 20)


if __name__ == "__main__":
    unittest.main()
