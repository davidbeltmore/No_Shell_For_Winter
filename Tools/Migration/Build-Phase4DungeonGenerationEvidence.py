"""Consolidate the exact DungeonGeneration map migration into tracked evidence."""

import csv
import datetime
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = (
    "Docs/Migration/Evidence/Phase4_DungeonGeneration_ContentBuild.json"
)
CONTENT_COMMIT = "95fcd1b"
PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
TARGET_RELATIVE = "Content/Procedural/Maps/DungeonGeneration.umap"
SOURCE_LENGTH = 58016
SOURCE_SHA256 = (
    "B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F"
)
UE57_TARGET_LENGTH = 57890
UE57_TARGET_SHA256 = (
    "E1DF985A9EF8C97EC595A9CA014BD593ED07DDAEC805E1FA6F963FD9D158BE79"
)
TARGET_LENGTH = 58272
TARGET_SHA256 = (
    "4262B37586D7626F2C912AD81BE7FFF27EEA2615130919B5B85339E7B77E39E1"
)
EXPECTED_DEPENDENCIES = {
    "/Engine/EngineMaterials/WorldGridMaterial",
    "/Script/NavigationSystem",
    "/Script/PCG",
}
EXPECTED_ACTOR_CLASSES = {"PlayerStart", "NavMeshBoundsVolume", "PCGWorldActor"}
BASE = "Saved/Migration/Phase4/DungeonGeneration"


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


def require_safety_gate(gate, stage):
    require(
        gate.get("status") == "DUNGEON_GENERATION_SOURCE_PROTECTED_SAFETY_PASS"
        and gate.get("stage") == stage
        and gate.get("source_map", {}).get("package") == PACKAGE
        and gate.get("source_map", {}).get("length") == SOURCE_LENGTH
        and gate.get("source_map", {}).get("sha256") == SOURCE_SHA256
        and gate.get("source_read_only", {}).get("result") == "PASS"
        and gate.get("protected_invariants", {}).get("result") == "PASS"
        and gate.get("source_tree_mounted") is False
        and gate.get("raw_target_asset_copy_requested") is False,
        stage + " source/protected safety gate failed",
    )


harness = load(BASE + "/DungeonGeneration57HarnessReceipt.json")
validation57 = load(BASE + "/DungeonGeneration57Validation.json")
migration57 = load(BASE + "/DungeonGeneration57Migration.json")
resave58 = load(BASE + "/DungeonGeneration58Resave.json")
post_migration_gate = load(BASE + "/Gates/POST_MIGRATION57_SafetyGate.json")
post_resave_gate = load(BASE + "/Gates/POST_RESAVE58_SafetyGate.json")

require(
    harness.get("status") == "ISOLATED_DUNGEON_GENERATION57_HARNESS_PASS"
    and harness.get("package_count") == 1
    and harness.get("package") == PACKAGE
    and harness.get("source_map", {}).get("length") == SOURCE_LENGTH
    and harness.get("source_map", {}).get("sha256") == SOURCE_SHA256
    and harness.get("staged_map", {}).get("length") == SOURCE_LENGTH
    and harness.get("staged_map", {}).get("sha256") == SOURCE_SHA256
    and set(harness.get("direct_dependencies", [])) == EXPECTED_DEPENDENCIES
    and harness.get("game_dependency_count") == 0
    and harness.get("source_package_saves") == 0
    and harness.get("target_content_writes") == 0
    and harness.get("source_tree_mounted") is False,
    "Isolated UE 5.7 map harness gate failed",
)

require(
    validation57.get("status") == "UE57_DUNGEON_GENERATION_READ_ONLY_LOAD_PASS"
    and validation57.get("package") == PACKAGE
    and validation57.get("source_length") == SOURCE_LENGTH
    and validation57.get("source_sha256") == SOURCE_SHA256
    and set(validation57.get("dependencies", [])) == EXPECTED_DEPENDENCIES
    and set(validation57.get("hard_dependencies", [])) == EXPECTED_DEPENDENCIES
    and not validation57.get("soft_dependencies")
    and not validation57.get("game_dependencies")
    and "World" in validation57.get("registered_classes", [])
    and EXPECTED_ACTOR_CLASSES.issubset(validation57.get("actor_classes", []))
    and not validation57.get("missing_required_actor_classes")
    and validation57.get("map_load_requested") is True
    and validation57.get("map_save_requested") is False
    and validation57.get("packages_saved") == 0
    and validation57.get("target_content_writes") == 0
    and validation57.get("source_tree_mounted") is False,
    "UE 5.7 read-only map load gate failed",
)

package57 = migration57.get("package", {})
require(
    migration57.get("status") == "ASSETTOOLS_EXACT_DUNGEON_GENERATION_MIGRATION_PASS"
    and migration57.get("package_count") == 1
    and migration57.get("ignore_dependencies") is True
    and migration57.get("target_delta_exact") is True
    and migration57.get("created_package_files")
    == ["procedural/maps/dungeongeneration.umap"]
    and not migration57.get("modified_existing_package_files")
    and not migration57.get("removed_package_files")
    and not migration57.get("game_dependencies")
    and migration57.get("migration_invoked_this_run") is False
    and migration57.get("resumed_from_prior_log") is True
    and migration57.get("source_map_unchanged") is True
    and migration57.get("staged_map_unchanged") is True
    and migration57.get("source_tree_mounted") is False
    and migration57.get("raw_target_asset_copy_requested") is False
    and package57.get("package") == PACKAGE
    and package57.get("source_length") == SOURCE_LENGTH
    and package57.get("source_sha256") == SOURCE_SHA256
    and package57.get("length") == UE57_TARGET_LENGTH
    and package57.get("sha256") == UE57_TARGET_SHA256
    and package57.get("bytes_match_source") is False,
    "UE 5.7 exact AssetTools map migration gate failed",
)

for label in ("loaded_before_save", "loaded_after_reload"):
    loaded = resave58.get(label, {})
    require(
        loaded.get("world_path", "").startswith(PACKAGE + ".")
        and EXPECTED_ACTOR_CLASSES.issubset(loaded.get("actor_classes", []))
        and not loaded.get("missing_required_actor_classes")
        and not loaded.get("streaming_levels")
        and loaded.get("world_partitioned") is False,
        "UE 5.8 world inspection failed for " + label,
    )
require(
    resave58.get("status") == "UE58_DUNGEON_GENERATION_LOAD_RESAVE_RELOAD_PASS"
    and resave58.get("package") == PACKAGE
    and resave58.get("source_length") == SOURCE_LENGTH
    and resave58.get("source_sha256") == SOURCE_SHA256
    and resave58.get("length_before_resave") == UE57_TARGET_LENGTH
    and resave58.get("sha256_before_resave") == UE57_TARGET_SHA256
    and resave58.get("length_after_resave") == TARGET_LENGTH
    and resave58.get("sha256_after_resave") == TARGET_SHA256
    and set(resave58.get("dependencies_before_resave", [])) == EXPECTED_DEPENDENCIES
    and set(resave58.get("dependencies_after_resave", [])) == EXPECTED_DEPENDENCIES
    and set(resave58.get("hard_dependencies_after_resave", []))
    == EXPECTED_DEPENDENCIES
    and not resave58.get("soft_dependencies_after_resave")
    and not resave58.get("game_dependencies")
    and resave58.get("map_load_requested") is True
    and resave58.get("map_save_requested") is True
    and resave58.get("map_reload_requested") is True
    and resave58.get("target_delta_exact") is True
    and resave58.get("modified_package_files")
    == ["procedural/maps/dungeongeneration.umap"]
    and not resave58.get("created_package_files")
    and not resave58.get("removed_package_files")
    and not resave58.get("sidecars_or_external_packages_created")
    and not resave58.get("dirty_map_packages_after_reload"),
    "UE 5.8 map load/save/reload gate failed",
)

require_safety_gate(post_migration_gate, "POST_MIGRATION57")
require_safety_gate(post_resave_gate, "POST_RESAVE58")
require(
    post_resave_gate.get("target_map", {}).get("length") == TARGET_LENGTH
    and post_resave_gate.get("target_map", {}).get("sha256") == TARGET_SHA256
    and len(post_resave_gate.get("forbidden_target_paths_absent", [])) == 6,
    "POST_RESAVE58 target map safety record differs",
)

target_path = ROOT / TARGET_RELATIVE
require(target_path.is_file(), "Migrated target map is absent")
require(
    target_path.stat().st_size == TARGET_LENGTH
    and sha256(target_path) == TARGET_SHA256,
    "Current target map differs from the UE 5.8 receipt",
)

build_logs = {
    "editor": "Saved/Migration/Logs/Phase4_DungeonGeneration_EditorBuild58.log",
    "game": "Saved/Migration/Logs/Phase4_DungeonGeneration_GameBuild58.log",
}
for label, relative in build_logs.items():
    require(
        "Result: Succeeded" in read_text_auto(relative),
        label + " build did not report success",
    )

content_logs = {
    "validation57": "Saved/Migration/Logs/Phase4_DungeonGeneration57_Validation_Retry.log",
    "migration57": "Saved/Migration/Logs/Phase4_DungeonGeneration57_Migration_Resume.log",
    "resave58": "Saved/Migration/Logs/Phase4_DungeonGeneration58_Resave.log",
}
content_markers = {
    "validation57": "CODEX_DUNGEON_GENERATION57_VALIDATION_PASS",
    "migration57": "CODEX_DUNGEON_GENERATION57_MIGRATION_PASS",
    "resave58": "CODEX_DUNGEON_GENERATION58_RESAVE_PASS",
}
for label, relative in content_logs.items():
    text = read_text_auto(relative)
    require(
        content_markers[label] in text
        and "Success - 0 error(s)" in text
        and "LogPythonScriptCommandlet: Error" not in text
        and "LogBlueprint: Error" not in text
        and not ("Compile of " in text and " failed" in text),
        label + " log does not contain a clean PASS",
    )

prior_logs = {
    "validation57_api_probe": "Saved/Migration/Logs/Phase4_DungeonGeneration57_Validation.log",
    "migration57_assettools_run": "Saved/Migration/Logs/Phase4_DungeonGeneration57_Migration.log",
}
prior_validation = read_text_auto(prior_logs["validation57_api_probe"])
require(
    "AssetData' object has no attribute 'object_path'" in prior_validation,
    "Initial UE 5.7 validation log does not prove the audited API retry",
)
prior_migration = read_text_auto(prior_logs["migration57_assettools_run"])
success_token = (
    "AssetTools: Package (/Game/Procedural/Maps/DungeonGeneration) was "
    "migrated successfully as (/Game/Procedural/Maps/DungeonGeneration)"
)
require(
    prior_migration.count(success_token) == 1
    and prior_migration.count("AssetTools: Package (/Game/") == 1
    and "UE 5.7 AssetTools output is not byte-identical to the staged map"
    in prior_migration,
    "Initial UE 5.7 AssetTools log does not prove the exact recovery path",
)

manifest_path = ROOT / "Docs/Migration/04_Content_Migration_Manifest.csv"
with manifest_path.open("r", encoding="utf-8-sig", newline="") as handle:
    manifest_rows = list(csv.DictReader(handle))
selected = [row for row in manifest_rows if row.get("TestEvidence") == EVIDENCE_PATH]
require(len(selected) == 1, "Manifest does not contain exactly one map evidence row")
row = selected[0]
require(
    row.get("PackageName") == PACKAGE
    and row.get("TargetAssetClass") == "World"
    and row.get("Presence") == "BOTH"
    and row.get("Classification") == "MIGRATED_PROJECT_CONTENT"
    and row.get("Authority") == "SOURCE_BEHAVIOR"
    and row.get("Action") == "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    and row.get("Result") == "PASS"
    and row.get("Commit") == CONTENT_COMMIT
    and row.get("TargetLength") == str(TARGET_LENGTH)
    and row.get("TargetSHA256") == TARGET_SHA256
    and set(filter(None, row.get("TargetDependencies", "").split(";")))
    == EXPECTED_DEPENDENCIES,
    "Manifest map disposition differs from the exact PASS",
)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "content_commit": CONTENT_COMMIT,
    "status": "CONTENT_BUILD_MAP_LOAD_PASS_PIE_VISUAL_COOK_PACKAGE_DUNGEON_RUNTIME_PENDING",
    "scope": {
        "source_packages_migrated": 1,
        "package": PACKAGE,
        "asset_class": "World",
        "source_length": SOURCE_LENGTH,
        "source_sha256": SOURCE_SHA256,
        "target_length": TARGET_LENGTH,
        "target_sha256": TARGET_SHA256,
        "external_game_dependencies": 0,
        "engine_dependencies": sorted(EXPECTED_DEPENDENCIES),
        "required_actor_classes": sorted(EXPECTED_ACTOR_CLASSES),
        "streaming_levels": 0,
        "world_partitioned": False,
        "sidecars_or_external_packages": 0,
    },
    "gates": {
        "isolated_ue57_harness": harness["status"],
        "ue57_read_only_map_load": validation57["status"],
        "ue57_assettools_exact_migration": migration57["status"],
        "ue58_load_save_reload": resave58["status"],
        "target_delta_exact": True,
        "editor_build": "PASS",
        "game_build": "PASS",
        "source_read_only_after_ue57": "PASS",
        "protected_target_invariants_after_ue57": "PASS",
        "source_read_only_after_ue58": "PASS",
        "protected_target_invariants_after_ue58": "PASS",
    },
    "process_notes": [
        "The first UE 5.7 validation attempt used an unavailable AssetData.object_path property; the compatible get_soft_object_path retry passed without saving the map.",
        "The first UE 5.7 migration run migrated exactly one package, then stopped at an invalid byte-identity assertion because AssetTools reserialized the map. The resume gate verified that exact one-package delta before UE 5.8 resave.",
    ],
    "pending": [
        "PIE runtime validation on the migrated map",
        "visible procedural and navigation QA",
        "cook and cooked-manifest validation",
        "package",
        "packaged runtime",
        "/Game/Procedural/DoorToLevel",
        "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
        "canonical StartPoint discovery/spawn",
        "full procedural dungeon generation and cleanup flow",
    ],
    "evidence": {
        "harness": BASE + "/DungeonGeneration57HarnessReceipt.json",
        "validation57": BASE + "/DungeonGeneration57Validation.json",
        "migration57": BASE + "/DungeonGeneration57Migration.json",
        "resave58": BASE + "/DungeonGeneration58Resave.json",
        "post_migration57_gate": BASE + "/Gates/POST_MIGRATION57_SafetyGate.json",
        "post_resave58_gate": BASE + "/Gates/POST_RESAVE58_SafetyGate.json",
        "content_logs": content_logs,
        "prior_logs": prior_logs,
        "build_logs": build_logs,
    },
}

output = ROOT / EVIDENCE_PATH
output.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print("PHASE4_DUNGEON_GENERATION_EVIDENCE_PASS", output)
