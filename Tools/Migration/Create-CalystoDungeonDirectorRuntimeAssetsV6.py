"""Create the three definitive, unversioned Calysto runtime Blueprints.

The first run duplicates the byte-frozen V4-named Blueprints to new object
paths, reparents the chest copies to the unversioned native chest class,
compiles, and saves only the three new packages. Existing destinations are
validated read-only. Source assets are never renamed or saved, which keeps all
legacy policy references stable until controlled retirement.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
PACKAGE_SUFFIXES = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")

CHEST_PARENT = "/Script/EFProjectSystemsGameplay.ProjectCalystoChest"
ARMOR_PARENT = "/Script/InventorySystem.ACFWorldItem"
SPECS = (
    {
        "source": "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4",
        "destination": "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
        "parent": CHEST_PARENT,
        "source_sha256": "3D7FA02BC0B3A11315E6CB130A479FA6099B792D79DE288B374CF62684853117",
    },
    {
        "source": "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4",
        "destination": "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
        "parent": CHEST_PARENT,
        "source_sha256": "45D439633454B551979F594A7ADC00D399520598541163F425BD01B4B1078205",
    },
    {
        "source": "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4",
        "destination": "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
        "parent": ARMOR_PARENT,
        "source_sha256": "DC92929C9D5FA12B6672BBCB2168BBE360964D1D8A860806CF066A19D9F23DFD",
    },
)
RECEIPT = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV6"
    / "CreateRuntimeAssetsV6.json"
)


class Blocked(RuntimeError):
    pass


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def package_file(package: str) -> Path:
    return CONTENT_ROOT / Path(package[len("/Game/") :] + ".uasset")


def snapshot_content() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for path in CONTENT_ROOT.rglob("*"):
        if not path.is_file() or path.suffix.casefold() not in PACKAGE_SUFFIXES:
            continue
        stat = path.stat()
        result[path.relative_to(CONTENT_ROOT).as_posix()] = (
            int(stat.st_size), int(stat.st_mtime_ns), sha256(path)
        )
    return result


def changed_packages(before: dict[str, Any], after: dict[str, Any]) -> tuple[list[str], list[str]]:
    files = sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path))
    packages = sorted({"/Game/" + path.rsplit(".", 1)[0] for path in files})
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
        fail("Calysto V6 runtime-asset creation requires UE 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
        fail(f"Wrong writable target: {PROJECT_ROOT}")


def object_path(value: Any) -> str:
    return "" if value is None else str(value.get_path_name())


def load_exact_class(path: str) -> Any:
    value = unreal.load_class(None, path)
    if value is None:
        raise Blocked(f"BLOCKED_NATIVE_UNVERSIONED_CLASS_MISSING: {path}")
    if object_path(value) != path:
        fail(f"{path} resolved through a redirect to {object_path(value)}")
    return value


def blueprint_state(package: str, expected_parent: str, compile_asset: bool) -> dict[str, Any]:
    blueprint = unreal.load_asset(package)
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail(f"Expected Blueprint is missing: {package}")
    if compile_asset:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    parent_path = object_path(parent)
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in status.upper() if character.isalnum())
    asset_name = package.rsplit("/", 1)[-1]
    generated = unreal.load_class(None, package + "." + asset_name + "_C")
    expected_parent_class = load_exact_class(expected_parent)
    if parent_path != expected_parent:
        fail(
            f"Blueprint parent mismatch for {package}: "
            f"actual={parent_path} expected={expected_parent}"
        )
    if "UPTODATE" not in normalized:
        fail(f"Blueprint compile is not up to date: {package} status={status}")
    if generated is None or not unreal.MathLibrary.class_is_child_of(
        generated, expected_parent_class
    ):
        fail(f"Generated class ancestry is invalid: {package}")
    disk_path = package_file(package)
    return {
        "package": package,
        "parent": parent_path,
        "compile_status": status,
        "generated_class": object_path(generated),
        "package_sha256": sha256(disk_path) if disk_path.is_file() else "PENDING_SAVE",
    }


validate_context()
before = snapshot_content()
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
}

try:
    destination_presence = [
        unreal.EditorAssetLibrary.does_asset_exist(spec["destination"]) for spec in SPECS
    ]
    if any(destination_presence) and not all(destination_presence):
        fail(
            "Partial unversioned Blueprint set exists; refusing an ambiguous repair: "
            f"{destination_presence}"
        )

    if all(destination_presence):
        result["mode"] = "VALIDATE_EXISTING_READ_ONLY"
        dirty_destinations = sorted(
            spec["destination"] for spec in SPECS
            if spec["destination"] in dirty_packages()
        )
        if dirty_destinations:
            fail(
                "Save or discard dirty definitive runtime Blueprints before validation: "
                f"{dirty_destinations}"
            )
        result["blueprints"] = [
            blueprint_state(spec["destination"], spec["parent"], compile_asset=False)
            for spec in SPECS
        ]
    else:
        result["mode"] = "CREATE_ONCE_FROM_FROZEN_BLUEPRINTS"
        dirty = dirty_packages()
        result["source_assets"] = []
        for spec in SPECS:
            source_file = package_file(spec["source"])
            if not source_file.is_file():
                fail(f"Frozen source Blueprint is missing: {spec['source']}")
            actual_hash = sha256(source_file)
            if actual_hash != spec["source_sha256"]:
                fail(
                    f"Frozen source Blueprint drift: {spec['source']} "
                    f"actual={actual_hash} expected={spec['source_sha256']}"
                )
            if spec["source"] in dirty:
                fail(f"Frozen source Blueprint is dirty: {spec['source']}")
            source_asset = unreal.load_asset(spec["source"])
            if source_asset is None or source_asset.get_class().get_name() != "Blueprint":
                fail(f"Frozen source is not a Blueprint: {spec['source']}")
            result["source_assets"].append(
                {"package": spec["source"], "sha256": actual_hash, "dirty": False}
            )

        parents = {path: load_exact_class(path) for path in {spec["parent"] for spec in SPECS}}
        states: list[dict[str, Any]] = []
        for spec in SPECS:
            duplicated = unreal.EditorAssetLibrary.duplicate_asset(
                spec["source"], spec["destination"]
            )
            if duplicated is None:
                fail(
                    f"Could not duplicate {spec['source']} to {spec['destination']}"
                )
            created.append(spec["destination"])
            outcome = unreal.BlueprintEditorLibrary.reparent_blueprint(
                duplicated, parents[spec["parent"]]
            )
            if outcome is False:
                fail(f"Reparent returned false: {spec['destination']}")
            states.append(
                blueprint_state(spec["destination"], spec["parent"], compile_asset=True)
            )

        for spec in SPECS:
            if not unreal.EditorAssetLibrary.save_asset(
                spec["destination"], only_if_is_dirty=False
            ):
                fail(f"Could not save new Blueprint: {spec['destination']}")
            saved.append(spec["destination"])
        result["blueprints"] = [
            blueprint_state(spec["destination"], spec["parent"], compile_asset=False)
            for spec in SPECS
        ]

        source_after = {
            spec["source"]: sha256(package_file(spec["source"])) for spec in SPECS
        }
        expected_source_after = {
            spec["source"]: spec["source_sha256"] for spec in SPECS
        }
        if source_after != expected_source_after:
            fail("A frozen source Blueprint changed during V6 duplication")

    after = snapshot_content()
    files, packages = changed_packages(before, after)
    expected_packages = sorted(spec["destination"] for spec in SPECS) if created else []
    if packages != expected_packages:
        fail(f"Unexpected Content delta: actual={packages} expected={expected_packages}")
    if sorted(saved) != expected_packages:
        fail(f"Unexpected save set: actual={saved} expected={expected_packages}")
    result["changed_content_files"] = files
    result["asset_mutations"] = packages
    result["created"] = created
    result["saved"] = saved
    result["status"] = "PASS"
except Blocked as exc:
    result["status"] = "BLOCKED_NATIVE_UNVERSIONED_CLASS_MISSING"
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
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
    final = snapshot_content()
    files, packages = changed_packages(before, final)
    result["changed_content_files_final"] = files
    result["asset_mutations_final"] = packages
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    write_receipt_append_only(RECEIPT, result)
    unreal.log("CALYSTO_V6_RUNTIME_ASSETS_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(f"{result['status']}: {result.get('error', 'unknown failure')}")
