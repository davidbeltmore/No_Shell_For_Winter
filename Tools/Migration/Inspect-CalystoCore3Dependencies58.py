"""Read-only UE 5.8 dependency inspection for the three failing Calysto assets.

Required environment:
  CODEX_CALYSTO_CORE3_INSPECTION_EVIDENCE

The script may load and compile Blueprints in memory to expose their current
status, but it never calls an Unreal save API.  It also inventories Content
before and after the inspection and fails if any package file changed on disk.
"""

import datetime
import hashlib
import json
import os

import unreal


EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_INSPECTION_EVIDENCE", ""
).strip()
SOURCE_ROOT = os.path.realpath(r"D:\Projects UE5\LustAsDeadlySin")
TARGETS = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
SMART_SCATTER = "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter"
GRAMMAR_SETTING = "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting"
MISSING_BIOME = "/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0"
CURRENT_SCATTER_CLASS = (
    "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter"
)
CURRENT_SCATTER_DEFAULT = "/Game/Calysto/Shared/Data/DA_ScatterDummy"
STALE_SCATTER_CLASS = "/Game/Calysto/Scatter/Data/Structure/PDA_CalystoScatter"
STALE_SCATTER_DEFAULT = "/Game/Calysto/Scatter/Data/DA_ScatterDummy"

INSPECT_PACKAGES = (
    *TARGETS,
    SMART_SCATTER,
    GRAMMAR_SETTING,
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    CURRENT_SCATTER_CLASS,
    CURRENT_SCATTER_DEFAULT,
    STALE_SCATTER_CLASS,
    STALE_SCATTER_DEFAULT,
    MISSING_BIOME,
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoGameMode",
)

PRELOAD_ORDER = (
    "/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode",
    CURRENT_SCATTER_CLASS,
    CURRENT_SCATTER_DEFAULT,
    SMART_SCATTER,
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    GRAMMAR_SETTING,
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
)

CLASSIFICATION = {
    TARGETS[0]: {
        "role": "runtime_actor_procedural_dungeon_generator",
        "editor_only": False,
        "repair": "remigrate_after_prewarm_then_refresh_rebuild_nodes",
    },
    TARGETS[1]: {
        "role": "runtime_player_controller_legacy_sample_support",
        "editor_only": False,
        "repair": "recover_missing_vendor_asset_or_remove_rebuild_legacy_block",
    },
    TARGETS[2]: {
        "role": "runtime_primary_data_asset_contract",
        "editor_only": False,
        "repair": "repair_ST_SmartScatter_first_then_refresh_this_blueprint",
    },
    SMART_SCATTER: {
        "role": "runtime_user_defined_struct_contract",
        "editor_only": False,
        "repair": "rebuild_object_property_and_replace_stale_default_path",
    },
}

PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_CORE3_INSPECT58_FAIL: " + message)
    raise RuntimeError(message)


def is_under(path, root):
    try:
        return os.path.commonpath(
            [os.path.realpath(path), os.path.realpath(root)]
        ).lower() == os.path.realpath(root).lower()
    except ValueError:
        return False


def package_file(content_root, package):
    return os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def disk_record(content_root, package):
    path = package_file(content_root, package)
    if not os.path.isfile(path):
        return {"present": False, "file": path}
    stat = os.stat(path)
    return {
        "present": True,
        "file": path,
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def contains_ascii(content_root, package, text):
    path = package_file(content_root, package)
    if not os.path.isfile(path):
        return False
    with open(path, "rb") as handle:
        return text.encode("utf-8") in handle.read()


def inventory(content_root):
    result = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/")
            stat = os.stat(path)
            result[relative.lower()] = {
                "length": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
            }
    return result


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for property_name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(property_name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


def safe_editor_property(asset, property_name):
    try:
        return {
            "available": True,
            "value": str(asset.get_editor_property(property_name)),
        }
    except Exception as exc:
        return {"available": False, "error": repr(exc)}


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def write_evidence(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not EVIDENCE_VALUE:
    fail("CODEX_CALYSTO_CORE3_INSPECTION_EVIDENCE is required")
if len(TARGETS) != 3 or len(set(TARGETS)) != 3:
    fail("Inspection target cohort is not exactly three unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in INSPECT_PACKAGES):
    fail("ExportedAnimations entered the Calysto inspection cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
evidence_path = os.path.realpath(EVIDENCE_VALUE)
source_content = os.path.realpath(os.path.join(SOURCE_ROOT, "Content"))
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Inspector is not running against NoShellForWinter.uproject")
if not is_under(evidence_path, saved_root):
    fail("Inspection evidence must remain under target Saved/Migration")
if not os.path.isdir(source_content):
    fail("Read-only source Content tree is absent")
if is_under(project_root, SOURCE_ROOT) or is_under(SOURCE_ROOT, project_root):
    fail("Source and target roots overlap")

content_before = inventory(content_root)
target_disk = {
    package: disk_record(content_root, package) for package in INSPECT_PACKAGES
}
source_disk = {
    package: disk_record(source_content, package) for package in INSPECT_PACKAGES
}

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
options = dependency_options()
registry_rows = {}
for package in INSPECT_PACKAGES:
    registry_rows[package] = {
        "dependencies": sorted(
            {str(value) for value in registry.get_dependencies(package, options)}
        ),
        "referencers": sorted(
            {str(value) for value in registry.get_referencers(package, options)}
        ),
    }

preload_rows = []
loaded_assets = {}
for package in PRELOAD_ORDER:
    try:
        asset = unreal.EditorAssetLibrary.load_asset(package)
        error = ""
    except Exception as exc:
        asset = None
        error = repr(exc)
    loaded_assets[package] = asset
    preload_rows.append(
        {
            "package": package,
            "loaded": asset is not None,
            "class": asset.get_class().get_name() if asset is not None else "",
            "error": error,
        }
    )

struct_rows = {}
for package in (SMART_SCATTER, GRAMMAR_SETTING):
    asset = loaded_assets.get(package)
    struct_rows[package] = {
        "loaded": asset is not None,
        "class": asset.get_class().get_name() if asset is not None else "",
        "status": safe_editor_property(asset, "status") if asset is not None else {},
        "error_message": (
            safe_editor_property(asset, "error_message") if asset is not None else {}
        ),
    }

blueprint_rows = []
for package in TARGETS:
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
        if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
            raise RuntimeError("Asset is not a Blueprint")
        parent_before = object_path(
            unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        )
        status_before = str(blueprint.get_editor_property("status"))
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status_after = str(blueprint.get_editor_property("status"))
        blueprint_rows.append(
            {
                "package": package,
                "loaded": True,
                "parent": parent_before,
                "status_before_compile": status_before,
                "status_after_compile": status_after,
                "compile_exception": "",
            }
        )
    except Exception as exc:
        blueprint_rows.append(
            {
                "package": package,
                "loaded": False,
                "compile_exception": repr(exc),
            }
        )

content_after = inventory(content_root)
content_delta = []
for relative in sorted(set(content_before) | set(content_after)):
    before = content_before.get(relative)
    after = content_after.get(relative)
    if before != after:
        content_delta.append({"relative": relative, "before": before, "after": after})

smart_error = str(
    struct_rows.get(SMART_SCATTER, {})
    .get("error_message", {})
    .get("value", "")
)
binary_signals = {
    "smart_scatter_error_property": contains_ascii(
        content_root,
        SMART_SCATTER,
        "Data_23_DDA7FF704A715D8FC2E50BAE84265C16",
    ),
    "smart_scatter_serialized_error": contains_ascii(
        content_root,
        SMART_SCATTER,
        "Invalid object property. Structure 'UserDefinedStruct "
        "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter.ST_SmartScatter'",
    ),
    "smart_scatter_stale_default": contains_ascii(
        content_root,
        SMART_SCATTER,
        "/Game/Calysto/Scatter/Data/DA_ScatterDummy",
    ),
    "massive_dungeon_make_grammar_setting": contains_ascii(
        content_root,
        TARGETS[0],
        "K2Node_MakeStruct_ST_GrammarSetting",
    ),
    "controller_references_missing_biome": contains_ascii(
        content_root,
        TARGETS[1],
        "/Game/Calysto/World/Blueprint/Legacy/BP_Biome3",
    ),
}
findings = [
    {
        "id": "SMART_SCATTER_ROOT_CAUSE",
        "confirmed": (
            "Data_23_DDA7FF704A715D8FC2E50BAE84265C16" in smart_error
            or (
                binary_signals["smart_scatter_error_property"]
                and binary_signals["smart_scatter_serialized_error"]
            )
        ),
        "root_asset": SMART_SCATTER,
        "affected_asset": TARGETS[2],
        "required_action": (
            "Repair/remigrate ST_SmartScatter after loading PDA_CalystoScatter and "
            "Enum_ScatterMode; reset Data default from stale /Game/Calysto/Scatter "
            "path to the current /Game/Calysto/Shared asset (or None), save the "
            "struct, then refresh/compile/save PDA_VegetationCalysto."
        ),
    },
    {
        "id": "MASSIVE_DUNGEON_UNKNOWN_STRUCT",
        "confirmed": (
            target_disk[GRAMMAR_SETTING]["present"]
            and binary_signals["massive_dungeon_make_grammar_setting"]
        ),
        "root_asset": GRAMMAR_SETTING,
        "affected_asset": TARGETS[0],
        "required_action": (
            "Preload ST_GrammarSetting plus room/dungeon support, remigrate the exact "
            "source Blueprint, then reconstruct/refresh its stale split and MakeStruct nodes."
        ),
    },
    {
        "id": "CONTROLLER_DANGLING_LEGACY_CLASS",
        "confirmed": (
            not source_disk[MISSING_BIOME]["present"]
            and not target_disk[MISSING_BIOME]["present"]
            and binary_signals["controller_references_missing_biome"]
        ),
        "root_asset": MISSING_BIOME,
        "affected_asset": TARGETS[1],
        "required_action": (
            "Recover BP_Biome3_0 from the matching Calysto vendor/version-control "
            "source, or remove/rebuild the legacy Generate/RandomizeSeed block. "
            "Remigration from the current source cannot restore this class."
        ),
    },
]

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_CORE3_DEPENDENCY_INSPECTION_COMPLETE"
        if not content_delta
        else "UE58_CALYSTO_CORE3_READ_ONLY_INVARIANT_FAILED"
    ),
    "engine_version": engine_version,
    "project_file": project_file,
    "source_root_read_only": SOURCE_ROOT,
    "target_root": project_root,
    "targets": list(TARGETS),
    "exported_animations_excluded": True,
    "save_api_calls": 0,
    "classifications": CLASSIFICATION,
    "target_disk": target_disk,
    "source_disk": source_disk,
    "registry": registry_rows,
    "preload": preload_rows,
    "structs": struct_rows,
    "blueprints_in_memory": blueprint_rows,
    "binary_signals": binary_signals,
    "findings": findings,
    "content_package_delta": content_delta,
    "content_unchanged": not content_delta,
}
write_evidence(evidence_path, payload)

if content_delta:
    fail("Content changed during read-only inspection; see evidence")

unreal.log(
    "CODEX_CALYSTO_CORE3_INSPECT58_COMPLETE: targets=3 findings={} evidence={}".format(
        len(findings), evidence_path
    )
)
