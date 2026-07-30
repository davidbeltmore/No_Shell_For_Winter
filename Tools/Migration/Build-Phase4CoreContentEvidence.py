"""Consolidate the Phase 4 core-content/config/build evidence into a tracked receipt."""

import csv
import datetime
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError("Required evidence is absent: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def require(value, message):
    if not value:
        raise RuntimeError(message)


def read_text_auto(path):
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig", errors="replace")


core57 = load("Saved/Migration/Phase4/EFProjectCoreContent57Migration.json")
core57_validation = load(
    "Saved/Migration/Phase4/EFProjectCoreContent57Validation.json"
)
core58 = load("Saved/Migration/Phase4/EFProjectCoreContent58Resave.json")
preview58 = load(
    "Saved/Migration/Phase4/CharacterBackgroundPreviewImport58.json"
)
settings58 = load("Saved/Migration/Phase4/EFProjectCoreSettings58.json")
defeat57 = load("Saved/Migration/Phase4/DefeatVisualMigration57.json")
defeat58 = load("Saved/Migration/Phase4/DefeatVisualResave58.json")
probe58 = load("Saved/Migration/Phase3/EFProjectSystemsReadOnlyProbe58.json")
source_gate = load("Saved/Migration/Evidence/SourceReadOnlyVerification.json")
protected_gate = load(
    "Saved/Migration/Evidence/ProtectedInvariantVerification.json"
)
automation = load(
    "Saved/Migration/Automation/Phase4_EFProjectSystems_CoreContent_20260713_corecontent_clean/index.json"
)

require(
    core57.get("status") == "ASSETTOOLS_EXACT_BATCH_MIGRATION_PASS"
    and core57.get("created_package_count") == 19,
    "Core UE 5.7 AssetTools gate is not 19/19 PASS",
)
require(
    core57_validation.get("status")
    == "UE57_POST_MIGRATION_READ_ONLY_VALIDATION_PASS"
    and core57_validation.get("package_count") == 19,
    "Core UE 5.7 read-only validation is not 19/19 PASS",
)
require(
    core58.get("status") == "UE58_LOAD_COMPILE_CONTRACT_RESAVE_PASS"
    and core58.get("package_count") == 19
    and not core58.get("redirectors"),
    "Core UE 5.8 load/compile/resave gate failed",
)
require(
    preview58.get("status") == "UE58_ASSETTOOLS_TEXTURE_IMPORT_PASS",
    "Generated preview Texture2D gate failed",
)
require(
    settings58.get("status") == "UE58_EFFECTIVE_CORE_SETTINGS_PASS",
    "Effective core settings gate failed",
)
require(
    defeat57.get("status")
    == "ASSETTOOLS_EXACT_DEFEAT_VISUAL_MIGRATION_PASS"
    and defeat57.get("created_package_count") == 12,
    "Defeat UE 5.7 AssetTools gate is not 12/12 PASS",
)
require(
    defeat58.get("status")
    == "UE58_DEFEAT_VISUAL_LOAD_COMPILE_RESAVE_PASS"
    and defeat58.get("package_count") == 12
    and defeat58.get("registered_package_count") == 13,
    "Defeat UE 5.8 load/compile/resave gate failed",
)
require(
    automation.get("succeeded") == 9
    and automation.get("succeededWithWarnings") == 0
    and automation.get("failed") == 0
    and automation.get("notRun") == 0,
    "Core-content Automation gate is not 9 clean successes",
)
require(source_gate.get("pass") is True, "Source read-only gate failed")
require(
    protected_gate.get("result") == "PASS"
    and protected_gate.get("mismatch_count") == 0,
    "Protected target invariant gate failed",
)
require(
    probe58.get("status")
    == "READ_ONLY_STRUCTURAL_PROBE_PASS_SOFT_ASSETS_PENDING"
    and probe58.get("soft_assets", {}).get("existing_count") == 9
    and probe58.get("soft_assets", {}).get("missing_count") == 2,
    "EFProjectSystems post-content structural probe does not report 9/11",
)

build_logs = {
    "editor": "Saved/Migration/Logs/Phase4_CoreContent_DefeatVisual_EditorBuild58.log",
    "game": "Saved/Migration/Logs/Phase4_CoreContent_DefeatVisual_GameBuild58.log",
}
for label, relative in build_logs.items():
    text = read_text_auto(ROOT / relative)
    require("Result: Succeeded" in text, label + " build did not report success")

runtime_source = (
    ROOT
    / "Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/RuntimePerformance/ProjectRuntimePerformanceSubsystem.cpp"
).read_text(encoding="utf-8-sig")
stale_paths = (
    "T_Struggle_Cursor",
    "T_Struggle_Spark",
    "T_Struggle_Vignette",
)
canonical_paths = (
    "FF_Cinzel",
    "FF_BarlowSemiCondensed",
    "FF_BarlowSemiCondensedMedium",
    "T_Struggle_TopPanel",
    "T_Struggle_MainPanel",
    "T_Struggle_TargetChamber",
    "T_Struggle_TargetRing",
    "T_Struggle_TargetPulse",
    "T_Struggle_Arrow",
    "T_Struggle_GlowStreak",
    "T_Struggle_Noise",
    "T_Struggle_BackdropVignette",
)
require(
    not any(path in runtime_source for path in stale_paths),
    "Stale Defeat preload paths remain in project-owned C++",
)
require(
    all(path in runtime_source for path in canonical_paths),
    "Canonical Defeat preload set is incomplete",
)

manifest_path = ROOT / "Docs/Migration/04_Content_Migration_Manifest.csv"
with manifest_path.open("r", encoding="utf-8-sig", newline="") as handle:
    manifest_rows = {row["PackageName"]: row for row in csv.DictReader(handle)}
migrated_rows = [
    row
    for row in manifest_rows.values()
    if row.get("TestEvidence")
    == "Docs/Migration/Evidence/Phase4_EFProjectCoreContent_ConfigAutomation.json"
    and row.get("Classification") == "MIGRATED_PROJECT_CONTENT"
]
generated_rows = [
    row
    for row in manifest_rows.values()
    if row.get("TestEvidence")
    == "Docs/Migration/Evidence/Phase4_EFProjectCoreContent_ConfigAutomation.json"
    and row.get("Classification") == "TARGET_GENERATED_PROJECT_CONTENT"
]
require(len(migrated_rows) == 31, "Manifest does not contain 31 migrated rows")
require(len(generated_rows) == 1, "Manifest does not contain one generated row")
require(
    all(row.get("Result") == "PASS" for row in (*migrated_rows, *generated_rows)),
    "A Phase 4 manifest row is not PASS",
)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "CONTENT_CONFIG_BUILD_AUTOMATION_PASS_PIE_VISUAL_COOK_PACKAGE_PENDING",
    "scope": {
        "source_packages_migrated": 31,
        "target_texture_packages_generated": 1,
        "raw_files_verified": 1,
        "widget_blueprints_compiled": 2,
        "defeat_visual_registered_packages": 13,
    },
    "gates": {
        "ue57_core_assettools": core57["status"],
        "ue57_core_read_only_validation": core57_validation["status"],
        "ue58_core_resave": core58["status"],
        "ue58_preview_texture_import": preview58["status"],
        "ue58_effective_settings": settings58["status"],
        "ue57_defeat_visual_assettools": defeat57["status"],
        "ue58_defeat_visual_resave": defeat58["status"],
        "editor_build": "PASS",
        "game_build": "PASS",
        "automation": {
            "succeeded": automation["succeeded"],
            "succeeded_with_warnings": automation["succeededWithWarnings"],
            "failed": automation["failed"],
            "not_run": automation["notRun"],
        },
        "source_read_only": "PASS",
        "protected_target_invariants": "PASS",
        "structural_soft_assets": {
            "existing": probe58["soft_assets"]["existing_count"],
            "total": probe58["soft_assets"]["total"],
            "missing": [
                row["package"]
                for row in probe58["soft_assets"]["records"]
                if not row["exists"]
            ],
        },
        "stale_defeat_preload_paths_removed": True,
    },
    "release_values": {
        "pain_per_applied_damage": settings58["defeat"][
            "pain_per_applied_damage"
        ],
        "preview_texture": settings58["character_background"][
            "preview_image_texture"
        ],
    },
    "pending": [
        "PIE runtime scenarios",
        "visible visual QA",
        "cook",
        "package",
        "packaged runtime",
        "DoorToLevel",
        "DungeonGeneration",
    ],
    "evidence": {
        "core_migration57": "Saved/Migration/Phase4/EFProjectCoreContent57Migration.json",
        "core_validation57": "Saved/Migration/Phase4/EFProjectCoreContent57Validation.json",
        "core_resave58": "Saved/Migration/Phase4/EFProjectCoreContent58Resave.json",
        "preview_import58": "Saved/Migration/Phase4/CharacterBackgroundPreviewImport58.json",
        "settings58": "Saved/Migration/Phase4/EFProjectCoreSettings58.json",
        "defeat_migration57": "Saved/Migration/Phase4/DefeatVisualMigration57.json",
        "defeat_resave58": "Saved/Migration/Phase4/DefeatVisualResave58.json",
        "automation": "Saved/Migration/Automation/Phase4_EFProjectSystems_CoreContent_20260713_corecontent_clean/index.json",
        "build_logs": build_logs,
        "source_gate": "Saved/Migration/Evidence/SourceReadOnlyVerification.json",
        "protected_gate": "Saved/Migration/Evidence/ProtectedInvariantVerification.json",
    },
}

output = (
    ROOT
    / "Docs/Migration/Evidence/Phase4_EFProjectCoreContent_ConfigAutomation.json"
)
output.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
)
print("PHASE4_CORE_CONTENT_EVIDENCE_PASS", output)
