"""Resave the Food Props closure and repair/validate all 81 UE 5.8 pickables."""

from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone

import unreal


MANIFEST_PATH = os.environ.get("CODEX_FOODKIT81_MANIFEST", "").strip()
MIGRATION_PATH = os.environ.get("CODEX_FOODKIT81_MIGRATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_FOODKIT81_REPAIR_EVIDENCE", "").strip()
EXPECTED_PARENT = "/AscentCombatFramework/Blueprints/Actors/ACF_WorldItem_BP.ACF_WorldItem_BP_C"
REGISTRY_PACKAGE = "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry"
STATUS_TABLE_PACKAGE = "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses"
ALCOHOL_IDS = {
    "BP_Drink_AlcoholBottle07",
    "BP_Drink_AlcoholBottle08",
    "BP_Drink_AlcoholBottle09",
    "BP_Drink_AlcoholBottle10",
}


def fail(message):
    unreal.log_error("CODEX_FOODKIT81_REPAIR58_FAIL: " + message)
    raise RuntimeError(message)


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(content_root, package):
    return os.path.realpath(
        os.path.join(content_root, package[len("/Game/") :].replace("/", os.sep) + ".uasset")
    )


def file_snapshot(path):
    if not os.path.isfile(path):
        fail("Expected package file is absent: " + path)
    stat = os.stat(path)
    return {"file": path, "length": stat.st_size, "mtime_ns": stat.st_mtime_ns, "sha256": sha256(path)}


def normalized_blueprint_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    return status, "".join(character for character in status.upper() if character.isalnum())


def delta_snapshot(delta):
    return {
        "entry_name": str(delta.get_editor_property("entry_name")),
        "delta_value": float(delta.get_editor_property("delta_value")),
    }


def timed_delta_snapshot(delta):
    return {
        "entry_name": str(delta.get_editor_property("entry_name")),
        "total_delta": float(delta.get_editor_property("total_delta")),
        "duration_seconds": float(delta.get_editor_property("duration_seconds")),
        "tick_interval_seconds": float(delta.get_editor_property("tick_interval_seconds")),
    }


def profile_snapshot(profile):
    return {
        "source_id": str(profile.get_editor_property("source_id")),
        "need_deltas": [delta_snapshot(value) for value in profile.get_editor_property("need_deltas")],
        "sensation_deltas": [delta_snapshot(value) for value in profile.get_editor_property("sensation_deltas")],
        "timed_need_deltas": [timed_delta_snapshot(value) for value in profile.get_editor_property("timed_need_deltas")],
        "timed_sensation_deltas": [timed_delta_snapshot(value) for value in profile.get_editor_property("timed_sensation_deltas")],
        "clamp_to_range": bool(profile.get_editor_property("clamp_to_range")),
        "stack_policy": str(profile.get_editor_property("stack_policy")),
    }


def registry_snapshot(registry):
    rows = {}
    for entry in registry.get_editor_property("entries"):
        registry_id = str(entry.get_editor_property("registry_id"))
        if registry_id in rows:
            fail("Duplicate consumable registry id: " + registry_id)
        rows[registry_id] = profile_snapshot(entry.get_editor_property("profile"))
    return rows


def extent_snapshot(mesh):
    bounds = mesh.get_bounds()
    extent = bounds.get_editor_property("box_extent")
    values = {"x": float(extent.x), "y": float(extent.y), "z": float(extent.z)}
    if min(values.values()) <= 0.0:
        fail("StaticMesh has non-positive bounds: {} {}".format(object_path(mesh), values))
    return values


def generated_class(package):
    value = unreal.EditorAssetLibrary.load_blueprint_class(package)
    if value is None:
        fail("Blueprint generated class did not load: " + package)
    return value


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8, found " + engine_version)
for required in (MANIFEST_PATH, MIGRATION_PATH, EVIDENCE_PATH):
    if not required:
        fail("All CODEX_FOODKIT81_* environment paths are required")

project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Script is not running against NoShellForWinter")
with open(os.path.realpath(MANIFEST_PATH), "r", encoding="utf-8-sig") as handle:
    manifest = json.load(handle)
with open(os.path.realpath(MIGRATION_PATH), "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if manifest.get("status") != "FOOD_KIT_81_MANIFEST_PASS" or manifest.get("entry_count") != 81:
    fail("Food manifest is not PASS/81")
if (
    migration.get("status") != "FOODKIT81_ASSETTOOLS_MIGRATION_PASS"
    or migration.get("manifest_fingerprint") != manifest.get("fingerprint")
    or migration.get("seed_mesh_count") != 81
    or migration.get("target_delta_exact") is not True
):
    fail("UE 5.7 migration evidence does not match the manifest")

entries = manifest["entries"]
closure_packages = sorted(row["package"] for row in migration["packages"])
if len(closure_packages) != migration.get("dependency_closure_count"):
    fail("Migration closure package count differs")

# Complete read-only preflight before modifying any loaded object.
closure_assets = {}
mesh_audit = {}
for package in closure_packages:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Migrated closure asset failed to load: " + package)
    closure_assets[package] = asset
for entry in entries:
    mesh_package = entry["mesh_package"]
    mesh = closure_assets.get(mesh_package)
    if mesh is None or mesh.get_class().get_name() != "StaticMesh":
        fail("Manifest mesh is not a loaded StaticMesh: " + mesh_package)
    materials = list(mesh.get_editor_property("static_materials"))
    if not materials:
        fail("StaticMesh has no material slots: " + mesh_package)
    material_paths = []
    for slot in materials:
        material = slot.get_editor_property("material_interface")
        if material is None:
            fail("StaticMesh contains an empty material slot: " + mesh_package)
        material_paths.append(object_path(material))
    mesh_audit[mesh_package] = {
        "bounds_extent": extent_snapshot(mesh),
        "material_slot_count": len(materials),
        "materials": material_paths,
    }

    item_package = entry["item_package"]
    item_blueprint = unreal.EditorAssetLibrary.load_asset(item_package)
    if item_blueprint is None or item_blueprint.get_class().get_name() != "Blueprint":
        fail("Item Blueprint failed to load: " + item_package)
    item_cdo = unreal.get_default_object(generated_class(item_package))
    item_info = item_cdo.get_editor_property("item_info")
    world_mesh = item_info.get_editor_property("world_mesh")
    if object_path(world_mesh) != mesh_package + "." + mesh_package.rsplit("/", 1)[-1]:
        fail("ItemInfo.WorldMesh differs: {} expected={} actual={}".format(item_package, mesh_package, object_path(world_mesh)))

    pickup_package = entry["pickup_package"]
    pickup_blueprint = unreal.EditorAssetLibrary.load_asset(pickup_package)
    if pickup_blueprint is None or pickup_blueprint.get_class().get_name() != "Blueprint":
        fail("Pickup Blueprint failed to load: " + pickup_package)
    parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(pickup_blueprint)
    if object_path(parent) != EXPECTED_PARENT:
        fail("Pickup parent differs: {} {}".format(pickup_package, object_path(parent)))

registry = unreal.EditorAssetLibrary.load_asset(REGISTRY_PACKAGE)
status_table = unreal.EditorAssetLibrary.load_asset(STATUS_TABLE_PACKAGE)
if registry is None or registry.get_class().get_name() != "ProjectSurvivalConsumableRegistry":
    fail("Consumable registry failed to load")
if status_table is None or status_table.get_class().get_name() != "DataTable":
    fail("Status DataTable failed to load")
registry_before = registry_snapshot(registry)
if len(registry_before) != 81 or set(ALCOHOL_IDS) - set(registry_before):
    fail("Consumable registry is not the expected 81-entry catalog")
for registry_id in ALCOHOL_IDS:
    profile = registry_before[registry_id]
    if profile["need_deltas"] != [{"entry_name": "Thirst", "delta_value": 24.0}]:
        fail("Alcohol thirst contract differs before update: {} {}".format(registry_id, profile["need_deltas"]))
preexisting_alcohol_ids = sorted(
    registry_id
    for registry_id in ALCOHOL_IDS
    if registry_before[registry_id]["sensation_deltas"] == [{"entry_name": "Alcohol", "delta_value": 30.0}]
)
if preexisting_alcohol_ids not in ([], sorted(ALCOHOL_IDS)):
    fail("Alcohol registry update is partial or inconsistent: " + repr(preexisting_alcohol_ids))
for registry_id in ALCOHOL_IDS:
    sensation_deltas = registry_before[registry_id]["sensation_deltas"]
    if sensation_deltas not in ([], [{"entry_name": "Alcohol", "delta_value": 30.0}]):
        fail("Alcohol profile has unexpected sensation deltas before update: {} {}".format(registry_id, sensation_deltas))

save_packages = closure_packages + sorted(entry["pickup_package"] for entry in entries) + [REGISTRY_PACKAGE, STATUS_TABLE_PACKAGE]
if len(save_packages) != len(set(save_packages)):
    fail("Exact save-set contains duplicate package names")
disk_before = {package: file_snapshot(package_file(content_root, package)) for package in save_packages}

# In-memory mutations: repair pickups, add Alcohol to four profiles, and ensure two status rows.
pickup_rows = []
for entry in entries:
    item_package = entry["item_package"]
    pickup_package = entry["pickup_package"]
    mesh_package = entry["mesh_package"]
    item_class = generated_class(item_package)
    pickup_blueprint = unreal.EditorAssetLibrary.load_asset(pickup_package)
    if not unreal.ProjectSurvivalConsumableEditorLibrary.configure_blueprint_as_world_item_pickup(
        pickup_blueprint, item_class, 1, 1.0
    ):
        fail("Pickup configuration returned false: " + pickup_package)

    pickup_class = generated_class(pickup_package)
    cdo = unreal.get_default_object(pickup_class)
    mesh_components = list(cdo.get_components_by_class(unreal.StaticMeshComponent))
    storage_components = [component for component in cdo.get_components_by_class(unreal.ActorComponent) if component.get_class().get_name() == "ACFStorageComponent"]
    if len(mesh_components) != 1 or len(storage_components) != 1:
        fail("Pickup component counts differ: " + pickup_package)
    mesh_component = mesh_components[0]
    assigned_mesh = mesh_component.get_editor_property("static_mesh")
    scale = mesh_component.get_editor_property("relative_scale3d")
    if object_path(assigned_mesh) != mesh_package + "." + mesh_package.rsplit("/", 1)[-1]:
        fail("Pickup mesh assignment differs: " + pickup_package)
    if any(abs(float(value) - 1.0) > 0.0001 for value in (scale.x, scale.y, scale.z)):
        fail("Pickup relative scale is not uniformly 1.0: {} {}".format(pickup_package, scale))
    storage_items = list(storage_components[0].get_editor_property("items"))
    if len(storage_items) != 1:
        fail("Pickup storage does not contain exactly one entry: " + pickup_package)
    stored_class = storage_items[0].get_editor_property("item_class")
    stored_count = int(storage_items[0].get_editor_property("count"))
    if object_path(stored_class) != object_path(item_class) or stored_count != 1:
        fail("Pickup storage contract differs: {} class={} count={}".format(pickup_package, object_path(stored_class), stored_count))
    compile_status, normalized = normalized_blueprint_status(pickup_blueprint)
    if "UPTODATE" not in normalized:
        fail("Pickup Blueprint is not UP_TO_DATE: {} {}".format(pickup_package, compile_status))
    pickup_rows.append(
        {
            "pickup_package": pickup_package,
            "item_package": item_package,
            "mesh_package": mesh_package,
            "mesh_component": mesh_component.get_name(),
            "relative_scale": [float(scale.x), float(scale.y), float(scale.z)],
            "storage_entry_count": 1,
            "storage_item_class": object_path(stored_class),
            "storage_item_count": stored_count,
            "compile_status": compile_status,
            "bounds_extent": mesh_audit[mesh_package]["bounds_extent"],
        }
    )

registry_entries = list(registry.get_editor_property("entries"))
for registry_entry in registry_entries:
    registry_id = str(registry_entry.get_editor_property("registry_id"))
    if registry_id not in ALCOHOL_IDS:
        continue
    profile = registry_entry.get_editor_property("profile")
    retained = [value for value in profile.get_editor_property("sensation_deltas") if str(value.get_editor_property("entry_name")) != "Alcohol"]
    alcohol_delta = unreal.ProjectSurvivalNeedDelta()
    alcohol_delta.set_editor_property("entry_name", "Alcohol")
    alcohol_delta.set_editor_property("delta_value", 30.0)
    retained.append(alcohol_delta)
    profile.set_editor_property("sensation_deltas", retained)
    registry_entry.set_editor_property("profile", profile)
registry.set_editor_property("entries", registry_entries)
if not unreal.ProjectSurvivalConsumableEditorLibrary.ensure_nutrition_alcohol_status_rows(status_table):
    fail("Status DataTable row authoring returned false")

registry_after = registry_snapshot(registry)
changed_registry_ids = sorted(registry_id for registry_id in registry_before if registry_before[registry_id] != registry_after[registry_id])
expected_changed_registry_ids = [] if preexisting_alcohol_ids else sorted(ALCOHOL_IDS)
if changed_registry_ids != expected_changed_registry_ids:
    fail("Registry changed outside the four alcohol profiles: " + repr(changed_registry_ids))
for registry_id in ALCOHOL_IDS:
    expected_profile = dict(registry_before[registry_id])
    expected_profile["sensation_deltas"] = [
        value for value in expected_profile["sensation_deltas"] if value["entry_name"] != "Alcohol"
    ] + [{"entry_name": "Alcohol", "delta_value": 30.0}]
    if registry_after[registry_id] != expected_profile:
        fail("Alcohol profile differs from the exact +30 change: " + registry_id)
row_names = {str(value) for value in unreal.DataTableFunctionLibrary.get_data_table_row_names(status_table)}
if not {"WellFed", "Alcoholized"}.issubset(row_names):
    fail("Status DataTable is missing WellFed or Alcoholized after authoring")

# Exact save calls only after every in-memory validation has passed.
saved = []
for package in closure_packages:
    if not unreal.EditorAssetLibrary.save_loaded_asset(closure_assets[package], only_if_is_dirty=False):
        fail("Failed to resave migrated closure asset: " + package)
    saved.append(package)
for entry in entries:
    package = entry["pickup_package"]
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("Failed to save repaired pickup: " + package)
    saved.append(package)
for package, asset in ((REGISTRY_PACKAGE, registry), (STATUS_TABLE_PACKAGE, status_table)):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        fail("Failed to save authored data asset: " + package)
    saved.append(package)
if len(saved) != len(save_packages) or set(saved) != set(save_packages):
    fail("Actual save order/set differs from the guarded save-set")

disk_after = {package: file_snapshot(package_file(content_root, package)) for package in save_packages}
unchanged_saved_packages = [package for package in save_packages if disk_before[package]["sha256"] == disk_after[package]["sha256"]]
payload = {
    "schema_version": 1,
    "generated_utc": datetime.now(timezone.utc).isoformat(),
    "status": "UE58_FOODKIT81_PICKUP_REPAIR_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "manifest_fingerprint": manifest["fingerprint"],
    "closure_package_count": len(closure_packages),
    "closure_class_counts": migration["class_counts"],
    "mesh_count": len(mesh_audit),
    "item_world_mesh_valid_count": len(entries),
    "pickup_count": len(pickup_rows),
    "pickup_mesh_visible_contract_count": len(pickup_rows),
    "pickup_scale_one_count": len(pickup_rows),
    "pickup_storage_valid_count": len(pickup_rows),
    "blueprint_up_to_date_count": len(pickup_rows),
    "mesh_audit": mesh_audit,
    "pickups": pickup_rows,
    "registry_entry_count": len(registry_after),
    "registry_preexisting_alcohol_ids": preexisting_alcohol_ids,
    "registry_expected_changed_ids_this_run": expected_changed_registry_ids,
    "registry_changed_ids": changed_registry_ids,
    "registry_before": registry_before,
    "registry_after": registry_after,
    "status_rows_present": sorted({"WellFed", "Alcoholized"}),
    "status_table_row_count": len(row_names),
    "save_package_count": len(saved),
    "saved_packages": saved,
    "unchanged_saved_packages": unchanged_saved_packages,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "source_project_opened_or_saved": False,
}
evidence_path = os.path.realpath(EVIDENCE_PATH)
expected_evidence_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration", "FoodKitAlcohol"))
if os.path.commonpath([evidence_path, expected_evidence_root]).lower() != expected_evidence_root.lower():
    fail("Repair evidence escapes FoodKitAlcohol migration root")
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
unreal.log("CODEX_FOODKIT81_REPAIR58_PASS: " + evidence_path)
