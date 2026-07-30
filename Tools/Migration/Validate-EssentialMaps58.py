"""Fresh UE 5.8 read-only integrity gate for migrated essential maps.

Required environment:
  CODEX_ESSENTIAL_MAPS_MIGRATION_EVIDENCE
  CODEX_ESSENTIAL_MAPS_58_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


MIGRATION_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_MIGRATION_EVIDENCE", ""
).strip()
OUTPUT_VALUE = os.environ.get("CODEX_ESSENTIAL_MAPS_58_EVIDENCE", "").strip()
EXPECTED_MAPS = (
    "/Game/_Game/Hub/HUB",
    "/Game/_Game/Locations/StorySelection",
    "/Game/_Game/Locations/PCGLevel",
)


def fail(message):
    unreal.log_error("CODEX_ESSENTIAL_MAPS58_VALIDATION_FAIL: " + message)
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


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not MIGRATION_VALUE or not OUTPUT_VALUE:
    fail("Both documented EssentialMaps UE 5.8 variables are required")
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
migration_path = os.path.realpath(MIGRATION_VALUE)
output_path = os.path.realpath(OUTPUT_VALUE)
if not is_under(migration_path, os.path.join(project_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")
if not is_under(output_path, os.path.join(project_root, "Saved", "Migration")):
    fail("UE 5.8 evidence escapes target Saved/Migration")
with open(migration_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_ESSENTIAL_MAPS57_MIGRATION_PASS":
    fail("UE 5.7 migration evidence is not PASS")
if migration.get("target_delta_exact") is not True:
    fail("UE 5.7 migration target delta is not exact")
if os.path.realpath(migration.get("target_root", "")).lower() != project_root.lower():
    fail("Migration evidence belongs to a different target project")

packages = list(migration.get("packages", []))
if len(packages) != int(migration.get("migration_package_count", -1)):
    fail("Migration package count differs")
package_names = {row.get("package") for row in packages}
if not set(EXPECTED_MAPS).issubset(package_names):
    fail("A required essential map is absent from migration evidence")

disk_before = {}
for package_row in packages:
    package = str(package_row.get("package", ""))
    for file_row in package_row.get("files", []):
        path = os.path.realpath(str(file_row.get("file", "")))
        if not is_under(path, content_root) or not os.path.isfile(path):
            fail("Migrated package file is absent or escapes Content: " + package)
        expected_length = int(file_row.get("length", -1))
        expected_hash = str(file_row.get("sha256", "")).upper()
        actual = {"length": os.path.getsize(path), "sha256": sha256(path)}
        if actual["length"] != expected_length or actual["sha256"] != expected_hash:
            fail("Migrated package differs from UE 5.7 evidence: " + package)
        disk_before[path] = actual

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()
for package in sorted(package_names):
    assets = list(
        registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    )
    if not assets:
        fail("UE 5.8 Asset Registry has no on-disk asset for " + package)

options = dependency_options()
map_results = []
for package in EXPECTED_MAPS:
    dependencies = sorted(
        {str(value) for value in registry.get_dependencies(package, options)}
    )
    missing_game_dependencies = []
    for dependency in dependencies:
        if not dependency.startswith("/Game/"):
            continue
        if not list(
            registry.get_assets_by_package_name(
                dependency, include_only_on_disk_assets=True
            )
        ):
            missing_game_dependencies.append(dependency)
    world = unreal.EditorLoadingAndSavingUtils.load_map(package)
    if world is None or not world.get_path_name().startswith(package + "."):
        fail("UE 5.8 map load failed: " + package)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    partitioned = False
    try:
        partitioned = world.get_editor_property("world_partition") is not None
    except Exception:
        try:
            partitioned = world.get_world_partition() is not None
        except Exception:
            pass
    map_results.append(
        {
            "package": package,
            "world_path": world.get_path_name(),
            "actor_count": len(actors),
            "actor_classes": sorted(actor.get_class().get_name() for actor in actors),
            "world_partitioned": partitioned,
            "dependencies": dependencies,
            "missing_game_dependencies": missing_game_dependencies,
        }
    )

for path, before in disk_before.items():
    after = {"length": os.path.getsize(path), "sha256": sha256(path)}
    if after != before:
        fail("Read-only UE 5.8 validation changed a migrated package: " + path)

unresolved_game_dependencies = sorted(
    {
        dependency
        for row in map_results
        for dependency in row["missing_game_dependencies"]
    }
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_ESSENTIAL_MAPS_FRESH_READ_ONLY_LOAD_PASS"
        if not unresolved_game_dependencies
        else "UE58_ESSENTIAL_MAPS_DEPENDENCY_GAP"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "run_id": migration.get("run_id"),
    "migration_evidence": migration_path,
    "migration_evidence_sha256": sha256(migration_path),
    "map_count": len(map_results),
    "migration_package_count": len(packages),
    "maps": map_results,
    "unresolved_game_dependencies": unresolved_game_dependencies,
    "map_load_requested": True,
    "map_save_requested": False,
    "packages_saved": 0,
    "disk_bytes_unchanged": True,
    "visual_qa": "OUT_OF_SCOPE_BY_USER",
    "post_validation_protected_invariant_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
temporary = output_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, output_path)
if unresolved_game_dependencies:
    fail(
        "Essential maps retain unresolved /Game dependencies; evidence: "
        + output_path
    )
unreal.log("CODEX_ESSENTIAL_MAPS58_VALIDATION_PASS: " + output_path)
