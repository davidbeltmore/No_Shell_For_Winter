"""Export UE 5.7 source asset metadata without loading or saving source packages.

Run only from the isolated RegistryProbe57 project whose Content directory is a
junction to the read-only migration source. Output is written under the target's
Saved/Migration tree. No source asset is loaded through EditorAssetLibrary.
"""

import datetime
import json
import os
import sys

import unreal


OUTPUT_PATH = os.environ.get("CODEX_REGISTRY_OUTPUT", "").strip()
EXPECTED_SOURCE = os.environ.get("CODEX_EXPECTED_SOURCE_CONTENT", "").strip()


def fail(message):
    unreal.log_error("CODEX_REGISTRY_EXPORT_FAIL: " + message)
    raise RuntimeError(message)


if not OUTPUT_PATH:
    fail("CODEX_REGISTRY_OUTPUT is required")

project_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_content = os.path.realpath(EXPECTED_SOURCE) if EXPECTED_SOURCE else ""
if not expected_content or project_content.lower() != expected_content.lower():
    fail(
        "Content junction invariant failed: project_content={!r}, expected={!r}".format(
            project_content, expected_content
        )
    )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
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
        dependency_options.set_editor_property(property_name, property_name != "include_searchable_names")
    except Exception:
        pass

tag_names = (
    "ParentClass",
    "NativeParentClass",
    "GeneratedClass",
    "BlueprintType",
    "Skeleton",
    "PhysicsAsset",
    "TargetSkeleton",
    "NumMorphTargets",
    "NumLODs",
    "PrimaryAssetType",
)


def text(value):
    return "" if value is None else str(value)


def tag_value(asset, tag_name):
    try:
        value = asset.get_tag_value(tag_name)
        return text(value)
    except Exception:
        return ""


assets = []
all_assets = sorted(
    registry.get_assets_by_path("/Game", recursive=True, include_only_on_disk_assets=True),
    key=lambda item: (text(item.package_name).lower(), text(item.asset_name).lower()),
)

for index, asset in enumerate(all_assets):
    package_name = text(asset.package_name)
    if not package_name.startswith("/Game/"):
        continue

    dependencies = sorted(
        {text(item) for item in registry.get_dependencies(package_name, dependency_options)}
    )
    referencers = sorted(
        {text(item) for item in registry.get_referencers(package_name, dependency_options)}
    )
    tags = {name: tag_value(asset, name) for name in tag_names}
    tags = {key: value for key, value in tags.items() if value}

    class_path = ""
    try:
        class_path = text(asset.asset_class_path)
    except Exception:
        try:
            class_path = text(asset.asset_class)
        except Exception:
            pass

    assets.append(
        {
            "package_name": package_name,
            "package_path": text(asset.package_path),
            "asset_name": text(asset.asset_name),
            "object_path": package_name + "." + text(asset.asset_name),
            "class_path": class_path,
            "tags": tags,
            "dependencies": dependencies,
            "referencers": referencers,
        }
    )

    if index and index % 1000 == 0:
        unreal.log("CODEX_REGISTRY_EXPORT_PROGRESS: {}/{}".format(index, len(all_assets)))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "source_content_real_path": project_content,
    "package_count": len({item["package_name"] for item in assets}),
    "asset_count": len(assets),
    "assets": assets,
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log(
    "CODEX_REGISTRY_EXPORT_PASS: assets={} packages={} output={}".format(
        payload["asset_count"], payload["package_count"], OUTPUT_PATH
    )
)
