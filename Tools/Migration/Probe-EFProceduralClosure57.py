"""Export the dependency graph of the staged EFProcedural closure without loading assets."""

import csv
import datetime
import json
import os
from collections import deque

import unreal


SEEDS = (
    "/Game/Procedural/Maps/DungeonGeneration",
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
)
OUTPUT_PATH = os.environ.get("CODEX_EFPROCEDURAL_CLOSURE_OUTPUT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get("CODEX_EXPECTED_HARNESS_CONTENT", "").strip()
TARGET_MANIFEST = os.environ.get("CODEX_TARGET_MANIFEST", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()


def fail(message):
    unreal.log_error("CODEX_EFPROCEDURAL57_CLOSURE_FAIL: " + message)
    raise RuntimeError(message)


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all((OUTPUT_PATH, EXPECTED_HARNESS_CONTENT, TARGET_MANIFEST, EXPECTED_TARGET_ROOT)):
    fail("Closure output, harness Content, target manifest, and target root are required")

project_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_content = os.path.realpath(EXPECTED_HARNESS_CONTENT)
if project_content.lower() != expected_content.lower():
    fail("Harness Content invariant failed")

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
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()


def dependency_options(hard_package=False, soft_package=False, hard_manage=False, soft_manage=False):
    options = unreal.AssetRegistryDependencyOptions()
    values = {
        "include_hard_package_references": hard_package,
        "include_soft_package_references": soft_package,
        "include_hard_management_references": hard_manage,
        "include_soft_management_references": soft_manage,
        "include_searchable_names": False,
    }
    for property_name, value in values.items():
        try:
            options.set_editor_property(property_name, value)
        except Exception:
            pass
    return options


all_dependency_options = dependency_options(True, True, True, True)
hard_dependency_options = dependency_options(hard_package=True)
soft_dependency_options = dependency_options(soft_package=True)
management_dependency_options = dependency_options(hard_manage=True, soft_manage=True)

staged_packages = sorted(
    {
        str(asset.package_name)
        for asset in registry.get_assets_by_path(
            "/Game", recursive=True, include_only_on_disk_assets=True
        )
        if str(asset.package_name).startswith("/Game/")
    }
)
staged_set = set(staged_packages)
for seed in SEEDS:
    if seed not in staged_set:
        fail("Staged seed is absent from the registry: " + seed)

graph = {}
hard_graph = {}
soft_graph = {}
management_graph = {}
editor_module_packages = []
for package in staged_packages:
    dependencies = sorted(
        {str(item) for item in registry.get_dependencies(package, all_dependency_options)}
    )
    graph[package] = dependencies
    hard_graph[package] = sorted(
        {str(item) for item in registry.get_dependencies(package, hard_dependency_options)}
    )
    soft_graph[package] = sorted(
        {str(item) for item in registry.get_dependencies(package, soft_dependency_options)}
    )
    management_graph[package] = sorted(
        {str(item) for item in registry.get_dependencies(package, management_dependency_options)}
    )
    if "/Script/PCGEditor" in dependencies:
        editor_module_packages.append(package)

queue = deque(SEEDS)
visited = set()
external_source_only = set()
target_existing = set()
unclassified_game_dependencies = set()
while queue:
    package = queue.popleft()
    if package in visited:
        continue
    visited.add(package)
    for dependency in graph.get(package, []):
        if not dependency.startswith("/Game/"):
            continue
        if dependency in staged_set:
            queue.append(dependency)
            continue
        row = manifest.get(dependency)
        if not row:
            unclassified_game_dependencies.add(dependency)
        elif row["presence"] == "SOURCE_ONLY":
            external_source_only.add(dependency)
        else:
            target_existing.add(dependency)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "STAGED_CLOSURE_REGISTRY_PROBE_PASS",
    "engine_version": engine_version,
    "staged_package_count": len(staged_packages),
    "reachable_staged_package_count": len(visited.intersection(staged_set)),
    "unreachable_staged_packages": sorted(staged_set.difference(visited)),
    "external_source_only_count": len(external_source_only),
    "external_source_only": [
        {"package": package, "manifest": manifest.get(package, {})}
        for package in sorted(external_source_only)
    ],
    "target_existing_dependency_count": len(target_existing),
    "target_existing_dependencies": sorted(target_existing),
    "unclassified_game_dependency_count": len(unclassified_game_dependencies),
    "unclassified_game_dependencies": sorted(unclassified_game_dependencies),
    "editor_module_dependency_count": len(editor_module_packages),
    "editor_module_packages": sorted(editor_module_packages),
    "graph": graph,
    "hard_package_graph": hard_graph,
    "soft_package_graph": soft_graph,
    "management_graph": management_graph,
    "packages_loaded": 0,
    "packages_saved": 0,
}
output_path = os.path.realpath(OUTPUT_PATH)
expected_output_root = os.path.realpath(
    os.path.join(EXPECTED_TARGET_ROOT, "Saved", "Migration")
)
if not output_path.lower().startswith(expected_output_root.lower() + os.sep):
    fail("Closure output escapes target Saved/Migration")
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFPROCEDURAL57_CLOSURE_PASS: " + output_path)
