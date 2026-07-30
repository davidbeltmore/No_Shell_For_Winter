"""Consolidate the exact Phase 4 modern UI migration into a tracked receipt."""

import csv
import datetime
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = "Docs/Migration/Evidence/Phase4_ModernUI_ConfigBuild.json"
ROOT_COUNTS = {
    "/Game/_Game/Widgets/Chronicle": 23,
    "/Game/_Game/Widgets/InnerState": 24,
    "/Game/_Game/Widgets/Status": 17,
    "/Game/_Game/Widgets/Attributes": 27,
    "/Game/_Game/Widgets/SinfulAscensionAltar": 36,
}


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


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


harness = load("Saved/Migration/Phase4/ModernUI57HarnessReceipt.json")
validation57 = load("Saved/Migration/Phase4/ModernUI57Validation.json")
migration57 = load("Saved/Migration/Phase4/ModernUI57Migration.json")
resave58 = load("Saved/Migration/Phase4/ModernUI58Resave.json")
source_gate = load("Saved/Migration/Evidence/SourceReadOnlyVerification.json")
protected_gate = load(
    "Saved/Migration/Evidence/ProtectedInvariantVerification.json"
)
native_automation = load(
    "Saved/Migration/Automation/Phase3_EFProjectSystems_Native_20260713_modernui/index.json"
)
core_content_automation = load(
    "Saved/Migration/Automation/Phase4_EFProjectSystems_CoreContent_20260713_modernui/index.json"
)

require(
    harness.get("status") == "ISOLATED_MODERN_UI_HARNESS_PASS"
    and harness.get("expected_package_count") == 127
    and harness.get("expected_source_bytes") == 12370672
    and harness.get("root_counts") == ROOT_COUNTS,
    "UE 5.7 modern UI harness gate failed",
)
require(
    validation57.get("status")
    == "UE57_MODERN_UI_READ_ONLY_LOAD_COMPILE_PASS"
    and validation57.get("package_count") == 127
    and validation57.get("compiled_widget_blueprint_count") == 66
    and validation57.get("unique_native_parent_count") == 52
    and validation57.get("asset_save_requested") is False,
    "UE 5.7 modern UI read-only compile gate failed",
)
require(
    migration57.get("status")
    == "ASSETTOOLS_EXACT_MODERN_UI_MIGRATION_PASS"
    and migration57.get("package_count") == 127
    and migration57.get("root_counts") == ROOT_COUNTS
    and migration57.get("class_counts")
    == {"WidgetBlueprint": 66, "Texture2D": 54, "FontFace": 7}
    and not migration57.get("unexpected_game_dependencies"),
    "UE 5.7 exact AssetTools migration gate failed",
)
require(
    resave58.get("status") == "UE58_MODERN_UI_LOAD_COMPILE_RESAVE_PASS"
    and resave58.get("package_count") == 127
    and resave58.get("compiled_widget_blueprint_count") == 66
    and resave58.get("root_counts") == ROOT_COUNTS
    and resave58.get("class_counts")
    == {"WidgetBlueprint": 66, "Texture2D": 54, "FontFace": 7}
    and not resave58.get("unexpected_game_dependencies")
    and not resave58.get("redirectors"),
    "UE 5.8 modern UI compile/resave gate failed",
)
native_parents = {
    row["parent_class"] for row in resave58["compiled_widget_blueprints"]
}
require(
    len(native_parents) == 52
    and all(
        path.startswith("/Script/EFProjectSystemsGameplay.")
        for path in native_parents
    ),
    "UE 5.8 native-parent set is not the expected 52-class project set",
)
require(source_gate.get("pass") is True, "Source read-only gate failed")
require(
    protected_gate.get("result") == "PASS"
    and protected_gate.get("mismatch_count") == 0,
    "Protected target invariant gate failed",
)
require(
    native_automation.get("succeeded") == 71
    and native_automation.get("succeededWithWarnings") == 0
    and native_automation.get("failed") == 0
    and native_automation.get("notRun") == 0,
    "Post-modern-UI native Automation gate is not 71 clean successes",
)
require(
    core_content_automation.get("succeeded") == 9
    and core_content_automation.get("succeededWithWarnings") == 0
    and core_content_automation.get("failed") == 0
    and core_content_automation.get("notRun") == 0,
    "Core-content Automation gate is not 9 clean successes",
)

build_logs = {
    "editor": "Saved/Migration/Logs/Phase4_ModernUI_EditorBuild58.log",
    "game": "Saved/Migration/Logs/Phase4_ModernUI_GameBuild58.log",
}
for label, relative in build_logs.items():
    require(
        "Result: Succeeded" in read_text_auto(ROOT / relative),
        label + " build did not report success",
    )

content_logs = {
    "validation57": "Saved/Migration/Logs/Phase4_ModernUI57_Validation.log",
    "migration57": "Saved/Migration/Logs/Phase4_ModernUI57_Migration.log",
    "resave58": "Saved/Migration/Logs/Phase4_ModernUI58_Resave.log",
}
content_markers = {
    "validation57": "CODEX_MODERN_UI57_VALIDATION_PASS",
    "migration57": "CODEX_MODERN_UI57_PASS",
    "resave58": "CODEX_MODERN_UI58_PASS",
}
for label, relative in content_logs.items():
    log_text = read_text_auto(ROOT / relative)
    require(
        content_markers[label] in log_text
        and "Success - 0 error(s)" in log_text
        and "LogBlueprint: Error" not in log_text
        and not ("Compile of " in log_text and " failed" in log_text),
        label + " log does not contain a clean PASS",
    )

default_game = (ROOT / "Config" / "DefaultGame.ini").read_text(
    encoding="utf-8-sig"
)
cook_line = '+DirectoriesToAlwaysCook=(Path="/Game/_Game/Widgets")'
require(
    default_game.count(cook_line) == 1,
    "DefaultGame.ini does not contain exactly one modern UI always-cook root",
)

manifest_path = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
with manifest_path.open("r", encoding="utf-8-sig", newline="") as handle:
    manifest_rows = list(csv.DictReader(handle))
selected = [
    row
    for row in manifest_rows
    if row.get("TestEvidence") == EVIDENCE_PATH
]
require(len(selected) == 127, "Manifest does not contain 127 modern UI rows")
actual_root_counts = {
    root: sum(
        1 for row in selected if row["PackageName"].startswith(root + "/")
    )
    for root in ROOT_COUNTS
}
require(
    actual_root_counts == ROOT_COUNTS,
    "Manifest modern UI root counts differ from the audited allowlist",
)
manifest_class_counts = {}
for row in selected:
    asset_class = row["TargetAssetClass"]
    manifest_class_counts[asset_class] = (
        manifest_class_counts.get(asset_class, 0) + 1
    )
require(
    manifest_class_counts
    == {"WidgetBlueprint": 66, "Texture2D": 54, "FontFace": 7},
    "Manifest modern UI class counts differ from the validated target",
)
for row in selected:
    require(
        row.get("Presence") == "BOTH"
        and row.get("Classification") == "MIGRATED_PROJECT_CONTENT"
        and row.get("Result") == "PASS",
        "Modern UI manifest disposition is not PASS for " + row["PackageName"],
    )
    target_path = ROOT / Path(row["TargetFile"].replace("\\", "/"))
    require(target_path.is_file(), "Manifest target is absent: " + str(target_path))
    require(
        str(target_path.stat().st_size) == row["TargetLength"]
        and sha256(target_path) == row["TargetSHA256"],
        "Manifest target hash differs for " + row["PackageName"],
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "CONTENT_CONFIG_BUILD_PASS_PIE_VISUAL_COOK_PACKAGE_PENDING",
    "scope": {
        "source_packages_migrated": 127,
        "source_bytes": 12370672,
        "widget_blueprints_compiled": 66,
        "unique_native_parents": 52,
        "textures": 54,
        "font_faces": 7,
        "root_counts": ROOT_COUNTS,
    },
    "gates": {
        "isolated_ue57_harness": harness["status"],
        "ue57_read_only_load_compile": validation57["status"],
        "ue57_assettools_exact_migration": migration57["status"],
        "ue58_load_compile_resave": resave58["status"],
        "external_game_dependencies": 0,
        "redirectors": 0,
        "editor_build": "PASS",
        "game_build": "PASS",
        "automation": {
            "native_succeeded": 71,
            "core_content_succeeded": 9,
            "total_succeeded": 80,
            "succeeded_with_warnings": 0,
            "failed": 0,
            "not_run": 0,
        },
        "source_read_only": "PASS",
        "protected_target_invariants": "PASS",
        "always_cook_config_present": True,
    },
    "pending": [
        "PIE input and runtime scenarios",
        "visible visual QA",
        "cook manifest validation for all 127 packages",
        "package",
        "packaged runtime",
    ],
    "evidence": {
        "harness": "Saved/Migration/Phase4/ModernUI57HarnessReceipt.json",
        "validation57": "Saved/Migration/Phase4/ModernUI57Validation.json",
        "migration57": "Saved/Migration/Phase4/ModernUI57Migration.json",
        "resave58": "Saved/Migration/Phase4/ModernUI58Resave.json",
        "content_logs": content_logs,
        "build_logs": build_logs,
        "source_gate": "Saved/Migration/Evidence/SourceReadOnlyVerification.json",
        "protected_gate": "Saved/Migration/Evidence/ProtectedInvariantVerification.json",
        "native_automation": "Saved/Migration/Automation/Phase3_EFProjectSystems_Native_20260713_modernui/index.json",
        "core_content_automation": "Saved/Migration/Automation/Phase4_EFProjectSystems_CoreContent_20260713_modernui/index.json",
    },
}

output = ROOT / EVIDENCE_PATH
output.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
)
print("PHASE4_MODERN_UI_EVIDENCE_PASS", output)
