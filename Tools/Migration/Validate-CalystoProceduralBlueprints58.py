"""Compile every Calysto and project Procedural Blueprint in a fresh UE 5.8 process.

This is deliberately read-only: compilation happens in memory and no package is saved.
Required environment: CODEX_CALYSTO_PROCEDURAL_BP_EVIDENCE
"""

import datetime
import json
import os
import traceback

import unreal


OUTPUT_VALUE = os.environ.get("CODEX_CALYSTO_PROCEDURAL_BP_EVIDENCE", "").strip()
ROOTS = ("/Game/Calysto", "/Game/Procedural")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_PROCEDURAL_BP58_FAIL: " + message)
    raise RuntimeError(message)


def is_under(path, root):
    try:
        return os.path.commonpath(
            [os.path.realpath(path), os.path.realpath(root)]
        ).lower() == os.path.realpath(root).lower()
    except ValueError:
        return False


def asset_class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def compile_status(blueprint):
    value = str(blueprint.get_editor_property("status"))
    normalized = "".join(char for char in value.upper() if char.isalnum())
    return value, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
output_path = os.path.realpath(OUTPUT_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not OUTPUT_VALUE or not is_under(
    output_path, os.path.join(project_root, "Saved", "Migration")
):
    fail("Evidence path is absent or escapes target Saved/Migration")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(list(ROOTS), True)
registry.wait_for_completion()

candidates = {}
for root in ROOTS:
    for asset_data in registry.get_assets_by_path(
        root, recursive=True, include_only_on_disk_assets=True
    ):
        class_name = asset_class_name(asset_data)
        if class_name.endswith("Blueprint"):
            package = str(asset_data.package_name)
            candidates[package] = {
                "asset_data": asset_data,
                "asset_class": class_name,
            }

if "/Game/Procedural/DoorToLevel" not in candidates:
    fail("DoorToLevel is absent from the Blueprint compile cohort")
if not any(package.startswith("/Game/Calysto/") for package in candidates):
    fail("No Calysto Blueprints were discovered")

rows = []
failures = []
for package in sorted(candidates):
    asset_class = candidates[package]["asset_class"]
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
        if blueprint is None:
            raise RuntimeError("load_asset returned None")
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status, normalized = compile_status(blueprint)
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        parent_path = parent.get_path_name() if parent else ""
        row = {
            "package": package,
            "asset_class": asset_class,
            "compile_status": status,
            "parent": parent_path,
        }
        rows.append(row)
        if "UPTODATE" not in normalized or "ERROR" in normalized:
            failures.append(
                {"package": package, "reason": "COMPILE_STATUS", "status": status}
            )
        if not parent_path:
            failures.append({"package": package, "reason": "NULL_PARENT"})
    except Exception as exc:
        failures.append(
            {
                "package": package,
                "reason": "LOAD_OR_COMPILE_EXCEPTION",
                "error": repr(exc),
                "traceback": traceback.format_exc(),
            }
        )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_PROCEDURAL_BLUEPRINT_COMPILE_PASS"
        if not failures
        else "UE58_CALYSTO_PROCEDURAL_BLUEPRINT_COMPILE_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "roots": list(ROOTS),
    "blueprint_count": len(candidates),
    "compiled_count": len(rows),
    "packages_saved": 0,
    "rows": rows,
    "failures": failures,
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)

if failures:
    fail("{} Blueprint validation failures; see {}".format(len(failures), output_path))
unreal.log("CODEX_CALYSTO_PROCEDURAL_BP58_PASS: " + output_path)
