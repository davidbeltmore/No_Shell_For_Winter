"""Read-only UE 5.8 load/Blueprint-compile validation for a bulk migration.

Required process environment:
  CODEX_BULK_EXPECTED_COUNT
  CODEX_BULK_MIGRATION_EVIDENCE (or CODEX_BULK_EVIDENCE)
  CODEX_BULK_VALIDATION_EVIDENCE

The script never calls an asset-save API. Blueprint compilation is in memory;
every selected package file is hashed before and after to prove disk immutability.
"""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_COUNT_VALUE = os.environ.get("CODEX_BULK_EXPECTED_COUNT", "").strip()
MIGRATION_EVIDENCE_VALUE = (
    os.environ.get("CODEX_BULK_MIGRATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_BULK_EVIDENCE", "").strip()
)
VALIDATION_EVIDENCE_VALUE = os.environ.get(
    "CODEX_BULK_VALIDATION_EVIDENCE", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_BULK_PROJECT_CONTENT58_FAIL: " + message)
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


def package_file(content_root, package_name):
    if not package_name.startswith("/Game/"):
        fail("Migration evidence contains a non-/Game package: " + package_name)
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def file_snapshot(path):
    if not os.path.isfile(path):
        return None
    stat = os.stat(path)
    return {
        "file": path,
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def normalized_blueprint_status(asset):
    status = str(asset.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not all(
    (
        EXPECTED_COUNT_VALUE,
        MIGRATION_EVIDENCE_VALUE,
        VALIDATION_EVIDENCE_VALUE,
    )
):
    fail("All documented bulk validation environment variables are required")
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_BULK_EXPECTED_COUNT is not an integer")
if expected_count <= 0:
    fail("CODEX_BULK_EXPECTED_COUNT must be positive")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    fail("Target Content invariant failed")

migration_evidence_path = os.path.realpath(MIGRATION_EVIDENCE_VALUE)
validation_evidence_path = os.path.realpath(VALIDATION_EVIDENCE_VALUE)
saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
if not is_under(migration_evidence_path, saved_migration_root):
    fail("Migration evidence escapes target Saved/Migration")
if not is_under(validation_evidence_path, saved_migration_root):
    fail("Validation evidence escapes target Saved/Migration")
if migration_evidence_path.lower() == validation_evidence_path.lower():
    fail("Validation evidence must not overwrite migration evidence")
if not os.path.isfile(migration_evidence_path):
    fail("UE 5.7 bulk migration evidence is absent")

with open(migration_evidence_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_BULK_PROJECT_CONTENT57_PASS":
    fail("UE 5.7 bulk migration evidence status is not PASS")
if migration.get("target_delta_exact") is not True:
    fail("UE 5.7 bulk migration target delta is not exact")
if migration.get("staging_unchanged") is not True:
    fail("UE 5.7 bulk migration staging invariant is not PASS")
if os.path.realpath(migration.get("target_root", "")).lower() != project_root.lower():
    fail("UE 5.7 migration evidence belongs to a different target project")

evidence_rows = list(migration.get("packages", []))
packages = [str(row.get("package", "")).strip() for row in evidence_rows]
if len(packages) != expected_count or len(set(packages)) != expected_count:
    fail(
        "Migration evidence package count differs: {} != {}".format(
            len(packages), expected_count
        )
    )
if any("/_quarantine/" in package.lower() for package in packages):
    fail("Migration evidence contains a forbidden /_Quarantine/ package")
if packages != sorted(packages):
    fail("Migration evidence packages are not deterministically sorted")
rows_by_package = {row["package"]: row for row in evidence_rows}

disk_before = {}
preflight_failures = []
for package in packages:
    row = rows_by_package[package]
    path = package_file(content_root, package)
    recorded_path = os.path.realpath(str(row.get("file", "")))
    snapshot = file_snapshot(path)
    disk_before[package] = snapshot
    if recorded_path.lower() != path.lower():
        preflight_failures.append(
            {"package": package, "reason": "MIGRATION_EVIDENCE_PATH_MISMATCH"}
        )
    elif snapshot is None:
        preflight_failures.append(
            {"package": package, "reason": "TARGET_PACKAGE_FILE_ABSENT"}
        )
    elif (
        snapshot["length"] != row.get("length")
        or snapshot["sha256"] != row.get("sha256")
    ):
        preflight_failures.append(
            {"package": package, "reason": "TARGET_BYTES_CHANGED_SINCE_UE57"}
        )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()

loaded_results = []
failed_results = list(preflight_failures)
preflight_failed_packages = {row["package"] for row in preflight_failures}
blueprint_compile_results = []
for package in packages:
    if package in preflight_failed_packages:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": [],
                "reason": "PREFLIGHT_FAILED",
            }
        )
        continue
    registry_rows = list(
        registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
    )
    registry_classes = sorted({class_name(row) for row in registry_rows})
    if not registry_rows:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": [],
                "reason": "ASSET_REGISTRY_ROW_ABSENT",
            }
        )
        failed_results.append(
            {"package": package, "reason": "ASSET_REGISTRY_ROW_ABSENT"}
        )
        continue
    try:
        asset = unreal.EditorAssetLibrary.load_asset(package)
    except Exception as exc:
        asset = None
        load_error = repr(exc)
    else:
        load_error = ""
    if asset is None:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": registry_classes,
                "reason": "LOAD_RETURNED_NONE",
                "error": load_error,
            }
        )
        failed_results.append(
            {"package": package, "reason": "LOAD_RETURNED_NONE", "error": load_error}
        )
        continue

    actual_class = asset.get_class().get_name()
    loaded_results.append(
        {
            "package": package,
            "loaded": True,
            "class": actual_class,
            "registry_classes": registry_classes,
            "reason": "",
        }
    )
    if actual_class == "Blueprint" or actual_class.endswith("Blueprint"):
        compile_row = {
            "package": package,
            "class": actual_class,
            "compile_requested": True,
            "status": "",
            "result": "FAIL",
            "error": "",
        }
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            status, normalized = normalized_blueprint_status(asset)
            compile_row["status"] = status
            if "UPTODATE" in normalized:
                compile_row["result"] = "PASS_UP_TO_DATE"
            else:
                compile_row["error"] = "Blueprint did not reach UP_TO_DATE"
                failed_results.append(
                    {
                        "package": package,
                        "reason": "BLUEPRINT_NOT_UP_TO_DATE",
                        "status": status,
                    }
                )
        except Exception as exc:
            compile_row["error"] = repr(exc)
            failed_results.append(
                {
                    "package": package,
                    "reason": "BLUEPRINT_COMPILE_EXCEPTION",
                    "error": repr(exc),
                }
            )
        blueprint_compile_results.append(compile_row)

disk_after = {
    package: file_snapshot(package_file(content_root, package))
    for package in packages
}
changed_package_files = [
    package for package in packages if disk_after[package] != disk_before[package]
]
if changed_package_files:
    for package in changed_package_files:
        failed_results.append(
            {"package": package, "reason": "PACKAGE_FILE_CHANGED_DURING_VALIDATION"}
        )

all_loaded = (
    len(loaded_results) == expected_count
    and all(row["loaded"] for row in loaded_results)
)
blueprints_up_to_date = all(
    row["result"] == "PASS_UP_TO_DATE" for row in blueprint_compile_results
)
passed = (
    all_loaded
    and blueprints_up_to_date
    and not failed_results
    and not changed_package_files
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_BULK_PROJECT_CONTENT_READ_ONLY_LOAD_COMPILE_PASS"
        if passed
        else "UE58_BULK_PROJECT_CONTENT_READ_ONLY_LOAD_COMPILE_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "migration_evidence": migration_evidence_path,
    "migration_evidence_sha256": sha256(migration_evidence_path),
    "expected_count": expected_count,
    "package_count": len(packages),
    "quarantine_excluded": True,
    "loaded_count": sum(1 for row in loaded_results if row["loaded"]),
    "failed_count": len(failed_results),
    "blueprint_count": len(blueprint_compile_results),
    "blueprint_up_to_date_count": sum(
        1
        for row in blueprint_compile_results
        if row["result"] == "PASS_UP_TO_DATE"
    ),
    "all_packages_loaded": all_loaded,
    "all_blueprints_up_to_date": blueprints_up_to_date,
    "loaded": loaded_results,
    "failed": failed_results,
    "blueprint_compile_results": blueprint_compile_results,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_files": changed_package_files,
    "package_files_unchanged": not changed_package_files,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
}
os.makedirs(os.path.dirname(validation_evidence_path), exist_ok=True)
temporary_path = validation_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_evidence_path)

if not passed:
    fail(
        "Read-only validation failed: loaded={}/{}, blueprints_up_to_date={}/{}, "
        "failures={}, changed_files={}".format(
            sum(1 for row in loaded_results if row["loaded"]),
            expected_count,
            sum(
                1
                for row in blueprint_compile_results
                if row["result"] == "PASS_UP_TO_DATE"
            ),
            len(blueprint_compile_results),
            len(failed_results),
            len(changed_package_files),
        )
    )
unreal.log("CODEX_BULK_PROJECT_CONTENT58_PASS: " + validation_evidence_path)
