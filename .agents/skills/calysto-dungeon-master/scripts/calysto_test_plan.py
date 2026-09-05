#!/usr/bin/env python3
"""Emit a bounded Calysto V7 test plan; never run Unreal or certify gameplay.

Only Python's standard library is required. The optional output is a JSON plan,
not a receipt. Missing V7 automation bindings stay explicit until implemented.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


PLAN_SCHEMA_VERSION = 1
DIRECTOR_SCHEMA_VERSION = 7
MCP_ATTEMPT_SECONDS = 10
MCP_MAX_ATTEMPTS = 2
EVIDENCE_CAPTURE_SECONDS = 180
EDITOR_LAUNCH_SECONDS = 180
FLOOR_REQUEST_SECONDS = 30
FLOOR_MAX_ATTEMPTS = 4
ACTIVE_GENERATION_LIMIT = 1
QUICK_FLOOR_COUNT = 3
QUICK_TRILOGY_SECONDS = 180
PROBABILITY_MIN_TRIALS = 100_000
PROBABILITY_COMPUTE_TARGET_SECONDS = 15
PROBABILITY_PROCESS_SECONDS = 180
FOCUSED_COMPILE_SECONDS = 300
FOCUSED_RELEASE_CASES_SECONDS = 600
FOCUSED_RELEASE_MAX_FLOOR_REQUESTS = 12
COLD_BUILD_SECONDS = 900
PACKAGE_SECONDS = 2400
BUILD_NO_PROGRESS_SECONDS = 180
RETENTION_FLOOR_COUNT = 25
RETENTION_SECONDS = 900
OBSERVATION_POLL_SECONDS = 2
USER_UPDATE_SECONDS = 60
FIXTURE_RUN_SEED = 709_202_607
FAILING_TOPOLOGY_SEEDS = (1_779_679_224, 1_190_737_158)
THIRD_TOPOLOGY_SEED = 953_980_453
RUNNER_STATUS = "REQUIRED_NOT_IMPLEMENTED"

PROBABILITY_LAWS = (
    "eligible_style_weights",
    "eligible_content_chance",
    "additional_room_theme_presence",
    "conditional_theme_weights",
    "guaranteed_theme_room_count",
    "conditional_fixed_amount",
    "conditional_uniform_amount",
    "conditional_triangular_amount",
    "continuous_triangular_layout",
    "conditional_container_contents",
    "eligible_entry_weights",
    "populated_rarity_tiers_including_winter",
    "optional_native_decoration_empty",
    "selected_decal_chance",
    "custom_pcg_extension_chance",
)

DETERMINISTIC_CASES = (
    "curve_exact_endpoints_and_linear_interpolation",
    "array_reorder_preserves_stable_decisions",
    "display_name_change_preserves_identity_and_behavior",
    "material_change_preserves_topology",
    "theme_weight_change_preserves_theme_presence_set",
    "empty_catalog_and_all_zero_weights",
    "exhausted_cooldown_and_budget",
    "incompatible_surface_and_unavailable_footprint",
    "no_eligible_room_rejects_before_content_commitment",
    "impossible_amount_excluded_before_roll_without_truncation",
    "rarity_normalizes_populated_eligible_tiers_without_hidden_nothing",
    "disabled_adaptation_has_exactly_zero_influence",
    "inherit_extend_replace_block_and_separate_probability_overrides",
    "floor_limits_apply_across_all_rooms",
    "request_callback_ids_do_not_affect_random_seeds",
)


def _step(
    step_id: str,
    purpose: str,
    hard_cap_seconds: int,
    *,
    prerequisites: tuple[str, ...] = (),
    runner: str | None = None,
    **details: Any,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": step_id,
        "status": "PLANNED",
        "purpose": purpose,
        "hard_cap_seconds": hard_cap_seconds,
        "prerequisites": list(prerequisites),
        "on_timeout": "Stop this step; preserve evidence and mark its gate PENDING or FAIL according to observed evidence.",
    }
    if runner:
        result["runner"] = {"capability": runner, "status": RUNNER_STATUS, "command": None}
    result.update(details)
    return result


def _trilogy() -> dict[str, Any]:
    return _step(
        "real_door_trilogy",
        "Use the real DoorToLevel from HUB, then the real floor doors through three consecutive accepted floors in one run.",
        QUICK_TRILOGY_SECONDS,
        prerequisites=("probability_suite", "affected_blueprint_compile"),
        runner="V7 natural-door traversal and transaction evidence recorder",
        startup_excluded=True,
        start_location="HUB",
        same_run_required=True,
        accepted_floor_count=QUICK_FLOOR_COUNT,
        run_seed=FIXTURE_RUN_SEED,
        seed_override_contract="Development-only initial topology-seed fixture; preserve real doors and normal run progression. Bind and verify a real capability before executing.",
        floors=[
            {
                "floor_number": index,
                "initial_topology_seed": seed,
                "is_failure_regression_seed": seed in FAILING_TOPOLOGY_SEEDS,
                "trigger": "real_DoorToLevel" if index == 1 else "real_floor_door",
                "required_terminal_state": "PlayerReleasedAfterVerifiedCommit",
            }
            for index, seed in enumerate((*FAILING_TOPOLOGY_SEEDS, THIRD_TOPOLOGY_SEED), start=1)
        ],
        each_floor_request={
            "shared_deadline_seconds": FLOOR_REQUEST_SECONDS,
            "max_total_attempts": FLOOR_MAX_ATTEMPTS,
            "includes": ["all_attempts", "pending_observation", "rejected_attempt_cleanup", "commit", "player_release"],
            "pending_consumes_attempt": False,
            "retry_seed": "Deterministically derive from run identity, floor identity, and attempt identity; never from callback request ID.",
            "retry_preserves": ["run", "floor", "selected_style", "rules", "budgets", "pre_floor_companion_and_outcome_state"],
            "no_deadline_reset_on_retry": True,
        },
        after_failure="Stop the trilogy, preserve the protected player and concise Retry / Return to HUB state; no automatic HUB bounce or independent floor restart to manufacture a pass.",
        evidence=[
            "One run identity, increasing accepted floors 1, 2, 3 and real-door origin for each request.",
            "All accepted and rejected attempt identities, topology seeds, timings, cleanup and final result.",
            "Structural floor instances, transforms, collision, nav relevance and actual geometry bounds.",
            "Unique native Start/End, entry blocking floor, capsule clearance, nav registration/tiles and Start-to-End path.",
            "Exactly one root GenerateLocal per attempt and no release before all reservations materialize and verify.",
            "Screenshots of each released floor plus visible traversal and the validated entry transform consumed by EFLevelFlow.",
        ],
    )


def build_plan(mode: str) -> dict[str, Any]:
    """Return a reproducible plan. Clock time, host state and random RNG are unused."""
    if mode not in ("quick", "release"):
        raise ValueError("mode must be quick or release")

    steps = [
        _step(
            "readonly_context",
            "Read live target project, dirty packages and current authority without editor mutation.",
            MCP_ATTEMPT_SECONDS * MCP_MAX_ATTEMPTS,
            max_connection_attempts=MCP_MAX_ATTEMPTS,
            per_connection_attempt_seconds=MCP_ATTEMPT_SECONDS,
            on_unavailable="Confirm the target editor process; mark live context PENDING. Launch only within the authorized execution scope, then rediscover MCP.",
        ),
        _step(
            "freeze_evidence",
            "Capture dirty worktree, protected hashes and historical discrepancies without changing the baseline or discarding editor work.",
            EVIDENCE_CAPTURE_SECONDS,
            prerequisites=("readonly_context",),
            preserve=["dirty_worktree", "V6_migration_input", "protected_hashes_and_prior_discrepancies", "failing_seed_evidence"],
        ),
        _step(
            "editor_available",
            "If needed for authorized execution, launch the target using the protected project launch script and wait for editor readiness.",
            EDITOR_LAUNCH_SECONDS,
            prerequisites=("freeze_evidence",),
            conditional="Skip launch if the correct target editor is already ready.",
            on_timeout="Stop; capture target process and log state. Do not enter an unbounded launch/reconnect loop.",
        ),
        _step(
            "probability_suite",
            "Exercise the actual shared native V7 probability library in memory, without generating world floors.",
            PROBABILITY_PROCESS_SECONDS,
            prerequisites=("freeze_evidence",),
            runner="V7 native probability and deterministic behavioral test suite",
            computation_target_seconds=PROBABILITY_COMPUTE_TARGET_SECONDS,
            requested_world_floor_count=0,
            laws=[{"id": name, "min_trials": PROBABILITY_MIN_TRIALS} for name in PROBABILITY_LAWS],
            deterministic_cases=list(DETERMINISTIC_CASES),
            theme_count_law="1 + Binomial(N - 1, 0.25), N >= 1 eligible rooms",
            expected_themed_fraction="(1 + 0.25 * (N - 1)) / N",
            protected_rooms=["Start", "End", "Critical", "Progression"],
            report_populations=["unconstrained_requested", "feasible_reserved", "accepted_realized"],
            evidence=[
                "Fixed reproducible native seeds, independent mathematical oracles, trial counts and observed distributions for every law.",
                "Presence and conditional weights tested separately; Winter and intentional Empty remain explicit.",
                "Every exposed gameplay setting mapped to a runtime-effect test; an unsupported setting blocks its gate.",
                "Wait for the process to exit; a PASS JSON followed by a crash fails the gate.",
            ],
        ),
        _step(
            "affected_blueprint_compile",
            "Compile affected project-owned Blueprints without saving protected assets.",
            FOCUSED_COMPILE_SECONDS,
            prerequisites=("editor_available",),
            runner="V7 affected-Blueprint compile and protected-save audit",
        ),
        _trilogy(),
    ]

    if mode == "release":
        steps.extend([
            _step(
                "focused_release_cases",
                "Run bounded, risk-based native parity, visual, transaction and gameplay fixtures only after the three-floor smoke succeeds.",
                FOCUSED_RELEASE_CASES_SECONDS,
                prerequisites=("real_door_trilogy",),
                runner="V7 focused integration and failure-injection suite",
                max_additional_floor_requests=FOCUSED_RELEASE_MAX_FLOOR_REQUESTS,
                shared_floor_request_deadline_seconds=FLOOR_REQUEST_SECONDS,
                cases=[
                    "Native structural parity with extensions disabled; all initial Styles and supported size/depth extremes.",
                    "Forge orange, Shrine blue and General grey on actual Floor/Wall/Roof components and shared boundaries.",
                    "All placement zones, native torch effects, protected routes, pooled blood materialization and dependency closure.",
                    "Recruitment, death, recall, inventory travel, chest lockpicking and contents, Theme catalog isolation.",
                    "Delayed geometry/navigation stays Pending; selected-object failure rolls back the entire attempt.",
                    "Cancellation, stale callback, repeated door, same-seed restart, replay, reroll and exhausted recovery.",
                    "Unsupported custom PCG contract rejects before generation; complete all remaining functional controls' tests.",
                    "Cold/warm phase, hitch, memory and PSO measurements; preserve all rejected attempt timings.",
                    "Intended final HUB return through the real terminal-floor door and progression rule; not inferred from the three-floor smoke.",
                ],
                coverage_policy="Use pairwise fixtures and reuse observed floors. If coverage cannot fit the cap, report named uncovered gates PENDING and schedule a reviewed follow-up; never silently expand the loop.",
            ),
            _step(
                "retention_soak",
                "Measure retained actors, components, objects, leases and memory across 25 consecutive floors after successful smoke.",
                RETENTION_SECONDS,
                prerequisites=("real_door_trilogy", "focused_release_cases"),
                runner="V7 bounded floor-retention recorder",
                accepted_floor_count=RETENTION_FLOOR_COUNT,
                same_run_required=True,
                max_floor_requests=RETENTION_FLOOR_COUNT,
                shared_floor_request_deadline_seconds=FLOOR_REQUEST_SECONDS,
                max_attempts_per_request=FLOOR_MAX_ATTEMPTS,
                stop_on_first_failed_request=True,
                evidence="Record counts at the same lifecycle boundary; distinguish stable caching from persistent growth. Include rejected attempts and cleanup in latency data; P95 < 30 s is a measured target, never inferred from this schedule.",
            ),
        ])
        previous = "retention_soak"
        for target in ("editor", "development", "shipping"):
            step_id = f"cold_build_{target}"
            steps.append(_step(
                step_id,
                f"Obtain a successful cold {target.title()} build from the candidate tree using the protected build workflow where applicable.",
                COLD_BUILD_SECONDS,
                prerequisites=(previous,),
                no_progress_cap_seconds=BUILD_NO_PROGRESS_SECONDS,
                runner=f"Validated {target} build binding and process-exit recorder",
            ))
            previous = step_id
        for target in ("development", "shipping"):
            package_id = f"cook_package_{target}"
            steps.append(_step(
                package_id,
                f"Actually cook and produce a fresh {target.title()} package, recording process exit and dependency audit.",
                PACKAGE_SECONDS,
                prerequisites=(previous,),
                no_progress_cap_seconds=BUILD_NO_PROGRESS_SECONDS,
                runner=f"Validated {target} cook/package binding and package audit",
                requires="RealisticBlood entitlement confirmation before distribution; preserve native Calysto torch dependencies.",
            ))
            smoke_id = f"packaged_trilogy_{target}"
            steps.append(_step(
                smoke_id,
                f"Run three consecutive real-door floors in the fresh {target.title()} package and capture visual and process-exit evidence.",
                QUICK_TRILOGY_SECONDS,
                prerequisites=(package_id,),
                runner=f"V7 {target} packaged traversal and visual smoke recorder",
                accepted_floor_count=QUICK_FLOOR_COUNT,
                same_run_required=True,
                shared_floor_request_deadline_seconds=FLOOR_REQUEST_SECONDS,
                max_attempts_per_request=FLOOR_MAX_ATTEMPTS,
                stop_on_first_failed_request=True,
            ))
            previous = smoke_id

    plan = {
        "plan_schema_version": PLAN_SCHEMA_VERSION,
        "director_schema_version": DIRECTOR_SCHEMA_VERSION,
        "mode": mode,
        "status": "PLANNED",
        "gameplay_verified": False,
        "execution_performed": False,
        "scope": "Schedule and evidence requirements only. This helper never invokes Unreal, runs probability trials, compiles, launches, packages, or validates a receipt.",
        "authority_target": "/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector",
        "authority_state": "PENDING_LIVE_AND_FINAL_TREE_VERIFICATION",
        "timing_policy": {
            "units": "seconds",
            "estimates_are_guarantees": False,
            "hard_caps_enforced_by_this_helper": False,
            "future_executor_must_enforce_monotonic_deadlines": True,
            "poll_interval_seconds": OBSERVATION_POLL_SECONDS,
            "user_update_max_interval_seconds": USER_UPDATE_SECONDS,
            "max_active_generation_attempts": ACTIVE_GENERATION_LIMIT,
            "no_progress_definition": "New useful compiler/cook/phase work; heartbeat text alone does not reset the bound.",
            "timeout_cleanup": "Cancel only task-owned work safely, retain logs and stop progression. Never force-kill an editor with unsaved user work or reset a floor deadline to allow cleanup/retry.",
        },
        "seed_policy": {
            "run_seed": FIXTURE_RUN_SEED,
            "regression_topology_seeds": list(FAILING_TOPOLOGY_SEEDS),
            "run_and_topology_seeds_are_distinct_fields": True,
            "callback_request_ids_excluded_from_random_seeds": True,
        },
        "steps": steps,
        "acceptance_requirements": [
            "Bind actual discovered V7 runners and document exact commands; REQUIRED_NOT_IMPLEMENTED is never success.",
            "Each gate records source/target path, symbol/asset, test, logs/screenshots, process exit and commit/snapshot.",
            "Require zero process exit, zero Blueprint errors and no fatal/crash log errors; inspect logs after the process exits.",
            "Re-hash protected invariants without silently rebaselining pre-existing discrepancies.",
            "Separate authoring validity, probability evidence, native integration, gameplay and release verification.",
            "V6 remains immutable migration input until candidate gates pass; no active V6 fallback. Final V7-only tree requires a fresh release plan run after retirement.",
            "Quick completion alone does not establish release readiness. Missing evidence stays PENDING.",
        ],
    }
    return plan


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan_command = commands.add_parser("plan", help="Print or save a deterministic, non-executing JSON schedule.")
    plan_command.add_argument("--mode", choices=("quick", "release"), default="quick")
    plan_command.add_argument("--output", type=Path, help="Optional JSON output file; parent directory must already exist.")
    args = parser.parse_args(argv)
    document = json.dumps(build_plan(args.mode), indent=2, ensure_ascii=True) + "\n"
    if args.output is None:
        print(document, end="")
    else:
        if args.output.suffix.lower() != ".json":
            parser.error("--output must name a .json file")
        try:
            args.output.write_text(document, encoding="utf-8")
        except OSError as error:
            parser.error(f"could not write plan: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
