"""Build tracked evidence for the exact DirtyPawn asset/runtime-contract batch."""

import csv
import datetime
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = "Docs/Migration/Evidence/Phase4_DirtyPawnAssets_ContentRuntime.json"
COMMIT = "ec10c8e"
BASE = "Saved/Migration/Phase4/DirtyPawnAssets"
SOURCE_BYTES = 70130437
SOURCE_FINGERPRINT = (
    "8F58CFD389DA0D63D2633D372C5DE251496EC53F53EEC15FE51AB160C46AEC36"
)
TARGET_BYTES = 70129199
TARGET_FINGERPRINT = (
    "773CC5A9423B7C250041FF4F2FD4BFBC0EE5725456C26AA6AA244FC0CCF94D34"
)
CLASS_COUNTS = {"Texture2D": 6, "MaterialFunction": 8, "Material": 1}
PACKAGES = {
    "/Game/_Game/Textures/leakage_sfhkcazc_4k/T_Leakage_sfhkcazc_2K_Mask",
    "/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D",
    "/Game/DirtyPawnSystem/Demo/StarterContent/Textures/T_Perlin_Noise_M",
    "/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Blood/MF_Blood_Mask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SandSnow_Mask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SkinnedHeightMask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Mud/MF_MudHeight_BaseColor",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_BaseColor",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Normal",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Roughness",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Smear/MF_Smear_Mask",
    "/Game/DirtyPawnSystem/Textures/T_BloodScratch_Alpha",
    "/Game/DirtyPawnSystem/Textures/T_Noise_Normal",
    "/Game/DirtyPawnSystem/Textures/T_Perlin_Noise_M",
}
EXPECTED_TESTS = {
    "ACFUltimateSample.Defeat.Flow.PIE.CancelledDefeatedSceneRestoresMovement",
    "ACFUltimateSample.Defeat.Flow.PIE.CombatStruggleLose",
    "ACFUltimateSample.Defeat.Flow.PIE.CombatStruggleWin",
    "ACFUltimateSample.Defeat.Flow.PIE.OutOfCombatRecovery",
    "ACFUltimateSample.Defeat.Flow.PIE.RepeatKnockoutSameCombat",
    "ACFUltimateSample.Defeat.Flow.PIE.TravelArrivalFapCancelRestoresMovement",
}
RUNTIME_REPORT = (
    "Saved/Migration/Automation/"
    "Phase4_EFProjectSystems_DefeatPIE_DirtyPawn_20260713_084332/index.json"
)
RUNTIME_LOG = (
    "Saved/Migration/Logs/"
    "Phase4_EFProjectSystems_DefeatPIE_DirtyPawn_20260713_084332.log"
)


def load(relative):
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError("Required evidence is absent: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def require(value, message):
    if not value:
        raise RuntimeError(message)


def read_text_auto(relative):
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError("Required log is absent: " + str(path))
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig", errors="replace")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def target_relative(package):
    return "Content/" + package.removeprefix("/Game/") + ".uasset"


def batch_fingerprint(rows, prefix=""):
    order = sorted(
        rows,
        key=lambda package: (
            0 if package.startswith("/Game/_Game/") else 1,
            package,
        ),
    )
    lines = [
        "{}|{}|{}".format(
            package,
            rows[package][prefix + "length"],
            rows[package][prefix + "sha256"].upper(),
        )
        for package in order
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def package_map(rows, label):
    mapped = {row["package"]: row for row in rows}
    require(
        set(mapped) == PACKAGES and len(rows) == 15,
        label + " package set differs from the exact allowlist",
    )
    return mapped


def validate_source_gate(relative, label):
    gate = load(relative)
    require(
        gate.get("pass") is True
        and gate.get("head_match") is True
        and gate.get("status_match") is True
        and gate.get("modified_file_hashes_match") is True
        and gate.get("lfs_manifest_match") is True,
        label + " source read-only gate failed",
    )
    return gate


def validate_protected_gate(relative, label):
    gate = load(relative)
    require(
        gate.get("result") == "PASS"
        and gate.get("mismatch_count") == 0
        and all(row.get("Result") == "PASS" for row in gate.get("sets", []))
        and all(
            row.get("Result") == "PASS"
            for row in gate.get("authoritative_assets", [])
        ),
        label + " protected invariant gate failed",
    )
    return gate


def validate_safety_gate(stage, expected_target_rows):
    relative = BASE + "/Gates/" + stage + "_SafetyGate.json"
    gate = load(relative)
    source_relative = BASE + "/Gates/" + stage + "_SourceReadOnly.json"
    protected_relative = BASE + "/Gates/" + stage + "_ProtectedInvariants.json"
    validate_source_gate(source_relative, stage)
    validate_protected_gate(protected_relative, stage)
    require(
        gate.get("status") == "DIRTYPAWN_ASSETS_SOURCE_PROTECTED_SAFETY_PASS"
        and gate.get("stage") == stage
        and gate.get("package_count") == 15
        and gate.get("source_bytes") == SOURCE_BYTES
        and gate.get("source_fingerprint") == SOURCE_FINGERPRINT
        and gate.get("source_read_only", {}).get("result") == "PASS"
        and gate.get("protected_invariants", {}).get("result") == "PASS"
        and gate.get("source_tree_mounted") is False
        and gate.get("raw_target_asset_copy_requested") is False
        and gate.get("target_sidecars_absent") is True,
        stage + " combined safety gate failed",
    )
    require(
        gate["source_read_only"]["evidence_sha256"]
        == sha256(ROOT / source_relative)
        and gate["protected_invariants"]["evidence_sha256"]
        == sha256(ROOT / protected_relative),
        stage + " nested evidence hash differs",
    )
    source_rows = package_map(gate.get("source_assets", []), stage + " source")
    target_rows = package_map(gate.get("target_assets", []), stage + " target")
    for package in PACKAGES:
        source = source_rows[package]
        expected = expected_target_rows[package]
        require(
            source.get("length") == expected["source_length"]
            and source.get("sha256") == expected["source_sha256"],
            stage + " source hash differs for " + package,
        )
        target = target_rows[package]
        require(
            target.get("exists") is True
            and target.get("length") == expected["length"]
            and target.get("sha256") == expected["sha256"],
            stage + " target hash differs for " + package,
        )
    return gate


harness = load(BASE + "/DirtyPawnAssets57HarnessReceipt.json")
validation57 = load(BASE + "/DirtyPawnAssets57Validation.json")
migration57 = load(BASE + "/DirtyPawnAssets57Migration.json")
resave58 = load(BASE + "/DirtyPawnAssets58Resave.json")

harness_rows = package_map(harness.get("staged_assets", []), "Harness")
require(
    harness.get("status") == "ISOLATED_DIRTYPAWN_ASSETS57_HARNESS_PASS"
    and harness.get("package_count") == 15
    and harness.get("source_bytes") == SOURCE_BYTES
    and harness.get("source_fingerprint") == SOURCE_FINGERPRINT
    and harness.get("class_counts") == CLASS_COUNTS
    and harness.get("source_package_saves") == 0
    and harness.get("target_content_writes") == 0
    and harness.get("source_tree_mounted") is False
    and harness.get("junctions_or_symlinks_present") is False
    and len(harness.get("wrapper_game_dependencies", [])) == 14
    and set(harness.get("wrapper_game_dependencies", [])).issubset(PACKAGES),
    "Isolated UE 5.7 DirtyPawn harness gate failed",
)
source_rows_for_fingerprint = {
    package: {
        "length": row["length"],
        "sha256": row["sha256"],
    }
    for package, row in harness_rows.items()
}
require(
    batch_fingerprint(source_rows_for_fingerprint) == SOURCE_FINGERPRINT,
    "Harness source fingerprint differs",
)

validation_rows = package_map(validation57.get("packages", []), "UE 5.7 validation")
require(
    validation57.get("status") == "UE57_DIRTYPAWN_ASSETS_READ_ONLY_LOAD_PASS"
    and validation57.get("package_count") == 15
    and validation57.get("source_bytes") == SOURCE_BYTES
    and validation57.get("source_fingerprint") == SOURCE_FINGERPRINT
    and validation57.get("class_counts") == CLASS_COUNTS
    and validation57.get("asset_load_requested") is True
    and validation57.get("asset_save_requested") is False
    and validation57.get("material_compile_requested") is False
    and validation57.get("packages_saved") == 0
    and validation57.get("target_content_writes") == 0
    and validation57.get("source_tree_mounted") is False
    and not validation57.get("redirectors")
    and not validation57.get("unexpected_game_dependencies")
    and not validation57.get("unexpected_registered_packages")
    and not validation57.get("dirty_allowlisted_packages_after_load")
    and set(validation57.get("dependencies", {})) == PACKAGES,
    "UE 5.7 read-only DirtyPawn load gate failed",
)
require(
    {row["class"] for row in validation_rows.values()}
    == {"Texture2D", "MaterialFunction", "Material"},
    "UE 5.7 registered class set differs",
)

migration_rows = package_map(migration57.get("packages", []), "UE 5.7 migration")
require(
    migration57.get("status") == "ASSETTOOLS_EXACT_DIRTYPAWN_ASSETS_MIGRATION_PASS"
    and migration57.get("package_count") == 15
    and migration57.get("created_package_count") == 15
    and migration57.get("source_bytes") == SOURCE_BYTES
    and migration57.get("source_fingerprint") == SOURCE_FINGERPRINT
    and migration57.get("class_counts") == CLASS_COUNTS
    and migration57.get("ignore_dependencies") is True
    and migration57.get("target_delta_exact") is True
    and migration57.get("migration_invoked_this_run") is False
    and migration57.get("resumed_from_prior_log") is True
    and migration57.get("source_assets_unchanged") is True
    and migration57.get("staged_assets_unchanged") is True
    and migration57.get("source_tree_mounted") is False
    and migration57.get("raw_target_asset_copy_requested") is False
    and migration57.get("byte_identical_package_count") == 11
    and migration57.get("reserialized_package_count") == 4
    and len(migration57.get("created_package_files", [])) == 15
    and not migration57.get("modified_existing_package_files")
    and not migration57.get("removed_package_files")
    and not migration57.get("target_sidecars_created")
    and not migration57.get("unexpected_game_dependencies"),
    "UE 5.7 exact AssetTools DirtyPawn migration gate failed",
)
require(
    sum(row["length"] for row in migration_rows.values())
    == migration57.get("target_bytes_after_assettools")
    and batch_fingerprint(migration_rows) == migration57.get(
        "target_fingerprint_after_assettools"
    ),
    "UE 5.7 target package fingerprint differs",
)

resave_rows = package_map(resave58.get("packages", []), "UE 5.8 resave")
dependencies = resave58.get("dependencies_after_resave", {})
require(
    resave58.get("status") == "UE58_DIRTYPAWN_ASSETS_LOAD_COMPILE_RESAVE_RELOAD_PASS"
    and resave58.get("package_count") == 15
    and resave58.get("source_bytes") == SOURCE_BYTES
    and resave58.get("source_fingerprint") == SOURCE_FINGERPRINT
    and resave58.get("target_bytes_after_resave") == TARGET_BYTES
    and resave58.get("target_fingerprint_after_resave") == TARGET_FINGERPRINT
    and resave58.get("class_counts") == CLASS_COUNTS
    and resave58.get("material_compile_count") == 1
    and resave58.get("material_function_compile_count") == 8
    and resave58.get("material_compile_api_result") == "NO_EXCEPTION"
    and len(resave58.get("material_compile_operations", [])) == 9
    and all(
        row.get("result") == "NO_EXCEPTION"
        for row in resave58.get("material_compile_operations", [])
    )
    and resave58.get("asset_reload_requested") is True
    and resave58.get("target_delta_exact") is True
    and len(resave58.get("modified_package_files", [])) == 15
    and not resave58.get("created_package_files")
    and not resave58.get("removed_package_files")
    and not resave58.get("target_sidecars_created")
    and not resave58.get("redirectors")
    and not resave58.get("unexpected_game_dependencies")
    and not resave58.get("dirty_allowlisted_packages_after_reload")
    and resave58.get("pie_requested") is False
    and resave58.get("visual_qa_requested") is False
    and resave58.get("texture_validation_count") == 6
    and resave58.get("blood_texture_dimensions") == [2048, 2048]
    and resave58.get("blood_texture_alpha_gate")
    == "PASS_DIMENSIONS_ALPHA_PENDING_API_UNAVAILABLE"
    and set(dependencies) == PACKAGES,
    "UE 5.8 DirtyPawn load/compile/resave/reload gate failed",
)
require(
    batch_fingerprint(resave_rows) == TARGET_FINGERPRINT,
    "UE 5.8 target fingerprint differs",
)
for package, direct_dependencies in dependencies.items():
    require(
        not [
            value
            for value in direct_dependencies
            if value.startswith("/Game/") and value not in PACKAGES
        ],
        "Unexpected /Game dependency after resave for " + package,
    )
    row = resave_rows[package]
    require(
        row["source_length"] == harness_rows[package]["length"]
        and row["source_sha256"] == harness_rows[package]["sha256"]
        and row["migration_length"] == migration_rows[package]["length"]
        and row["migration_sha256"] == migration_rows[package]["sha256"],
        "Cross-phase package receipt differs for " + package,
    )
    target = ROOT / target_relative(package)
    require(target.is_file(), "Migrated target asset is absent: " + str(target))
    require(
        target.stat().st_size == row["length"] and sha256(target) == row["sha256"],
        "Current target differs from UE 5.8 evidence: " + package,
    )

post_migration_gate = validate_safety_gate("POST_MIGRATION57", migration_rows)
post_resave_gate = validate_safety_gate("POST_RESAVE58", resave_rows)

content_logs = {
    "validation57": "Saved/Migration/Logs/Phase4_DirtyPawnAssets57_Validation.log",
    "migration57": "Saved/Migration/Logs/Phase4_DirtyPawnAssets57_Migration_Resume.log",
    "resave58": "Saved/Migration/Logs/Phase4_DirtyPawnAssets58_Resave.log",
}
markers = {
    "validation57": "CODEX_DIRTYPAWN_ASSETS57_VALIDATION_PASS",
    "migration57": "CODEX_DIRTYPAWN_ASSETS57_MIGRATION_PASS",
    "resave58": "CODEX_DIRTYPAWN_ASSETS58_RESAVE_PASS",
}
for label, relative in content_logs.items():
    log_text = read_text_auto(relative)
    require(
        markers[label] in log_text
        and "Success - 0 error(s)" in log_text
        and "LogPythonScriptCommandlet: Error" not in log_text,
        label + " log does not contain a clean PASS",
    )

resave_log = read_text_auto(content_logs["resave58"])
material_shader_error_patterns = [
    r"LogMaterial(?:Compiler)?: Error:",
    r"LogShaderCompilers?: Error:",
    r"LogShaders?: Error:",
    r"Failed to compile (?:Material|Shader)",
    r"\b(?:Material|Shader) compilation failed\b",
]
material_shader_errors = sum(
    len(re.findall(pattern, resave_log, flags=re.IGNORECASE))
    for pattern in material_shader_error_patterns
)
require(material_shader_errors == 0, "UE 5.8 material/shader log errors are present")

prior_migration_log = "Saved/Migration/Logs/Phase4_DirtyPawnAssets57_Migration.log"
prior_text = read_text_auto(prior_migration_log)
require(
    prior_text.count("AssetTools: Package (/Game/") == 15
    and prior_text.count("was migrated successfully as (/Game/") == 15
    and prior_text.count("CODEX_DIRTYPAWN_ASSETS57_TARGET_DELTA_EXACT_JSON:") == 1
    and prior_text.count(
        "UE 5.7 AssetTools output differs from staged source: "
        "/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D"
    )
    >= 1,
    "Initial UE 5.7 AssetTools log does not prove the exact prior hash-stop",
)

build_logs = {
    "editor": "Saved/Migration/Logs/Phase4_DirtyPawnAssets_EditorBuild58.log",
    "game": "Saved/Migration/Logs/Phase4_DirtyPawnAssets_GameBuild58.log",
}
for label, relative in build_logs.items():
    require(
        "Result: Succeeded" in read_text_auto(relative),
        label + " build did not report success",
    )

runtime_report = load(RUNTIME_REPORT)
runtime_tests = runtime_report.get("tests", [])
require(
    runtime_report.get("succeeded") == 0
    and runtime_report.get("succeededWithWarnings") == 6
    and runtime_report.get("failed") == 0
    and runtime_report.get("notRun") == 0
    and runtime_report.get("inProcess") == 0
    and len(runtime_tests) == 6
    and {row.get("fullTestPath") for row in runtime_tests} == EXPECTED_TESTS
    and all(row.get("state") == "Success" for row in runtime_tests),
    "Focused DirtyPawn runtime automation report is not the exact 6-test PASS",
)

runtime_log = read_text_auto(RUNTIME_LOG)
completed_success_count = len(
    re.findall(r"Test Completed\. Result=\{Success\}", runtime_log)
)
ready_bindings_count = len(
    re.findall(
        r"\[DirtyPawnRuntime\] Ready owner=.*? bindings=6\b", runtime_log
    )
)
wrapper_missing_count = runtime_log.count(
    "[DirtyPawnRuntime] Wrapper material missing:"
)
dirty_pawn_runtime_errors = len(
    re.findall(
        r"\[DirtyPawnRuntime\].*(?:missing|failed|error)",
        runtime_log,
        flags=re.IGNORECASE,
    )
)
lifecycle_error_patterns = [
    r"Log(?:World|PlayLevel|PIE): Error:",
    r"World.*(?:not cleaned|leak)",
    r"Object.*leak",
    r"Fatal error",
    r"Ensure condition failed",
]
lifecycle_error_count = sum(
    len(re.findall(pattern, runtime_log, flags=re.IGNORECASE))
    for pattern in lifecycle_error_patterns
)
morph_physics_warning_count = len(
    re.findall(
        r"EF Morph Physics Constraint Driver .*could not find constraint",
        runtime_log,
    )
)
require(
    completed_success_count == 6
    and ready_bindings_count == 6
    and wrapper_missing_count == 0
    and dirty_pawn_runtime_errors == 0
    and lifecycle_error_count == 0,
    "Focused DirtyPawn runtime log contract failed",
)
require(
    morph_physics_warning_count > 0,
    "Expected Phase 7 morph-physics warnings were not observed/classified",
)

manifest_path = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
with manifest_path.open("r", encoding="utf-8-sig", newline="") as handle:
    manifest_rows = list(csv.DictReader(handle))
selected = [row for row in manifest_rows if row.get("TestEvidence") == EVIDENCE_PATH]
selected_by_package = {row["PackageName"]: row for row in selected}
require(
    len(selected) == 15 and set(selected_by_package) == PACKAGES,
    "Manifest does not contain exactly the 15 DirtyPawn evidence rows",
)
for package, row in selected_by_package.items():
    evidence_row = resave_rows[package]
    direct_dependencies = set(dependencies[package])
    require(
        row.get("TargetPath") == package
        and row.get("TargetFile").replace("\\", "/") == target_relative(package)
        and row.get("TargetAssetClass") == evidence_row["class"]
        and row.get("Presence") == "BOTH"
        and row.get("TargetLength") == str(evidence_row["length"])
        and row.get("TargetSHA256") == evidence_row["sha256"]
        and row.get("TargetRegistryPresent") == "True"
        and row.get("TargetDependencyCount") == str(len(direct_dependencies))
        and set(filter(None, row.get("TargetDependencies", "").split(";")))
        == direct_dependencies
        and row.get("Classification") == "MIGRATED_PROJECT_CONTENT"
        and row.get("Authority") == "SOURCE_BEHAVIOR"
        and row.get("Action")
        == "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
        and row.get("Result") == "PASS"
        and row.get("Commit") == COMMIT,
        "Manifest DirtyPawn disposition differs for " + package,
    )

report_timestamp = datetime.datetime.strptime(
    runtime_report["reportCreatedOn"], "%Y.%m.%d-%H.%M.%S"
).replace(tzinfo=datetime.timezone.utc)
package_evidence = []
for package in sorted(PACKAGES):
    row = resave_rows[package]
    package_evidence.append(
        {
            "package": package,
            "class": row["class"],
            "source_length": row["source_length"],
            "source_sha256": row["source_sha256"],
            "target_length": row["length"],
            "target_sha256": row["sha256"],
            "target_dependencies": sorted(set(dependencies[package])),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": report_timestamp.isoformat(),
    "content_commit": COMMIT,
    "status": "CONTENT_BUILD_RUNTIME_BINDING_CONTRACT_PASS_VISUAL_ALPHA_COOK_PACKAGE_PENDING",
    "scope": {
        "source_packages_migrated": 15,
        "source_bytes": SOURCE_BYTES,
        "source_fingerprint": SOURCE_FINGERPRINT,
        "target_bytes": TARGET_BYTES,
        "target_fingerprint": TARGET_FINGERPRINT,
        "class_counts": CLASS_COUNTS,
        "material_functions_compiled": 8,
        "materials_compiled": 1,
        "textures_loaded": 6,
        "redirectors": 0,
        "unexpected_game_dependencies": 0,
    },
    "gates": {
        "isolated_ue57_harness": harness["status"],
        "ue57_read_only_load": validation57["status"],
        "ue57_assettools_exact_migration": migration57["status"],
        "ue57_prior_hash_stop_recovered": "PASS",
        "ue58_load_compile_resave_reload": resave58["status"],
        "ue58_material_shader_log_errors": material_shader_errors,
        "target_delta_exact": True,
        "editor_build": "PASS",
        "game_build": "PASS",
        "source_read_only_after_ue57": "PASS",
        "protected_target_invariants_after_ue57": "PASS",
        "source_read_only_after_ue58": "PASS",
        "protected_target_invariants_after_ue58": "PASS",
        "focused_runtime_binding_contract": "PASS_WITH_EXPECTED_WARNINGS",
    },
    "runtime_contract": {
        "scope": "Focused Defeat-flow PIE integration and DirtyPawn wrapper/material binding readiness; not effect visual acceptance.",
        "automation_succeeded": 0,
        "automation_succeeded_with_warnings": 6,
        "automation_failed": 0,
        "automation_not_run": 0,
        "test_result_success_count": completed_success_count,
        "tests": sorted(EXPECTED_TESTS),
        "wrapper_material_missing_count": wrapper_missing_count,
        "ready_bindings_6_count": ready_bindings_count,
        "dirty_pawn_runtime_error_count": dirty_pawn_runtime_errors,
        "pie_lifecycle_error_count": lifecycle_error_count,
        "morph_physics_warning_count": morph_physics_warning_count,
        "morph_physics_warning_disposition": "PENDING_PHASE7_PLAYER_DAZ_MORPH_PHYSICS",
    },
    "texture_limits": {
        "blood_texture_dimensions": [2048, 2048],
        "blood_texture_alpha": "PENDING_API_AND_VISUAL_CONFIRMATION",
        "note": "The UE 5.8 commandlet proved dimensions only; it did not promote alpha-channel or rendered blood acceptance.",
    },
    "packages": package_evidence,
    "process_notes": [
        "The first UE 5.7 AssetTools run migrated exactly 15 packages, then stopped at an invalid byte-identity assertion after four assets were legitimately reserialized.",
        "The resume path verified the exact prior 15-package target delta and hash-stop provenance before UE 5.8 resave.",
        "The focused runtime contract proves wrapper resolution and six material bindings in each of six successful Defeat-flow tests; it is not visual DirtyPawn acceptance.",
    ],
    "pending": [
        "visible wet/water/wash QA",
        "visible mud QA and persistence sequence",
        "visible blood and smear QA",
        "visible snow, sand, and dirt/burn QA",
        "DirtyPawn wrapper compatibility with the project tattoo paths",
        "blood texture alpha-channel API and rendered-alpha confirmation",
        "Phase 7 authoritative Female/Player morph-physics constraint audit",
        "cook and cooked-manifest validation for all 15 packages",
        "package",
        "packaged runtime",
    ],
    "evidence": {
        "harness": BASE + "/DirtyPawnAssets57HarnessReceipt.json",
        "validation57": BASE + "/DirtyPawnAssets57Validation.json",
        "migration57": BASE + "/DirtyPawnAssets57Migration.json",
        "resave58": BASE + "/DirtyPawnAssets58Resave.json",
        "post_migration57_gate": BASE + "/Gates/POST_MIGRATION57_SafetyGate.json",
        "post_resave58_gate": BASE + "/Gates/POST_RESAVE58_SafetyGate.json",
        "content_logs": content_logs,
        "prior_migration_log": prior_migration_log,
        "build_logs": build_logs,
        "runtime_report": RUNTIME_REPORT,
        "runtime_log": RUNTIME_LOG,
    },
}

output = ROOT / EVIDENCE_PATH
output.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print("PHASE4_DIRTYPAWN_ASSETS_EVIDENCE_PASS", output)
