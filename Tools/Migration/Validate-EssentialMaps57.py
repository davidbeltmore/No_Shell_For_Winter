"""Read-only UE 5.7 load gate for the three essential maps harness.

Required environment:
  CODEX_ESSENTIAL_MAPS_RECEIPT
  CODEX_ESSENTIAL_MAPS_VALIDATION_EVIDENCE
  CODEX_ESSENTIAL_MAPS_EXPECTED_HARNESS_CONTENT
"""

import datetime
import hashlib
import json
import os

import unreal


RECEIPT_VALUE = os.environ.get("CODEX_ESSENTIAL_MAPS_RECEIPT", "").strip()
OUTPUT_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_HARNESS_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_EXPECTED_HARNESS_CONTENT", ""
).strip()
EXPECTED_MAPS = {
    "/Game/_Game/Hub/HUB",
    "/Game/_Game/Locations/StorySelection",
    "/Game/_Game/Locations/PCGLevel",
}


def fail(message):
    unreal.log_error("CODEX_ESSENTIAL_MAPS57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return os.path.commonpath(
            [os.path.realpath(path), os.path.realpath(root)]
        ).lower() == os.path.realpath(root).lower()
    except ValueError:
        return False


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


def asset_object_path(asset):
    try:
        return str(asset.object_path)
    except Exception:
        return "{}.{}".format(asset.package_name, asset.asset_name)


def dirty_packages(function_name):
    try:
        values = getattr(unreal.EditorLoadingAndSavingUtils, function_name)()
    except Exception as exc:
        return [], repr(exc)
    names = []
    for value in values:
        try:
            names.append(value.get_name())
        except Exception:
            names.append(str(value))
    return sorted(set(names)), ""


def inspect_world(world, package):
    if world is None:
        fail("Map load returned None: " + package)
    world_path = world.get_path_name()
    if not world_path.startswith(package + "."):
        fail("Loaded World path differs for {}: {}".format(package, world_path))
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    classes = sorted(actor.get_class().get_name() for actor in actors)
    streaming_levels = []
    try:
        streaming_levels = [str(value) for value in world.get_streaming_levels()]
    except Exception:
        pass
    partitioned = False
    partition_probe = "unavailable"
    try:
        partitioned = world.get_editor_property("world_partition") is not None
        partition_probe = "world.world_partition"
    except Exception:
        try:
            partitioned = world.get_world_partition() is not None
            partition_probe = "world.get_world_partition"
        except Exception:
            pass
    return {
        "world_path": world_path,
        "actor_count": len(actors),
        "actor_classes": classes,
        "streaming_levels": streaming_levels,
        "world_partitioned": partitioned,
        "world_partition_probe": partition_probe,
    }


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not RECEIPT_VALUE or not OUTPUT_VALUE or not EXPECTED_HARNESS_VALUE:
    fail("All documented EssentialMaps validation variables are required")

receipt_path = os.path.realpath(RECEIPT_VALUE)
output_path = os.path.realpath(OUTPUT_VALUE)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_harness = os.path.realpath(EXPECTED_HARNESS_VALUE)
if harness_content.lower() != expected_harness.lower():
    fail("Commandlet is not running in the audited detached harness")
if not os.path.isfile(receipt_path):
    fail("Harness receipt is absent")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "ISOLATED_ESSENTIAL_MAPS57_HARNESS_PASS":
    fail("Harness receipt is not PASS")
if os.path.realpath(receipt.get("harness_content", "")).lower() != harness_content.lower():
    fail("Receipt names a different harness Content")
target_root = os.path.realpath(receipt.get("target_root", ""))
source_root = os.path.realpath(receipt.get("source_root", ""))
if not is_under(harness_content, os.path.join(target_root, "Saved", "Migration")):
    fail("Harness escapes target Saved/Migration staging")
if is_under(harness_content, os.path.join(target_root, "Content")) or is_under(
    harness_content, source_root
):
    fail("Harness unexpectedly lives inside source or live target Content")
if not is_under(output_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Validation evidence escapes target Saved/Migration")

maps = list(receipt.get("maps", []))
map_packages = {row.get("package") for row in maps}
if len(maps) != 3 or map_packages != EXPECTED_MAPS:
    fail("Receipt essential map set differs")
migration_rows = list(receipt.get("migration_packages", []))
if len(migration_rows) != int(receipt.get("migration_package_count", -1)):
    fail("Receipt migration package count differs")
if not EXPECTED_MAPS.issubset({row.get("package") for row in migration_rows}):
    fail("A required map is absent from migration seeds")
if any(
    str(row.get("package", "")).startswith(("/Game/FullSample", "/Game/DazToUnreal"))
    for row in migration_rows
):
    fail("Protected package entered the migration seed set")

all_seed_files = []
for package_row in migration_rows:
    package = str(package_row.get("package", ""))
    if not package.startswith("/Game/"):
        fail("Invalid migration package: " + package)
    files = list(package_row.get("files", []))
    primary_extension = str(package_row.get("primary_extension", "")).lower()
    primaries = [
        row
        for row in files
        if os.path.splitext(str(row.get("relative_file", "")))[1].lower()
        == primary_extension
    ]
    if len(primaries) != 1:
        fail("Migration package has no unique primary: " + package)
    for row in files:
        source_file = os.path.realpath(str(row.get("source", "")))
        staged_file = os.path.realpath(str(row.get("staged", "")))
        if not is_under(source_file, os.path.join(source_root, "Content")):
            fail("Receipt source file escapes source Content: " + package)
        if not is_under(staged_file, harness_content):
            fail("Receipt staged file escapes harness: " + package)
        expected_length = int(row.get("length", -1))
        expected_hash = str(row.get("sha256", "")).upper()
        for label, path in (("source", source_file), ("staged", staged_file)):
            if not os.path.isfile(path):
                fail("{} file is absent for {}".format(label, package))
            if os.path.getsize(path) != expected_length or sha256(path) != expected_hash:
                fail("{} bytes differ for {}".format(label, package))
        all_seed_files.append((package, row, source_file, staged_file))

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()
registry_rows = []
for package_row in migration_rows:
    package = str(package_row["package"])
    assets = list(
        registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    )
    if not assets:
        fail("Harness Asset Registry has no on-disk asset for " + package)
    registry_rows.append(
        {
            "package": package,
            "asset_count": len(assets),
            "object_paths": sorted(asset_object_path(asset) for asset in assets),
        }
    )

map_results = []
options = dependency_options()
dirty_content_before, dirty_content_before_error = dirty_packages(
    "get_dirty_content_packages"
)
dirty_maps_before, dirty_maps_before_error = dirty_packages("get_dirty_map_packages")
for map_row in maps:
    package = str(map_row["package"])
    dependencies = sorted(
        {str(value) for value in registry.get_dependencies(package, options)}
    )
    expected = sorted(set(map_row.get("manifest_direct_dependencies", [])))
    dependency_delta = {
        "missing": sorted(set(expected) - set(dependencies)),
        "additional": sorted(set(dependencies) - set(expected)),
    }
    if map_row.get("source_registry_present") and (
        dependency_delta["missing"] or dependency_delta["additional"]
    ):
        fail("Direct dependency set differs for {}: {}".format(package, dependency_delta))
    world = unreal.EditorLoadingAndSavingUtils.load_map(package)
    loaded = inspect_world(world, package)
    loaded.update(
        {
            "package": package,
            "dependencies": dependencies,
            "manifest_dependency_comparison_enforced": bool(
                map_row.get("source_registry_present")
            ),
            "dependency_delta": dependency_delta,
            "staged_external_actor_package_count": int(
                map_row.get("external_actor_package_count", 0)
            ),
            "staged_external_object_package_count": int(
                map_row.get("external_object_package_count", 0)
            ),
            "staged_built_data": bool(map_row.get("built_data_present")),
        }
    )
    map_results.append(loaded)

for package, row, source_file, staged_file in all_seed_files:
    expected_length = int(row["length"])
    expected_hash = str(row["sha256"]).upper()
    if os.path.getsize(source_file) != expected_length or sha256(source_file) != expected_hash:
        fail("Source changed during read-only load: " + package)
    if os.path.getsize(staged_file) != expected_length or sha256(staged_file) != expected_hash:
        fail("Staged file changed during read-only load: " + package)

dirty_content_after, dirty_content_after_error = dirty_packages(
    "get_dirty_content_packages"
)
dirty_maps_after, dirty_maps_after_error = dirty_packages("get_dirty_map_packages")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_ESSENTIAL_MAPS_READ_ONLY_LOAD_PASS",
    "engine_version": engine_version,
    "run_id": receipt.get("run_id"),
    "receipt": receipt_path,
    "receipt_sha256": sha256(receipt_path),
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "map_count": len(map_results),
    "migration_package_count": len(migration_rows),
    "registry_packages": registry_rows,
    "maps": map_results,
    "dirty_content_packages_before": dirty_content_before,
    "dirty_content_packages_after": dirty_content_after,
    "dirty_map_packages_before": dirty_maps_before,
    "dirty_map_packages_after": dirty_maps_after,
    "dirty_content_probe_error_before": dirty_content_before_error,
    "dirty_content_probe_error_after": dirty_content_after_error,
    "dirty_map_probe_error_before": dirty_maps_before_error,
    "dirty_map_probe_error_after": dirty_maps_after_error,
    "map_load_requested": True,
    "map_save_requested": False,
    "packages_saved": 0,
    "source_tree_mounted": False,
    "target_content_writes": 0,
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
temporary = output_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, output_path)
unreal.log("CODEX_ESSENTIAL_MAPS57_VALIDATION_PASS: " + output_path)
