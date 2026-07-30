"""Migrate the complete legacy ExportedAnimations cohort through UE 5.7 AssetTools.

This script is deliberately restricted to the detached BulkProjectContent57Harness.
It reads LustAsDeadlySin only to verify the staged package bytes, migrates with
dependencies disabled, and refuses any target delta outside ExportedAnimations.
"""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_HARNESS_COUNT = 660
EXPECTED_SOURCE_COUNT = 661
EXPECTED_ADDED_COUNT = 657
SOURCE_ONLY_IN_HARNESS = "Anim_KA_Idle53_Seiza_Loop1.uasset"
PREEXISTING_PACKAGES = {
    "/game/exportedanimations/together/0001scene",
    "/game/exportedanimations/sexanimations/as_doggyclassic_1_female",
    "/game/exportedanimations/m_sexanimations/as_doggyclassic_1_male_corrected",
}
EXPECTED_PREEXISTING_HASHES = {
    "/game/exportedanimations/together/0001scene": (
        "E484F0DC66A69E4200F865CE3EBF90EC2CE52446C84CFCCBB95FF747C6F937AB"
    ),
    "/game/exportedanimations/sexanimations/as_doggyclassic_1_female": (
        "13A895CC893FFC284F0F29DC715F8410623FD49D9724A2EDB3D9F86280A5C5C7"
    ),
    "/game/exportedanimations/m_sexanimations/as_doggyclassic_1_male_corrected": (
        "57B14FE30D263D9D22FBF097CFDA608AC7101F348BBB53175FFC4194D429DA24"
    ),
}
PACKAGE_EXTENSIONS = (".uasset", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_EXPORTED_ANIMATIONS57_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return (
            os.path.commonpath([os.path.realpath(path), os.path.realpath(root)]).lower()
            == os.path.realpath(root).lower()
        )
    except ValueError:
        return False


def file_inventory(root, extensions=None, include_hash=False):
    rows = {}
    for directory, _subdirectories, files in os.walk(root):
        for name in files:
            if extensions and not name.lower().endswith(extensions):
                continue
            path = os.path.realpath(os.path.join(directory, name))
            relative = os.path.relpath(path, root).replace(os.sep, "/")
            stat = os.stat(path)
            row = {
                "relative": relative,
                "length": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
            }
            if include_hash:
                row["sha256"] = sha256(path)
            rows[relative.lower()] = row
    return rows


def package_files(content_root, package):
    stem = package[len("/Game/") :].replace("/", os.sep)
    rows = []
    for extension in PACKAGE_EXTENSIONS:
        path = os.path.realpath(os.path.join(content_root, stem + extension))
        if os.path.isfile(path):
            rows.append(
                {
                    "relative": os.path.relpath(path, content_root).replace(os.sep, "/"),
                    "length": os.path.getsize(path),
                    "sha256": sha256(path),
                }
            )
    return rows


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)

target_root = os.path.realpath(
    os.environ.get("CODEX_ANIMATION_TARGET_ROOT", "").strip()
)
source_content = os.path.realpath(
    os.environ.get("CODEX_ANIMATION_SOURCE_CONTENT", "").strip()
)
expected_harness_content = os.path.realpath(
    os.environ.get("CODEX_ANIMATION_HARNESS_CONTENT", "").strip()
)
evidence_path = os.path.realpath(
    os.environ.get("CODEX_ANIMATION57_EVIDENCE", "").strip()
)
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
target_content = os.path.realpath(os.path.join(target_root, "Content"))
target_saved = os.path.realpath(os.path.join(target_root, "Saved", "Migration"))

if os.path.basename(project_file).lower() != "bulkprojectcontent57harness.uproject":
    fail("This script may run only in BulkProjectContent57Harness")
if harness_content.lower() != expected_harness_content.lower():
    fail("Harness Content does not match the audited path")
if not is_under(harness_content, target_saved):
    fail("Harness must remain below target Saved/Migration")
if is_under(harness_content, target_content):
    fail("Harness Content overlaps live target Content")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
source_root = os.path.realpath(os.path.join(source_content, ".."))
source_descriptors = [
    name for name in os.listdir(source_root) if name.lower().endswith(".uproject")
]
if (
    os.path.basename(source_root).lower() != "lustasdeadlysin"
    or source_descriptors != ["ACFSample.uproject"]
):
    fail("Read-only source Content root is not LustAsDeadlySin")
if not is_under(evidence_path, target_saved):
    fail("Evidence path escapes target Saved/Migration")

source_exported = os.path.join(source_content, "ExportedAnimations")
harness_exported = os.path.join(harness_content, "ExportedAnimations")
source_before = file_inventory(source_exported, (".uasset",), include_hash=True)
harness_before = file_inventory(harness_exported, (".uasset",), include_hash=True)
if len(source_before) != EXPECTED_SOURCE_COUNT:
    fail("Unexpected source asset count: {}".format(len(source_before)))
if len(harness_before) != EXPECTED_HARNESS_COUNT:
    fail("Unexpected harness asset count: {}".format(len(harness_before)))
missing_from_harness = sorted(set(source_before) - set(harness_before))
if missing_from_harness != [SOURCE_ONLY_IN_HARNESS.lower()]:
    fail("Unexpected source/harness difference: " + repr(missing_from_harness))
for relative, harness_row in harness_before.items():
    source_row = source_before.get(relative)
    if (
        not source_row
        or source_row["length"] != harness_row["length"]
        or source_row["sha256"] != harness_row["sha256"]
    ):
        fail("Detached harness bytes differ from source: " + relative)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/ExportedAnimations"], True)
registry.wait_for_completion()
asset_data = list(
    registry.get_assets_by_path(
        "/Game/ExportedAnimations", recursive=True, include_only_on_disk_assets=True
    )
)
packages = sorted({str(row.package_name) for row in asset_data})
if len(packages) != EXPECTED_HARNESS_COUNT:
    fail("Unexpected registered harness package count: {}".format(len(packages)))
if any(not package.lower().startswith("/game/exportedanimations/") for package in packages):
    fail("A package escaped /Game/ExportedAnimations")

preexisting = {
    package.lower()
    for package in packages
    if package_files(target_content, package)
}
all_packages_lower = {package.lower() for package in packages}
resume_validation = preexisting == all_packages_lower
if preexisting != PREEXISTING_PACKAGES and not resume_validation:
    fail("Unexpected target collision set: " + repr(sorted(preexisting)))

target_before = file_inventory(target_content)
collision_before = {
    package: package_files(target_content, package)
    for package in packages
    if package.lower() in PREEXISTING_PACKAGES
}
for package, rows in collision_before.items():
    target_uasset = next(
        (row for row in rows if row["relative"].lower().endswith(".uasset")), None
    )
    if (
        not target_uasset
        or target_uasset["sha256"] != EXPECTED_PREEXISTING_HASHES[package.lower()]
    ):
        fail("A guarded pre-existing collision differs from baseline: " + package)

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
options.set_editor_property("orphan_folder", "")

chunk_size = 40
if not resume_validation:
    for offset in range(0, len(packages), chunk_size):
        chunk = packages[offset : offset + chunk_size]
        unreal.log(
            "CODEX_EXPORTED_ANIMATIONS57_CHUNK: {}-{} of {}".format(
                offset + 1, offset + len(chunk), len(packages)
            )
        )
        unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
            chunk, target_content, options
        )
else:
    unreal.log("CODEX_EXPORTED_ANIMATIONS57_RESUME_VALIDATION: all packages present")

target_after = file_inventory(target_content)
added = sorted(set(target_after) - set(target_before))
removed = sorted(set(target_before) - set(target_after))
changed = sorted(
    relative
    for relative in set(target_before) & set(target_after)
    if target_before[relative] != target_after[relative]
)
expected_added = sorted(
    ("ExportedAnimations/" + row["relative"]).lower()
    for relative, row in harness_before.items()
    if (
        "/game/exportedanimations/"
        + os.path.splitext(row["relative"])[0].replace("\\", "/")
    ).lower()
    not in PREEXISTING_PACKAGES
)
if resume_validation:
    if added or removed or changed:
        fail("Resume validation unexpectedly changed target Content")
else:
    if len(added) != EXPECTED_ADDED_COUNT or added != expected_added:
        fail(
            "Target added-file delta differs: actual={} expected={}".format(
                len(added), len(expected_added)
            )
        )
    if removed or changed:
        fail(
            "Migration removed or changed existing target files: removed={} changed={}".format(
                removed[:10], changed[:10]
            )
        )

serialized_byte_differences = []
target_package_rows = []
for package in packages:
    target_rows = package_files(target_content, package)
    if not target_rows:
        fail("Target package is absent after migration: " + package)
    if package.lower() in PREEXISTING_PACKAGES:
        if target_rows != collision_before[package]:
            fail("AssetTools changed a pre-existing collision: " + package)
    relative_key = (
        package[len("/Game/ExportedAnimations/") :] + ".uasset"
    ).lower()
    source_row = harness_before.get(relative_key)
    target_uasset = next(
        (row for row in target_rows if row["relative"].lower().endswith(".uasset")),
        None,
    )
    if not source_row or not target_uasset:
        fail("Target or staged package row is missing: " + package)
    bytes_match_harness = (
        source_row["length"] == target_uasset["length"]
        and source_row["sha256"] == target_uasset["sha256"]
    )
    if not bytes_match_harness and package.lower() not in PREEXISTING_PACKAGES:
        serialized_byte_differences.append(package)
    target_package_rows.append(
        {
            "package": package,
            "target_length": target_uasset["length"],
            "target_sha256": target_uasset["sha256"],
            "staged_length": source_row["length"],
            "staged_sha256": source_row["sha256"],
            "bytes_match_staged": bytes_match_harness,
        }
    )

source_after = file_inventory(source_exported, (".uasset",), include_hash=True)
if source_after != source_before:
    fail("Read-only source animation cohort changed during migration")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_ASSETTOOLS_EXPORTED_ANIMATIONS_PASS",
    "engine_version": engine_version,
    "project_file": project_file,
    "source_project_opened": False,
    "source_read_only_verified": True,
    "source_package_count": len(source_before),
    "harness_package_count": len(packages),
    "resume_validation": resume_validation,
    "preexisting_collision_count_this_process": len(preexisting),
    "preexisting_packages_this_process": sorted(preexisting),
    "guarded_preexisting_packages": sorted(PREEXISTING_PACKAGES),
    "added_file_count": len(added),
    "expected_added_file_count": EXPECTED_ADDED_COUNT,
    "migration_created_package_count": EXPECTED_ADDED_COUNT,
    "removed_file_count": len(removed),
    "changed_existing_file_count": len(changed),
    "assettools_serialized_byte_difference_count": len(
        serialized_byte_differences
    ),
    "assettools_serialized_packages": serialized_byte_differences,
    "ignore_dependencies": True,
    "asset_conflict": "SKIP",
    "packages": packages,
    "package_files": target_package_rows,
    "added_files": added,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EXPORTED_ANIMATIONS57_PASS: " + evidence_path)
