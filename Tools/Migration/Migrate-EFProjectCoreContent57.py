"""Migrate an exact 19-package EFProjectSystems content batch via UE 5.7 AssetTools.

The script is allowed to run only in the isolated target-hosted harness created
by Prepare-EFProjectCoreContent57Harness.ps1. It never mounts the source tree.
"""

import datetime
import hashlib
import json
import os

import unreal


SEEDS = (
    "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses",
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry",
    "/Game/Data/CharacterBackground/DT_ProjectBackstories",
    "/Game/Data/CharacterBackground/DT_ProjectProfessions",
    "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget",
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget",
)
EXPECTED_PACKAGES = (
    *SEEDS,
    "/Game/_Game/Icons/Bleeding",
    "/Game/_Game/Icons/Dirt",
    "/Game/_Game/Icons/Dizzy",
    "/Game/_Game/Icons/Exhausted",
    "/Game/_Game/Icons/extremepain_transparent",
    "/Game/_Game/Icons/Fear",
    "/Game/_Game/Icons/frenzy_transparent",
    "/Game/_Game/Icons/gracestep_transparent",
    "/Game/_Game/Icons/Hungry",
    "/Game/_Game/Icons/knockedout_transparent",
    "/Game/_Game/Icons/Orgasm",
    "/Game/_Game/Icons/SleepDeprived",
    "/Game/_Game/Icons/Thirst",
)

DESTINATION = os.environ.get("CODEX_EFCORE_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_EFCORE_EVIDENCE", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_EFCORE_CONTENT57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(content_root, package_name):
    relative = package_name[len("/Game/") :].replace("/", os.sep)
    return os.path.realpath(os.path.join(content_root, relative + ".uasset"))


def list_packages(content_root):
    result = set()
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if name.lower().endswith((".uasset", ".umap")):
                result.add(os.path.realpath(os.path.join(root, name)).lower())
    return result


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        DESTINATION,
        EVIDENCE_PATH,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All CODEX_EFCORE migration environment variables are required")

destination = os.path.realpath(DESTINATION)
target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
expected_destination = os.path.realpath(os.path.join(target_root, "Content"))
if destination.lower() != expected_destination.lower():
    fail(
        "Destination invariant failed: {!r} != {!r}".format(
            destination, expected_destination
        )
    )
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")

harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("The commandlet is not running inside the audited isolated harness")
if harness_content.lower().startswith(destination.lower() + os.sep):
    fail("Harness Content unexpectedly lives below target Content")

evidence_path = os.path.realpath(EVIDENCE_PATH)
evidence_root = os.path.realpath(os.path.join(target_root, "Saved", "Migration"))
if not evidence_path.lower().startswith(evidence_root.lower() + os.sep):
    fail("Evidence path escapes target Saved/Migration")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()

for package_name in EXPECTED_PACKAGES:
    assets = registry.get_assets_by_package_name(
        package_name, include_only_on_disk_assets=True
    )
    if not assets:
        fail("Expected staged package is not registered: " + package_name)

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

direct_dependencies = {}
unexpected_game_dependencies = set()
expected_set = set(EXPECTED_PACKAGES)
for seed in SEEDS:
    dependencies = sorted(
        {
            str(item)
            for item in registry.get_dependencies(seed, dependency_options)
        }
    )
    direct_dependencies[seed] = dependencies
    for dependency in dependencies:
        if dependency.startswith("/Game/") and dependency not in expected_set:
            unexpected_game_dependencies.add(dependency)
if unexpected_game_dependencies:
    fail(
        "Seed closure contains unstaged /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

expected_files = {
    package_file(destination, package_name).lower(): package_name
    for package_name in EXPECTED_PACKAGES
}
for path in expected_files:
    if os.path.exists(path):
        fail("Destination package already exists: " + path)

before_packages = list_packages(destination)
options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", False)
options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.SKIP
)
options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_EFCORE_CONTENT57_MIGRATION_BEGIN: " + repr(SEEDS))
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(SEEDS), destination, options
)

after_packages = list_packages(destination)
created_packages = after_packages - before_packages
expected_paths = set(expected_files)
if created_packages != expected_paths:
    fail(
        "Unexpected migrated package set; created={!r}, expected={!r}".format(
            sorted(created_packages), sorted(expected_paths)
        )
    )

package_rows = []
for path in sorted(created_packages):
    package_rows.append(
        {
            "package": expected_files[path],
            "file": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(
        datetime.timezone.utc
    ).isoformat(),
    "status": "ASSETTOOLS_EXACT_BATCH_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "destination": destination,
    "seed_packages": list(SEEDS),
    "direct_dependencies": direct_dependencies,
    "packages": package_rows,
    "created_package_count": len(package_rows),
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "dependencies_enabled": True,
    "unexpected_game_dependencies": [],
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCORE_CONTENT57_MIGRATION_PASS: " + evidence_path)
