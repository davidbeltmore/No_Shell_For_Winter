"""Retire exactly the superseded Dungeon Director V3 policy through Unreal.

This is an intentionally narrow, fail-closed cutover operation.  It never
runs on import unless three explicit environment guards are present, never
saves packages, never edits vendor assets, and writes a create-only receipt
only after every postcondition has passed.

Run this only inside the UE 5.8 Editor for NoShellForWinter after the V4
cutover gates are green.  The byte-exact V3 backup under Saved is preserved.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
from pathlib import Path

import unreal


EXPECTED_PROJECT_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
EXPECTED_PROJECT_FILE = "NoShellForWinter.uproject"

V3_DIRECTORY = "/Game/_Game/Data/CalystoDungeon/V3"
V3_ASSET_PATH = f"{V3_DIRECTORY}/DA_CalystoDungeonDirectorPolicy"
V3_CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy"
V3_EXPECTED_SIZE = 9211
V3_EXPECTED_SHA256 = (
    "9824B1EFC3EF8B24D5C33DDF0813B0EC999B3C9F5331BDB8E48D771120868D3A"
)

V4_ASSET_PATH = (
    "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
)
V4_CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
V4_EXPECTED_POLICY_ID = "CalystoDungeonDirectorV4"

APPLY_ENV = "CODEX_APPLY_CALYSTO_V3_POLICY_RETIREMENT"
ACK_ENV = "CODEX_ACK_CALYSTO_V3_POLICY_PATH"
EVIDENCE_ENV = "CODEX_CALYSTO_V3_RETIREMENT_EVIDENCE"

PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
EVIDENCE_ROOT = (
    SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV4"
).resolve()
V3_FILE = (
    CONTENT_ROOT
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V3"
    / "DA_CalystoDungeonDirectorPolicy.uasset"
)
V4_FILE = (
    CONTENT_ROOT
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V4"
    / "DA_CalystoDungeonDirectorPolicy.uasset"
)
V3_BACKUP_FILE = (
    EVIDENCE_ROOT
    / "RetiredPolicyAssets"
    / "DA_CalystoDungeonDirectorPolicy_V3.uasset"
)

# Project-owned and vendor content whose bytes must be identical before/after
# this one-asset operation.  The complete Content stat delta is checked too.
PROTECTED_CONTENT_FILES = (
    "Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.uasset",
    "Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.uasset",
    "Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    "Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner.uasset",
    "Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.uasset",
    "Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.uasset",
    "Calysto/Dungeon/PCG/Function/PCG_SpawnStartAndEnd.uasset",
    "Calysto/Dungeon/PCG/Function/PCG_SetRoomTheme.uasset",
    "Calysto/Dungeon/PCG/Function/PCG_DungeonSpawner.uasset",
    "Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.uasset",
    "Procedural/Maps/DungeonGeneration.umap",
    "Procedural/DoorToLevel.uasset",
    "FullSample/Player.uasset",
    "DazToUnreal/Female/Female.uasset",
    "DazToUnreal/Male/Male.uasset",
    "DazToUnreal/Multiple/Multiple.uasset",
)
BP_MASSIVE_DUNGEON_RELATIVE = (
    "Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset"
)
BP_MASSIVE_DUNGEON_EXPECTED_SHA256 = (
    "47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B"
)


def fail(message: str) -> None:
    unreal.log_error("CALYSTO_V3_POLICY_RETIREMENT_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def file_evidence(path: Path) -> dict[str, object]:
    if not path.is_file():
        fail(f"Required file is missing: {path}")
    return {
        "path": str(path),
        "size": int(path.stat().st_size),
        "sha256": sha256(path),
    }


def files_are_byte_exact(left: Path, right: Path) -> bool:
    if left.stat().st_size != right.stat().st_size:
        return False
    with left.open("rb") as left_handle, right.open("rb") as right_handle:
        while True:
            left_block = left_handle.read(1024 * 1024)
            right_block = right_handle.read(1024 * 1024)
            if left_block != right_block:
                return False
            if not left_block:
                return True


def path_is_within(path: Path, root: Path) -> bool:
    path_key = os.path.normcase(str(path.resolve()))
    root_key = os.path.normcase(str(root.resolve()))
    try:
        return os.path.commonpath((path_key, root_key)) == root_key
    except ValueError:
        return False


def content_stat_snapshot() -> dict[str, tuple[int, int]]:
    """Fast complete package inventory; protected bytes are hashed separately."""
    result: dict[str, tuple[int, int]] = {}
    for pattern in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(pattern):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
            )
    return result


def content_delta(
    before: dict[str, tuple[int, int]],
    after: dict[str, tuple[int, int]],
) -> dict[str, list[str]]:
    before_paths = set(before)
    after_paths = set(after)
    return {
        "added": sorted(after_paths - before_paths),
        "removed": sorted(before_paths - after_paths),
        "modified": sorted(
            path
            for path in before_paths & after_paths
            if before[path] != after[path]
        ),
    }


def dirty_packages() -> list[str]:
    content = list(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    )
    maps = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({str(package.get_path_name()) for package in content + maps})


def editor_property(owner, name: str):
    candidates = [name, name[0].lower() + name[1:]]
    snake: list[str] = []
    for character in name:
        if character.isupper() and snake:
            snake.append("_")
        snake.append(character.lower())
    candidates.append("".join(snake))
    for candidate in dict.fromkeys(candidates):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
    fail(f"{V4_ASSET_PATH} is missing required property {name}")


def validate_v4_policy(policy_class) -> dict[str, object]:
    asset = unreal.load_asset(V4_ASSET_PATH)
    if asset is None:
        fail(f"V4 policy is missing: {V4_ASSET_PATH}")
    if asset.get_class() != policy_class:
        fail(
            f"V4 policy class is {asset.get_class().get_path_name()}, expected "
            f"{V4_CLASS_PATH}"
        )
    if not V4_FILE.is_file():
        fail(f"V4 policy package is missing on disk: {V4_FILE}")

    schema_version = int(editor_property(asset, "SchemaVersion"))
    generator_version = int(editor_property(asset, "GeneratorVersion"))
    policy_id = str(editor_property(asset, "PolicyId"))
    if schema_version != 4 or generator_version != 4:
        fail(
            "V4 policy identity is invalid: "
            f"schema={schema_version} generator={generator_version}"
        )
    if policy_id != V4_EXPECTED_POLICY_ID:
        fail(
            f"V4 PolicyId is {policy_id!r}, expected "
            f"{V4_EXPECTED_POLICY_ID!r}"
        )

    validator = getattr(asset, "validate_policy", None)
    if not callable(validator) or validator() is not True:
        fail("Native V4 ValidatePolicy did not return true")
    hash_method = getattr(asset, "get_policy_hash", None)
    if not callable(hash_method):
        fail("V4 policy does not expose GetPolicyHash")
    policy_hash = str(hash_method()).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", policy_hash):
        fail("V4 policy returned an invalid canonical SHA-256")

    return {
        "asset_path": V4_ASSET_PATH,
        "class": asset.get_class().get_path_name(),
        "schema_version": schema_version,
        "generator_version": generator_version,
        "policy_id": policy_id,
        "policy_hash": policy_hash,
        "package_size": int(V4_FILE.stat().st_size),
        "package_sha256": sha256(V4_FILE),
    }


def protected_snapshot() -> dict[str, dict[str, object]]:
    return {
        relative: file_evidence(CONTENT_ROOT / Path(relative))
        for relative in PROTECTED_CONTENT_FILES
    }


def registry_referencers(registry, options) -> list[str]:
    try:
        values = registry.get_referencers(V3_ASSET_PATH, options)
    except TypeError:
        fail("AssetRegistry rejected the all-dependency referencer options")
    return sorted({str(value) for value in (values or [])})


def build_dependency_options(
    enabled_names: set[str],
):
    options = unreal.AssetRegistryDependencyOptions()
    all_names = (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    )
    for option_name in all_names:
        try:
            options.set_editor_property(
                option_name,
                option_name in enabled_names,
            )
        except Exception as error:
            fail(f"Could not configure {option_name}: {error}")
    return options


def registry_referencers_by_kind(registry) -> dict[str, list[str]]:
    option_names = {
        "hard_package": {"include_hard_package_references"},
        "soft_package": {"include_soft_package_references"},
        "hard_management": {"include_hard_management_references"},
        "soft_management": {"include_soft_management_references"},
        "all_dependency_kinds": {
            "include_hard_package_references",
            "include_soft_package_references",
            "include_hard_management_references",
            "include_soft_management_references",
        },
    }
    return {
        label: registry_referencers(
            registry,
            build_dependency_options(enabled),
        )
        for label, enabled in option_names.items()
    }


def editor_referencers() -> list[str]:
    try:
        values = unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            V3_ASSET_PATH,
            True,
        )
    except Exception as error:
        fail(f"EditorAssetLibrary referencer confirmation failed: {error}")
    return sorted({str(value) for value in (values or [])})


def assets_in_v3_directory(registry) -> list[dict[str, str]]:
    rows = registry.get_assets_by_path(
        V3_DIRECTORY,
        recursive=True,
        include_only_on_disk_assets=False,
    )
    return sorted(
        (
            {
                "package_name": str(row.package_name),
                "asset_name": str(row.asset_name),
                "asset_class_path": str(row.asset_class_path),
            }
            for row in rows
        ),
        key=lambda item: (item["package_name"], item["asset_name"]),
    )


def validate_execution_context() -> Path:
    if os.environ.get(APPLY_ENV, "") != "1":
        fail(f"Apply guard is not set; define {APPLY_ENV}=1")
    if os.environ.get(ACK_ENV, "") != V3_ASSET_PATH:
        fail(f"{ACK_ENV} must equal the exact asset path {V3_ASSET_PATH}")

    output_text = os.environ.get(EVIDENCE_ENV, "").strip()
    if not output_text:
        fail(f"{EVIDENCE_ENV} is required")
    output_path = Path(output_text)
    if not output_path.is_absolute():
        output_path = PROJECT_ROOT / output_path
    output_path = output_path.resolve()
    if output_path.suffix.casefold() != ".json":
        fail("Retirement evidence must use a .json filename")
    if not path_is_within(output_path, EVIDENCE_ROOT):
        fail(
            "Retirement evidence must stay below "
            f"{EVIDENCE_ROOT}; got {output_path}"
        )
    if output_path.exists():
        fail(f"Refusing to overwrite retirement evidence: {output_path}")

    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Dungeon Director V3 retirement requires Unreal Engine 5.8")
    if PROJECT_FILE.name.casefold() != EXPECTED_PROJECT_FILE.casefold():
        fail(f"Wrong Unreal project: {PROJECT_FILE}")
    if os.path.normcase(str(PROJECT_ROOT)) != os.path.normcase(
        str(EXPECTED_PROJECT_ROOT)
    ):
        fail(
            "Refusing to run outside the writable NoShellForWinter target: "
            f"actual={PROJECT_ROOT} expected={EXPECTED_PROJECT_ROOT}"
        )
    if os.path.normcase(str(CONTENT_ROOT)) != os.path.normcase(
        str((EXPECTED_PROJECT_ROOT / "Content").resolve())
    ):
        fail(f"Unexpected Content root: {CONTENT_ROOT}")
    return output_path


OUTPUT_PATH = validate_execution_context()

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()

dirty_before = dirty_packages()
if dirty_before:
    fail(
        "The Editor has dirty content/map packages. Save or discard them before "
        f"cutover; dirty={dirty_before}"
    )

if not unreal.EditorAssetLibrary.does_asset_exist(V3_ASSET_PATH):
    fail(f"V3 policy is not present: {V3_ASSET_PATH}")
if not V3_FILE.is_file():
    fail(f"V3 policy package is missing on disk: {V3_FILE}")

v3_asset = unreal.load_asset(V3_ASSET_PATH)
v3_class = unreal.load_class(None, V3_CLASS_PATH)
if v3_asset is None or v3_class is None:
    fail("V3 asset or compiled V3 class could not be loaded")
if v3_class.get_path_name() != V3_CLASS_PATH:
    fail(
        f"V3 class resolved through a redirect to {v3_class.get_path_name()}"
    )
if v3_asset.get_class() != v3_class:
    fail(
        f"V3 asset class is {v3_asset.get_class().get_path_name()}, expected "
        f"{V3_CLASS_PATH}"
    )

v3_live_before = file_evidence(V3_FILE)
v3_backup_before = file_evidence(V3_BACKUP_FILE)
for label, evidence in (
    ("live", v3_live_before),
    ("backup", v3_backup_before),
):
    if evidence["size"] != V3_EXPECTED_SIZE:
        fail(
            f"V3 {label} size changed: expected {V3_EXPECTED_SIZE}, "
            f"got {evidence['size']}"
        )
    if evidence["sha256"] != V3_EXPECTED_SHA256:
        fail(
            f"V3 {label} SHA-256 changed: expected {V3_EXPECTED_SHA256}, "
            f"got {evidence['sha256']}"
        )
if not files_are_byte_exact(V3_FILE, V3_BACKUP_FILE):
    fail("V3 live policy and Saved backup are not byte-exact")

v4_class = unreal.load_class(None, V4_CLASS_PATH)
if v4_class is None or v4_class.get_path_name() != V4_CLASS_PATH:
    fail(f"Missing exact compiled V4 class: {V4_CLASS_PATH}")
v4_before = validate_v4_policy(v4_class)

registry_refs_before = registry_referencers_by_kind(registry)
editor_refs_before = editor_referencers()
if any(registry_refs_before.values()) or editor_refs_before:
    fail(
        "V3 policy still has referencers; "
        f"registry={registry_refs_before} editor={editor_refs_before}"
    )

v3_assets_before = assets_in_v3_directory(registry)
if len(v3_assets_before) != 1 or (
    v3_assets_before[0]["package_name"] != V3_ASSET_PATH
):
    fail(
        "V3 directory inventory is not the one expected retirement target: "
        f"{v3_assets_before}"
    )

protected_before = protected_snapshot()
if (
    protected_before[BP_MASSIVE_DUNGEON_RELATIVE]["sha256"]
    != BP_MASSIVE_DUNGEON_EXPECTED_SHA256
):
    fail(
        "BP_MassiveDungeon no longer matches the protected V3/V4 baseline; "
        f"expected={BP_MASSIVE_DUNGEON_EXPECTED_SHA256} "
        f"actual={protected_before[BP_MASSIVE_DUNGEON_RELATIVE]['sha256']}"
    )

content_before = content_stat_snapshot()
expected_removed_relative = V3_FILE.relative_to(CONTENT_ROOT).as_posix()

# The only mutation in this script.  Do not replace this with raw filesystem
# deletion: Unreal must own package retirement and AssetRegistry invalidation.
delete_returned = unreal.EditorAssetLibrary.delete_asset(V3_ASSET_PATH)
if not delete_returned:
    fail(f"EditorAssetLibrary failed to delete {V3_ASSET_PATH}")
if unreal.EditorAssetLibrary.does_asset_exist(V3_ASSET_PATH):
    fail("V3 policy still exists after EditorAssetLibrary deletion")
if V3_FILE.exists():
    fail(f"V3 policy file still exists after deletion: {V3_FILE}")

registry.search_all_assets(True)
registry.wait_for_completion()

v3_assets_after = assets_in_v3_directory(registry)
if v3_assets_after:
    fail(f"V3 directory still contains registered assets: {v3_assets_after}")
registry_refs_after = registry_referencers_by_kind(registry)
editor_refs_after = editor_referencers()
if any(registry_refs_after.values()) or editor_refs_after:
    fail(
        "V3 referencers appeared after retirement; "
        f"registry={registry_refs_after} editor={editor_refs_after}"
    )

v4_after = validate_v4_policy(v4_class)
if v4_after != v4_before:
    fail(f"V4 changed during V3 retirement: before={v4_before} after={v4_after}")

v3_backup_after = file_evidence(V3_BACKUP_FILE)
if v3_backup_after != v3_backup_before:
    fail(
        "Byte-exact V3 backup changed during retirement: "
        f"before={v3_backup_before} after={v3_backup_after}"
    )

protected_after = protected_snapshot()
if protected_after != protected_before:
    fail("One or more protected content assets changed during V3 retirement")

dirty_after = dirty_packages()
if dirty_after:
    fail(
        "Retirement left dirty content/map packages; no package save is allowed: "
        f"{dirty_after}"
    )

content_after = content_stat_snapshot()
delta = content_delta(content_before, content_after)
expected_delta = {
    "added": [],
    "removed": [expected_removed_relative],
    "modified": [],
}
if delta != expected_delta:
    fail(f"Unexpected Content delta: actual={delta} expected={expected_delta}")

receipt = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(
        datetime.timezone.utc
    ).isoformat(),
    "result": "PASS",
    "operation": "DELETE_EXACT_V3_POLICY_VIA_EDITOR_ASSET_LIBRARY",
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "asset_path": V3_ASSET_PATH,
    "class_path": V3_CLASS_PATH,
    "delete_api_returned": bool(delete_returned),
    "v3_live_before": v3_live_before,
    "v3_backup_before": v3_backup_before,
    "v3_backup_after": v3_backup_after,
    "backup_byte_exact_before": True,
    "v4_before": v4_before,
    "v4_after": v4_after,
    "referencers_before": {
        "asset_registry": registry_refs_before,
        "editor_asset_library_confirmed": editor_refs_before,
    },
    "referencers_after": {
        "asset_registry": registry_refs_after,
        "editor_asset_library_confirmed": editor_refs_after,
    },
    "v3_directory_assets_before": v3_assets_before,
    "v3_directory_assets_after": v3_assets_after,
    "dirty_packages_before": dirty_before,
    "dirty_packages_after": dirty_after,
    "protected_assets_before": protected_before,
    "protected_assets_after": protected_after,
    "protected_assets_unchanged": True,
    "content_delta": delta,
    "asset_mutations": [V3_ASSET_PATH],
    "asset_saves": [],
    "calysto_vendor_asset_mutations": [],
}

OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
with OUTPUT_PATH.open("x", encoding="utf-8", newline="\n") as handle:
    json.dump(receipt, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log(
    "CALYSTO_V3_POLICY_RETIREMENT_PASS: "
    + json.dumps(receipt, sort_keys=True)
)
