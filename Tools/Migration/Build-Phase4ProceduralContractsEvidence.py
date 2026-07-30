"""Consolidate the exact procedural-contract migration into tracked evidence."""

import csv
import datetime
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = (
    "Docs/Migration/Evidence/Phase4_ProceduralContracts_ContentBuild.json"
)
EXPECTED_SOURCE_BYTES = 223283
EXPECTED_SOURCE_FINGERPRINT = (
    "30A8ACBD998EBD41242A3BD850C8CBB3E7E6A19D63D72E632D0BB897917FA006"
)
EXPECTED_CLASS_COUNTS = {
    "Blueprint": 6,
    "UserDefinedStruct": 12,
    "UserDefinedEnum": 2,
}
EXPECTED_PARENT_COUNTS = {
    "/Script/Engine.PrimaryDataAsset": 5,
    "/Script/Engine.Actor": 1,
}
EXPECTED_PACKAGES = {
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
}
EXCLUDED_RUNTIME_ASSETS = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Procedural/Maps/DungeonGeneration",
    "/Game/Procedural/DoorToLevel",
)


def load(relative):
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError("Required evidence is absent: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def require(value, message):
    if not value:
        raise RuntimeError(message)


def read_text_auto(path):
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


def source_fingerprint(rows):
    values = [rows[package]["SourceSHA256"].upper() for package in sorted(rows)]
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest().upper()


harness = load(
    "Saved/Migration/Phase4/EFProceduralContracts57HarnessReceipt.json"
)
validation57 = load(
    "Saved/Migration/Phase4/EFProceduralContracts57Validation.json"
)
migration57 = load(
    "Saved/Migration/Phase4/EFProceduralContracts57Migration.json"
)
resave58 = load(
    "Saved/Migration/Phase4/EFProceduralContracts58Resave.json"
)
source_gate = load("Saved/Migration/Evidence/SourceReadOnlyVerification.json")
protected_gate = load(
    "Saved/Migration/Evidence/ProtectedInvariantVerification.json"
)

require(
    harness.get("status") == "ISOLATED_PROCEDURAL_CONTRACTS_HARNESS_PASS"
    and harness.get("expected_package_count") == 20
    and harness.get("expected_source_bytes") == EXPECTED_SOURCE_BYTES
    and harness.get("expected_source_fingerprint")
    == EXPECTED_SOURCE_FINGERPRINT
    and harness.get("expected_class_counts") == EXPECTED_CLASS_COUNTS
    and harness.get("expected_blueprint_parent_counts")
    == EXPECTED_PARENT_COUNTS
    and harness.get("source_package_loads") == 0
    and harness.get("source_package_saves") == 0,
    "Isolated UE 5.7 harness gate failed",
)
harness_packages = {
    row["package"] for row in harness.get("staged_assets", [])
}
require(
    harness_packages == EXPECTED_PACKAGES
    and len(harness.get("staged_assets", [])) == 20,
    "Harness package set differs from the exact allowlist",
)

require(
    validation57.get("status")
    == "UE57_PROCEDURAL_CONTRACTS_READ_ONLY_LOAD_COMPILE_PASS"
    and validation57.get("package_count") == 20
    and validation57.get("source_bytes") == EXPECTED_SOURCE_BYTES
    and validation57.get("source_fingerprint") == EXPECTED_SOURCE_FINGERPRINT
    and validation57.get("class_counts") == EXPECTED_CLASS_COUNTS
    and validation57.get("compiled_blueprint_count") == 6
    and validation57.get("blueprint_parent_counts")
    == EXPECTED_PARENT_COUNTS
    and validation57.get("direct_contract_root_count") == 9
    and validation57.get("contract_closure_count") == 19
    and not validation57.get("unexpected_game_dependencies")
    and validation57.get("asset_save_requested") is False
    and validation57.get("packages_saved") == 0
    and validation57.get("source_tree_mounted") is False,
    "UE 5.7 read-only load/compile gate failed",
)

require(
    migration57.get("status")
    == "ASSETTOOLS_EXACT_PROCEDURAL_CONTRACTS_MIGRATION_PASS"
    and migration57.get("package_count") == 20
    and migration57.get("created_package_count") == 20
    and migration57.get("source_bytes") == EXPECTED_SOURCE_BYTES
    and migration57.get("source_fingerprint") == EXPECTED_SOURCE_FINGERPRINT
    and migration57.get("class_counts") == EXPECTED_CLASS_COUNTS
    and migration57.get("ignore_dependencies") is True
    and migration57.get("target_delta_exact") is True
    and not migration57.get("unexpected_game_dependencies")
    and migration57.get("source_tree_mounted") is False,
    "UE 5.7 exact AssetTools migration gate failed",
)
require(
    (
        migration57.get("migration_invoked_this_run") is True
        and migration57.get("resumed_from_prior_log") is False
    )
    or (
        migration57.get("migration_invoked_this_run") is False
        and migration57.get("resumed_from_prior_log") is True
        and migration57.get("prior_migration_log")
    ),
    "UE 5.7 migration execution provenance is incomplete",
)
migrated_packages = {
    row["package"] for row in migration57.get("packages", [])
}
require(
    migrated_packages == EXPECTED_PACKAGES
    and len(migration57.get("packages", [])) == 20,
    "UE 5.7 migrated package set differs from the exact allowlist",
)

require(
    resave58.get("status")
    == "UE58_PROCEDURAL_CONTRACTS_LOAD_COMPILE_RESAVE_PASS"
    and resave58.get("package_count") == 20
    and resave58.get("source_bytes") == EXPECTED_SOURCE_BYTES
    and resave58.get("source_fingerprint") == EXPECTED_SOURCE_FINGERPRINT
    and resave58.get("class_counts") == EXPECTED_CLASS_COUNTS
    and resave58.get("compiled_blueprint_count") == 6
    and resave58.get("blueprint_parent_counts") == EXPECTED_PARENT_COUNTS
    and not resave58.get("unexpected_game_dependencies")
    and not resave58.get("redirectors")
    and resave58.get("map_load_requested") is False
    and resave58.get("pie_requested") is False
    and resave58.get("excluded_runtime_assets_touched") is False,
    "UE 5.8 load/compile/resave gate failed",
)
resaved_packages = {
    row["package"] for row in resave58.get("packages", [])
}
require(
    resaved_packages == EXPECTED_PACKAGES
    and len(resave58.get("packages", [])) == 20,
    "UE 5.8 resaved package set differs from the exact allowlist",
)

require(source_gate.get("pass") is True, "Source read-only gate failed")
require(
    protected_gate.get("result") == "PASS"
    and protected_gate.get("mismatch_count") == 0,
    "Protected target invariant gate failed",
)

build_logs = {
    "editor": "Saved/Migration/Logs/Phase4_ProceduralContracts_EditorBuild58.log",
    "game": "Saved/Migration/Logs/Phase4_ProceduralContracts_GameBuild58.log",
}
for label, relative in build_logs.items():
    require(
        "Result: Succeeded" in read_text_auto(ROOT / relative),
        label + " build did not report success",
    )

content_logs = {
    "validation57": "Saved/Migration/Logs/Phase4_ProceduralContracts57_Validation.log",
    "migration57": "Saved/Migration/Logs/Phase4_ProceduralContracts57_Migration_Resume.log",
    "resave58": "Saved/Migration/Logs/Phase4_ProceduralContracts58_Resave.log",
}
content_markers = {
    "validation57": "CODEX_PROCEDURAL_CONTRACTS57_VALIDATION_PASS",
    "migration57": "CODEX_PROCEDURAL_CONTRACTS57_MIGRATION_PASS",
    "resave58": "CODEX_PROCEDURAL_CONTRACTS58_RESAVE_PASS",
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

prior_migration_log = "Saved/Migration/Logs/Phase4_ProceduralContracts57_Migration.log"
if migration57.get("resumed_from_prior_log") is True:
    prior_text = read_text_auto(ROOT / prior_migration_log)
    require(
        prior_text.count("AssetTools: Package (/Game/") == 20
        and "UE 5.7 migrated bytes differ from staged source: "
        "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint" in prior_text,
        "Prior UE 5.7 AssetTools log does not prove the audited recovery path",
    )

manifest_path = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
with manifest_path.open("r", encoding="utf-8-sig", newline="") as handle:
    manifest_rows = list(csv.DictReader(handle))
selected = [
    row for row in manifest_rows if row.get("TestEvidence") == EVIDENCE_PATH
]
require(len(selected) == 20, "Manifest does not contain exactly 20 evidence rows")
selected_by_package = {row["PackageName"]: row for row in selected}
require(
    set(selected_by_package) == EXPECTED_PACKAGES
    and len(selected_by_package) == 20,
    "Manifest evidence package set differs from the exact allowlist",
)
require(
    sum(int(row["SourceLength"]) for row in selected) == EXPECTED_SOURCE_BYTES,
    "Manifest source-byte total differs from the frozen baseline",
)
require(
    source_fingerprint(selected_by_package) == EXPECTED_SOURCE_FINGERPRINT,
    "Manifest source fingerprint differs from the frozen baseline",
)

manifest_class_counts = {}
resave_rows = {row["package"]: row for row in resave58["packages"]}
for package, row in selected_by_package.items():
    require(
        row.get("Presence") == "BOTH"
        and row.get("Classification") == "MIGRATED_PROJECT_CONTENT"
        and row.get("Authority") == "SOURCE_BEHAVIOR"
        and row.get("Action")
        == "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
        and row.get("Result") == "PASS",
        "Manifest disposition is not PASS for " + package,
    )
    target_path = ROOT / Path(row["TargetFile"].replace("\\", "/"))
    require(target_path.is_file(), "Manifest target is absent: " + str(target_path))
    require(
        str(target_path.stat().st_size) == row["TargetLength"]
        and sha256(target_path) == row["TargetSHA256"]
        and row["TargetSHA256"]
        == resave_rows[package]["sha256_after_resave"],
        "Manifest target hash differs for " + package,
    )
    asset_class = row["TargetAssetClass"]
    manifest_class_counts[asset_class] = (
        manifest_class_counts.get(asset_class, 0) + 1
    )
require(
    manifest_class_counts == EXPECTED_CLASS_COUNTS,
    "Manifest target class counts differ from the exact allowlist",
)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "CONTENT_BUILD_PASS_PIE_VISUAL_COOK_PACKAGE_DUNGEON_RUNTIME_PENDING",
    "scope": {
        "source_packages_migrated": 20,
        "source_bytes": EXPECTED_SOURCE_BYTES,
        "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
        "calysto_data_contracts": 19,
        "canonical_start_point_blueprint": 1,
        "class_counts": EXPECTED_CLASS_COUNTS,
        "compiled_blueprints": 6,
        "blueprint_parent_counts": EXPECTED_PARENT_COUNTS,
        "external_game_dependencies": 0,
        "redirectors": 0,
    },
    "gates": {
        "isolated_ue57_harness": harness["status"],
        "ue57_read_only_load_compile": validation57["status"],
        "ue57_assettools_exact_migration": migration57["status"],
        "ue58_load_compile_resave": resave58["status"],
        "ignore_dependencies": True,
        "target_delta_exact": True,
        "editor_build": "PASS",
        "game_build": "PASS",
        "source_read_only": "PASS",
        "protected_target_invariants": "PASS",
    },
    "excluded_runtime_assets": list(EXCLUDED_RUNTIME_ASSETS),
    "pending": [
        "PIE runtime and canonical StartPoint discovery/spawn validation",
        "visible visual QA",
        "cook and cooked-manifest validation for all 20 packages",
        "package",
        "packaged runtime",
        "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
        "/Game/Procedural/Maps/DungeonGeneration",
        "/Game/Procedural/DoorToLevel",
        "full procedural dungeon generation and cleanup flow",
    ],
    "evidence": {
        "harness": "Saved/Migration/Phase4/EFProceduralContracts57HarnessReceipt.json",
        "validation57": "Saved/Migration/Phase4/EFProceduralContracts57Validation.json",
        "migration57": "Saved/Migration/Phase4/EFProceduralContracts57Migration.json",
        "resave58": "Saved/Migration/Phase4/EFProceduralContracts58Resave.json",
        "content_logs": content_logs,
        "prior_migration_log": prior_migration_log,
        "build_logs": build_logs,
        "source_gate": "Saved/Migration/Evidence/SourceReadOnlyVerification.json",
        "protected_gate": "Saved/Migration/Evidence/ProtectedInvariantVerification.json",
    },
}

output = ROOT / EVIDENCE_PATH
output.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print("PHASE4_PROCEDURAL_CONTRACTS_EVIDENCE_PASS", output)
