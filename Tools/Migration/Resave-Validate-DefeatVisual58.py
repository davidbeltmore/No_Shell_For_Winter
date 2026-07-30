"""Load, class-check, and resave the exact Defeat visual batch in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
CONTENT_ROOT = os.path.realpath(unreal.Paths.project_content_dir())
PACKAGES = (
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed",
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium",
    "/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel",
)
WIDGET_PACKAGE = "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget"
EVIDENCE_PATH = os.path.join(
    PROJECT_ROOT, "Saved", "Migration", "Phase4", "DefeatVisualResave58.json"
)


def fail(message):
    unreal.log_error("CODEX_DEFEAT_VISUAL58_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(package_name):
    return os.path.realpath(
        os.path.join(
            CONTENT_ROOT,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

rows = []
for package in PACKAGES:
    path = package_file(package)
    if not os.path.isfile(path):
        fail("Package file is absent: " + path)
    before_hash = sha256(path)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load: " + package)
    asset_class = asset.get_class().get_name()
    expected_class = "FontFace" if "/Fonts/" in package else "Texture2D"
    if asset_class != expected_class:
        fail("Unexpected class for {}: {}".format(package, asset_class))
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("UE 5.8 save failed: " + package)
    rows.append(
        {
            "package": package,
            "class": asset_class,
            "file": path,
            "sha256_before_resave": before_hash,
            "sha256_after_resave": sha256(path),
            "length": os.path.getsize(path),
        }
    )

widget = unreal.EditorAssetLibrary.load_asset(WIDGET_PACKAGE)
if widget is None or widget.get_class().get_name() != "WidgetBlueprint":
    fail("Defeat WidgetBlueprint did not load")
unreal.BlueprintEditorLibrary.compile_blueprint(widget)
widget_status = str(widget.get_editor_property("status"))
if "UPTODATE" not in "".join(c for c in widget_status.upper() if c.isalnum()):
    fail("Defeat WidgetBlueprint compile status is " + widget_status)
generated_class = unreal.load_class(
    None,
    WIDGET_PACKAGE + ".WBP_ProjectKnockoutStruggleWidget_C",
)
native_parent = unreal.load_class(
    None,
    "/Script/EFProjectSystemsGameplay.ProjectKnockoutStruggleWidget",
)
if not generated_class or not native_parent or not unreal.MathLibrary.class_is_child_of(generated_class, native_parent):
    fail("Defeat WidgetBlueprint native-parent contract failed")
if not unreal.EditorAssetLibrary.save_asset(WIDGET_PACKAGE, only_if_is_dirty=False):
    fail("Defeat WidgetBlueprint save failed")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/UI/Defeat"], True)
registry.wait_for_completion()
registered_packages = sorted(
    {
        str(item.package_name)
        for item in registry.get_assets_by_path(
            "/Game/UI/Defeat", recursive=True, include_only_on_disk_assets=True
        )
    }
)
expected_registered = sorted((*PACKAGES, WIDGET_PACKAGE))
if registered_packages != expected_registered:
    fail(
        "Unexpected /Game/UI/Defeat package set: " + repr(registered_packages)
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_DEFEAT_VISUAL_LOAD_COMPILE_RESAVE_PASS",
    "engine_version": engine_version,
    "package_count": len(rows),
    "packages": rows,
    "widget": {
        "package": WIDGET_PACKAGE,
        "status": widget_status,
        "generated_class": generated_class.get_path_name(),
        "native_parent": native_parent.get_path_name(),
    },
    "registered_package_count": len(registered_packages),
    "redirectors": [],
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DEFEAT_VISUAL58_PASS: " + EVIDENCE_PATH)
