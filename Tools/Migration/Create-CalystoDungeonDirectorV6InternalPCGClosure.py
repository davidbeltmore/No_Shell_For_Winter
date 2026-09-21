"""Create or validate the two cooked-safe internal Calysto V6 PCG graphs.

The first run duplicates the exact vendor SetDungeonMesh/AddRamps graphs into
EFProcedural, then delegates the seven reviewed subgraph substitutions to a
native structural validator. Only the two new packages are saved. Later runs
are strictly read-only. Vendor packages and BP_MassiveDungeon are never saved.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
PROJECT_CONTENT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT = (PROJECT_ROOT / "Plugins" / "EFProcedural" / "Content").resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
PACKAGE_SUFFIXES = (".uasset", ".uexp", ".ubulk", ".uptnl")

SOURCE_SPECS = (
    {
        "package": "/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh",
        "object": "/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh",
        "file": PROJECT_CONTENT / "Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.uasset",
        "sha256": "7A185669F120870E8A201B10DEBC226E3C4222988528BAF1F4F7EA3650280318",
    },
    {
        "package": "/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps",
        "object": "/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps",
        "file": PROJECT_CONTENT / "Calysto/Dungeon/PCG/Function/PCG_AddRamps.uasset",
        "sha256": "C21C904C4531E294D47D4911BA4FF5AED343B0762D5458F45576994A93E238EE",
    },
    {
        "package": "/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple",
        "object": "/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple",
        "file": PROJECT_CONTENT / "Calysto/Shared/PCG/PCG_ObjectTransformSimple.uasset",
        "sha256": "D390E9D2188A1AA64F360D404E61DC7C0827830A9B0307A6C460E7078D446819",
    },
    {
        "package": "/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon",
        "object": "/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon",
        "file": PROJECT_CONTENT / "Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.uasset",
        "sha256": "706CE3769CD338023278D75AFF4FEE279721A9AD4AD9A83A5D281D84E004CE47",
    },
)
DESTINATION_DIRECTORY = "/EFProcedural/Calysto/Internal/PCG"
DESTINATION_SPECS = (
    {
        "source": "/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps",
        "package": f"{DESTINATION_DIRECTORY}/PCG_AddRampsCookedSafe",
        "object": f"{DESTINATION_DIRECTORY}/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe",
    },
    {
        "source": "/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh",
        "package": f"{DESTINATION_DIRECTORY}/PCG_SetDungeonMeshCookedSafe",
        "object": f"{DESTINATION_DIRECTORY}/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe",
    },
)
RECEIPT = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV6"
    / "CreateInternalPCGClosureV6.json"
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def destination_file(package: str) -> Path:
    prefix = "/EFProcedural/"
    if not package.startswith(prefix):
        fail(f"Destination escapes EFProcedural: {package}")
    return PLUGIN_CONTENT / (package[len(prefix) :] + ".uasset")


def snapshot_plugin_content() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    if not PLUGIN_CONTENT.is_dir():
        return result
    for path in PLUGIN_CONTENT.rglob("*"):
        if not path.is_file() or path.suffix.casefold() not in PACKAGE_SUFFIXES:
            continue
        stat = path.stat()
        result[path.relative_to(PLUGIN_CONTENT).as_posix()] = (
            int(stat.st_size), int(stat.st_mtime_ns), sha256(path)
        )
    return result


def changed_plugin_packages(
    before: dict[str, Any], after: dict[str, Any]
) -> tuple[list[str], list[str]]:
    files = sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )
    packages = sorted(
        {"/EFProcedural/" + path.rsplit(".", 1)[0] for path in files}
    )
    return files, packages


def dirty_packages() -> set[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {str(value.get_path_name()) for value in values}


def write_receipt_append_only(path: Path, payload: dict[str, Any]) -> Path:
    """Preserve every creator result before updating the canonical latest receipt."""
    stamp = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("generated_utc", "unknown"))
    ).strip("_")
    mode = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("mode", "UNKNOWN"))
    ).strip("_")
    history = path.parent / "ReceiptHistory" / path.stem / f"{stamp}_{mode}.json"
    history.parent.mkdir(parents=True, exist_ok=True)
    payload["append_only_receipt"] = str(history)
    serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    with history.open("x", encoding="utf-8") as stream:
        stream.write(serialized)
    path.write_text(serialized, encoding="utf-8")
    return history


def validate_context() -> None:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Calysto V6 internal PCG creation requires UE 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong project file: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
        fail(f"Wrong writable target: actual={PROJECT_ROOT} expected={EXPECTED_ROOT}")


def validate_source_files() -> dict[str, str]:
    result: dict[str, str] = {}
    dirty = dirty_packages()
    for spec in SOURCE_SPECS:
        if not spec["file"].is_file():
            fail(f"Protected vendor source is missing: {spec['file']}")
        actual = sha256(spec["file"])
        if actual != spec["sha256"]:
            fail(
                f"Protected vendor source hash drift: {spec['package']} "
                f"actual={actual} expected={spec['sha256']}"
            )
        if spec["package"] in dirty:
            fail(f"Protected vendor source is dirty: {spec['package']}")
        asset = unreal.load_asset(spec["package"])
        if asset is None or str(asset.get_path_name()) != spec["object"]:
            fail(f"Unable to load exact protected graph: {spec['object']}")
        if str(asset.get_class().get_name()) != "PCGGraph":
            fail(f"Protected source is not a PCGGraph: {spec['object']}")
        result[spec["package"]] = actual
    return dict(sorted(result.items()))


def native_function(name: str) -> Any:
    library = getattr(unreal, "EFCalystoPCGInternalClosureEditorLibrary", None)
    function = getattr(library, name, None) if library is not None else None
    if not callable(function):
        fail(
            "EFProceduralEditor native PCG closure bridge is unavailable; "
            f"cold-build the Editor before running this creator ({name})"
        )
    return function


def call_native(name: str, label: str) -> None:
    error = str(native_function(name)())
    if error:
        fail(f"{label}: {error}")


def validate_destinations() -> dict[str, Any]:
    call_native(
        "validate_internal_cooked_closure",
        "Native internal closure structural validation failed",
    )
    result: dict[str, Any] = {}
    for spec in DESTINATION_SPECS:
        asset = unreal.load_asset(spec["package"])
        if asset is None or str(asset.get_path_name()) != spec["object"]:
            fail(f"Missing exact internal PCG graph: {spec['object']}")
        if str(asset.get_class().get_name()) != "PCGGraph":
            fail(f"Internal asset is not a PCGGraph: {spec['object']}")
        disk = destination_file(spec["package"])
        result[spec["package"]] = {
            "object": spec["object"],
            "class": str(asset.get_class().get_path_name()),
            "package_sha256": sha256(disk) if disk.is_file() else "PENDING_SAVE",
        }
    return dict(sorted(result.items()))


validate_context()
before_plugin = snapshot_plugin_content()
before_dirty = dirty_packages()
created: list[str] = []
saved: list[str] = []
result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "status": "FAIL",
    "mode": "PREFLIGHT",
    "created": [],
    "saved": [],
    "asset_mutations": [],
    "vendor_assets_saved": [],
}

try:
    # Resolve both functions before any asset can be created.
    native_function("prepare_new_internal_cooked_closure")
    native_function("validate_internal_cooked_closure")
    source_hashes_before = validate_source_files()
    result["protected_vendor_hashes"] = source_hashes_before

    # The plugin mount can be available before its Asset Registry scan has
    # announced newly-created packages on the next unattended editor launch.
    # Disk identity is authoritative for create-once mode; exact object/class
    # validation below still loads both packages and validates their graph
    # structure before accepting them.
    presence = [
        destination_file(spec["package"]).is_file()
        for spec in DESTINATION_SPECS
    ]
    if any(presence) and not all(presence):
        fail(
            "Partial internal PCG closure exists; refusing ambiguous repair: "
            f"{presence}"
        )

    if all(presence):
        result["mode"] = "VALIDATE_EXISTING_READ_ONLY"
        dirty_destinations = sorted(
            spec["package"] for spec in DESTINATION_SPECS
            if spec["package"] in dirty_packages()
        )
        if dirty_destinations:
            fail(
                "Save or discard dirty internal PCG graphs before validation: "
                f"{dirty_destinations}"
            )
        result["assets"] = validate_destinations()
    else:
        result["mode"] = "CREATE_ONCE_FROM_FROZEN_VENDOR_GRAPHS"
        unreal.EditorAssetLibrary.make_directory(DESTINATION_DIRECTORY)
        for spec in DESTINATION_SPECS:
            duplicated = unreal.EditorAssetLibrary.duplicate_asset(
                spec["source"], spec["package"]
            )
            if duplicated is None or str(duplicated.get_path_name()) != spec["object"]:
                fail(
                    f"Could not duplicate {spec['source']} to exact object "
                    f"{spec['object']}"
                )
            created.append(spec["package"])

        call_native(
            "prepare_new_internal_cooked_closure",
            "Native seven-reference closure preparation failed",
        )
        call_native(
            "validate_internal_cooked_closure",
            "Prepared internal closure failed its shared runtime validator",
        )
        for spec in DESTINATION_SPECS:
            asset = unreal.load_asset(spec["package"])
            if asset is None or not unreal.EditorAssetLibrary.save_loaded_asset(
                asset, only_if_is_dirty=False
            ):
                fail(f"Could not save new internal graph: {spec['package']}")
            saved.append(spec["package"])
        result["assets"] = validate_destinations()

    source_hashes_after = validate_source_files()
    if source_hashes_after != source_hashes_before:
        fail("A protected vendor graph changed during internal closure creation")

    after_plugin = snapshot_plugin_content()
    changed_files, changed_packages = changed_plugin_packages(
        before_plugin, after_plugin
    )
    expected_changes = sorted(created)
    if changed_packages != expected_changes:
        fail(
            "Unexpected EFProcedural Content delta: "
            f"actual={changed_packages} expected={expected_changes}"
        )
    if sorted(saved) != expected_changes:
        fail(f"Unexpected save set: actual={saved} expected={expected_changes}")
    newly_dirty = sorted(dirty_packages() - before_dirty)
    if newly_dirty:
        fail(f"Creator left newly dirty packages: {newly_dirty}")

    result["changed_content_files"] = changed_files
    result["asset_mutations"] = changed_packages
    result["created"] = created
    result["saved"] = saved
    result["idempotent_read_only"] = not created
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
finally:
    if result["status"] != "PASS" and created:
        cleanup: dict[str, bool] = {}
        for package in reversed(created):
            try:
                cleanup[package] = bool(unreal.EditorAssetLibrary.delete_asset(package))
            except Exception:
                cleanup[package] = False
        result["failed_creation_cleanup"] = cleanup
    final_plugin = snapshot_plugin_content()
    final_files, final_packages = changed_plugin_packages(
        before_plugin, final_plugin
    )
    result["changed_content_files_final"] = final_files
    result["asset_mutations_final"] = final_packages
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    write_receipt_append_only(RECEIPT, result)
    unreal.log("CALYSTO_V6_INTERNAL_PCG_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Internal PCG closure creation failed"))
