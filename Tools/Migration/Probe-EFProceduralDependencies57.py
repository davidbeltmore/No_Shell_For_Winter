"""Inspect three exact-hash staged procedural seeds in the UE 5.7 Asset Registry."""

import csv
import datetime
import json
import os

import unreal


SEEDS = (
    "/Game/Procedural/Maps/DungeonGeneration",
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
)
OUTPUT_PATH = os.environ.get("CODEX_EFPROCEDURAL_PROBE_OUTPUT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get("CODEX_EXPECTED_HARNESS_CONTENT", "").strip()
TARGET_MANIFEST = os.environ.get("CODEX_TARGET_MANIFEST", "").strip()


def fail(message):
    unreal.log_error("CODEX_EFPROCEDURAL57_PROBE_FAIL: " + message)
    raise RuntimeError(message)


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not OUTPUT_PATH or not EXPECTED_HARNESS_CONTENT or not TARGET_MANIFEST:
    fail("Probe output, expected harness Content, and target manifest are required")

project_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_content = os.path.realpath(EXPECTED_HARNESS_CONTENT)
if project_content.lower() != expected_content.lower():
    fail(
        "Harness Content invariant failed: {!r} != {!r}".format(
            project_content, expected_content
        )
    )
if not os.path.isfile(TARGET_MANIFEST):
    fail("Target migration manifest is absent: " + TARGET_MANIFEST)

manifest = {}
with open(TARGET_MANIFEST, "r", encoding="utf-8-sig", newline="") as handle:
    for row in csv.DictReader(handle):
        manifest[row["PackageName"]] = {
            "presence": row["Presence"],
            "classification": row["Classification"],
            "action": row["Action"],
            "source_length": row["SourceLength"],
            "source_sha256": row["SourceSHA256"],
        }

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Procedural", "/Game/Calysto/Dungeon"], True)
registry.wait_for_completion()

dependency_options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
    "include_searchable_names",
):
    try:
        dependency_options.set_editor_property(
            property_name, property_name != "include_searchable_names"
        )
    except Exception:
        pass

seed_results = []
all_direct_dependencies = set()
for package in SEEDS:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    if not assets:
        fail("Staged seed was not registered: " + package)
    dependencies = sorted(
        {str(item) for item in registry.get_dependencies(package, dependency_options)}
    )
    all_direct_dependencies.update(dependencies)
    seed_results.append(
        {
            "package": package,
            "class": str(assets[0].asset_class_path),
            "registry_entry_count": len(assets),
            "direct_dependency_count": len(dependencies),
            "direct_dependencies": dependencies,
            "manifest": manifest.get(package, {}),
        }
    )

dependency_manifest = []
for package in sorted(all_direct_dependencies):
    dependency_manifest.append(
        {
            "package": package,
            "manifest": manifest.get(package, {}),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "STAGED_SEED_REGISTRY_DEPENDENCY_PROBE_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "harness_content": project_content,
    "seeds": seed_results,
    "unique_direct_dependency_count": len(all_direct_dependencies),
    "direct_dependency_manifest": dependency_manifest,
    "packages_loaded": 0,
    "packages_saved": 0,
}
output_path = os.path.realpath(OUTPUT_PATH)
if not output_path.lower().startswith(
    os.path.realpath(os.path.join(os.environ["CODEX_EXPECTED_TARGET_ROOT"], "Saved", "Migration")).lower()
    + os.sep
):
    fail("Probe output escapes target Saved/Migration")
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFPROCEDURAL57_PROBE_PASS: " + output_path)
