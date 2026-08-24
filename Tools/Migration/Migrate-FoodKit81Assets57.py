"""Migrate the exact 81 food meshes and their material/texture closure in UE 5.7."""

from __future__ import annotations

import hashlib
import json
import os
from collections import Counter, deque
from datetime import datetime, timezone

import unreal


MANIFEST_PATH = os.environ.get("CODEX_FOODKIT81_MANIFEST", "").strip()
RECEIPT_PATH = os.environ.get("CODEX_FOODKIT81_RECEIPT", "").strip()
TARGET_ROOT = os.environ.get("CODEX_FOODKIT81_TARGET", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_FOODKIT81_MIGRATION_EVIDENCE", "").strip()

ALLOWED_CLASSES = {
    "StaticMesh",
    "Material",
    "MaterialInstanceConstant",
    "MaterialFunction",
    "MaterialFunctionInstance",
    "Texture2D",
}
FORBIDDEN_CLASSES = {"Blueprint", "World", "Level", "MapBuildDataRegistry"}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_FOODKIT81_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def real(path):
    return os.path.realpath(path)


def is_under(path, root):
    normalized_path = real(path).lower()
    normalized_root = real(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


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


def snapshot_package_files(content_root):
    rows = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = real(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            rows[relative] = {"length": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    return rows


def package_file(content_root, package):
    relative = package[len("/Game/") :].replace("/", os.sep) + ".uasset"
    return real(os.path.join(content_root, relative))


def package_relative(package):
    return (package[len("/Game/") :] + ".uasset").lower()


for required in (MANIFEST_PATH, RECEIPT_PATH, TARGET_ROOT, EVIDENCE_PATH):
    if not required:
        raise RuntimeError("All CODEX_FOODKIT81_* environment variables are required")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7, found " + engine_version)

target_root = real(TARGET_ROOT)
target_content = real(os.path.join(target_root, "Content"))
harness_content = real(unreal.Paths.project_content_dir())
phase_root = real(os.path.join(target_root, "Saved", "Migration", "FoodKitAlcohol"))
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target project descriptor is absent")
if not is_under(harness_content, phase_root):
    fail("Harness is not isolated under target Saved/Migration/FoodKitAlcohol")
if is_under(harness_content, target_content):
    fail("Harness Content overlaps live target Content")

with open(real(MANIFEST_PATH), "r", encoding="utf-8-sig") as handle:
    manifest = json.load(handle)
with open(real(RECEIPT_PATH), "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if manifest.get("status") != "FOOD_KIT_81_MANIFEST_PASS" or manifest.get("entry_count") != 81:
    fail("81-entry manifest gate did not pass")
if (
    receipt.get("status") != "ISOLATED_FOODKIT81_HARNESS_PASS"
    or receipt.get("manifest_fingerprint") != manifest.get("fingerprint")
    or receipt.get("manifest_entry_count") != 81
    or real(receipt.get("harness_content", "")).lower() != harness_content.lower()
):
    fail("Detached harness receipt does not match the manifest")

seeds = sorted({entry["mesh_package"] for entry in manifest["entries"]})
if len(seeds) != 81:
    fail("Seed mesh package set is not exactly 81")
target_pack = os.path.join(target_content, "Food_Props_Kit")
if os.path.exists(target_pack):
    fail("Target Food_Props_Kit already exists; refusing overwrite")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Food_Props_Kit"], True)
registry.wait_for_completion()
options = dependency_options()

closure = set(seeds)
queue = deque(seeds)
external_dependencies = set()
direct_dependencies = {}
while queue:
    package = queue.popleft()
    dependencies = sorted({str(value) for value in registry.get_dependencies(package, options)})
    direct_dependencies[package] = dependencies
    for dependency in dependencies:
        if dependency.startswith("/Game/Food_Props_Kit/"):
            if dependency not in closure:
                closure.add(dependency)
                queue.append(dependency)
        elif dependency.startswith("/Game/"):
            fail("Unexpected project dependency outside Food_Props_Kit: {} -> {}".format(package, dependency))
        else:
            external_dependencies.add(dependency)

package_rows = []
class_counts = Counter()
for package in sorted(closure):
    assets = list(registry.get_assets_by_package_name(package, include_only_on_disk_assets=True))
    if not assets:
        fail("Closure package is not registered: " + package)
    classes = sorted({class_name(asset) for asset in assets})
    if FORBIDDEN_CLASSES.intersection(classes):
        fail("Forbidden Blueprint/World/Level dependency in closure: {} {}".format(package, classes))
    unexpected = sorted(set(classes) - ALLOWED_CLASSES)
    if unexpected:
        fail("Unexpected asset class in closure: {} {}".format(package, unexpected))
    for value in classes:
        class_counts[value] += 1
    source_file = package_file(harness_content, package)
    if not os.path.isfile(source_file):
        fail("Closure package file is absent from harness: " + package)
    package_rows.append(
        {
            "package": package,
            "classes": classes,
            "source_file": source_file,
            "source_length": os.path.getsize(source_file),
            "source_sha256": sha256(source_file),
        }
    )

if class_counts.get("StaticMesh", 0) != 81:
    fail("Closure does not contain exactly 81 StaticMesh packages: " + repr(dict(class_counts)))

before = snapshot_package_files(target_content)
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", False)
migration_options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
migration_options.set_editor_property("orphan_folder", "")

unreal.log(
    "CODEX_FOODKIT81_ASSETTOOLS_BEGIN: seeds={} closure={}".format(len(seeds), len(closure))
)
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    seeds, target_content, migration_options
)

after = snapshot_package_files(target_content)
created = sorted(set(after) - set(before))
removed = sorted(set(before) - set(after))
modified = sorted(
    name for name in set(before).intersection(after) if before[name] != after[name]
)
expected_created = sorted(package_relative(package) for package in closure)
if created != expected_created or removed or modified:
    fail(
        "Target delta differs from dependency closure; created={} expected={} removed={} modified={}".format(
            len(created), len(expected_created), removed[:10], modified[:10]
        )
    )

target_rows = []
for source_row in package_rows:
    package = source_row["package"]
    target_file = package_file(target_content, package)
    if not os.path.isfile(target_file):
        fail("Migrated target package is absent: " + package)
    target_rows.append(
        {
            **source_row,
            "target_file": target_file,
            "target_length": os.path.getsize(target_file),
            "target_sha256": sha256(target_file),
        }
    )

evidence_path = real(EVIDENCE_PATH)
if not is_under(evidence_path, phase_root):
    fail("Evidence path escapes the migration phase root")
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.now(timezone.utc).isoformat(),
    "status": "FOODKIT81_ASSETTOOLS_MIGRATION_PASS",
    "engine_version": engine_version,
    "manifest_fingerprint": manifest["fingerprint"],
    "seed_mesh_count": len(seeds),
    "dependency_closure_count": len(closure),
    "class_counts": dict(sorted(class_counts.items())),
    "seeds": seeds,
    "packages": target_rows,
    "direct_dependencies": direct_dependencies,
    "external_dependencies": sorted(external_dependencies),
    "forbidden_dependency_classes": sorted(FORBIDDEN_CLASSES),
    "forbidden_dependencies_found": [],
    "unexpected_game_dependencies": [],
    "ignore_dependencies": False,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified,
    "target_delta_exact": True,
    "source_tree_mounted": False,
    "raw_target_asset_copy_requested": False,
}
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
unreal.log("CODEX_FOODKIT81_ASSETTOOLS_MIGRATION_PASS: " + evidence_path)
