"""Load, compile, resave, reload, and validate the exact DirtyPawn batch in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGE_COUNT = 15
EXPECTED_SOURCE_BYTES = 70130437
EXPECTED_SOURCE_FINGERPRINT = (
    "8F58CFD389DA0D63D2633D372C5DE251496EC53F53EEC15FE51AB160C46AEC36"
)
ASSETS = (
    ("/Game/_Game/Textures/leakage_sfhkcazc_4k/T_Leakage_sfhkcazc_2K_Mask", "Texture2D", 3103406, "524C8FA141E6822957DB8FB078162D23A1EBE51174B7352A0437C8AE1211FF6A"),
    ("/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D", "Texture2D", 5671240, "47CF420ED20BFE200907775D89FB1A08F6D8FAF19A90C4DD9C46F2FB722DC23A"),
    ("/Game/DirtyPawnSystem/Demo/StarterContent/Textures/T_Perlin_Noise_M", "Texture2D", 6804350, "9709239B17825D08BC603D4F9321D209AD980D4A1FB91242C5971BAFDA6ED421"),
    ("/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper", "Material", 255891, "392D4DDFE8E8DD00278364E2F71385960720257614BB30679135C073B417D0F9"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Blood/MF_Blood_Mask", "MaterialFunction", 22408, "57BDD2550E8A1586A9999CAB8307CDB4BA52A926C9B0B9EDF2FCD66045BB852E"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SandSnow_Mask", "MaterialFunction", 23681, "56FBDDDD6762F8CD54DAF49BE8B8B4EA098BD08626C79926FEDB35AA4CDFEA7D"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SkinnedHeightMask", "MaterialFunction", 12443, "02D08E46CC2DE5E92BD9559EDF802DA42FD42460E313F7B754CE0F129DD969F6"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Mud/MF_MudHeight_BaseColor", "MaterialFunction", 30871, "191537151EAD8A64A90F18C7BF19BD5711DDBAF7C5CEE9A2B122706F1CFA6124"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_BaseColor", "MaterialFunction", 20559, "1370B08DEB27C2103029D615080E3A130AC0B50F3B62FAC78164A17B64F294B7"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Normal", "MaterialFunction", 21777, "47CDF279EC8493331CACD9FFCB05754A0FD3F303E4786063E450BB1AE0839665"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Roughness", "MaterialFunction", 28930, "59D6D1E3B53A7AC68318BB618398C988C1A06FC3AFD72CC691535ECDA20AC4CB"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Smear/MF_Smear_Mask", "MaterialFunction", 24293, "D75FDB123DEADA3C94BE1EC96D41370450784E7F6D103A84AE515D043BC8EDDA"),
    ("/Game/DirtyPawnSystem/Textures/T_BloodScratch_Alpha", "Texture2D", 3151543, "B47114C5A32143F9D904E37D64CBB1DB3EEF0BC4F3C2E8F72CA9EEB0C3DB58F2"),
    ("/Game/DirtyPawnSystem/Textures/T_Noise_Normal", "Texture2D", 44168853, "4FCDADCE5A057901DC58A2D01C4F1222788A57776CEB9C300237C96A5962E252"),
    ("/Game/DirtyPawnSystem/Textures/T_Perlin_Noise_M", "Texture2D", 6790192, "A723B32150877CFFE0C0CCD63012645A6F164A7EA3060F997DC02F9B04FE233A"),
)
EXPECTED = {
    package: {"class": asset_class, "length": length, "sha256": digest}
    for package, asset_class, length, digest in ASSETS
}
EXPECTED_ORDER = tuple(row[0] for row in ASSETS)
EXPECTED_CLASS_COUNTS = {"Texture2D": 6, "MaterialFunction": 8, "Material": 1}
WRAPPER = ASSETS[3][0]
BLOOD_TEXTURE = ASSETS[12][0]
LOAD_ORDER = (
    ASSETS[0][0],
    ASSETS[1][0],
    ASSETS[2][0],
    ASSETS[12][0],
    ASSETS[13][0],
    ASSETS[14][0],
    ASSETS[6][0],
    ASSETS[5][0],
    ASSETS[4][0],
    ASSETS[7][0],
    ASSETS[8][0],
    ASSETS[9][0],
    ASSETS[10][0],
    ASSETS[11][0],
    WRAPPER,
)
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_DIRTYPAWN_ASSETS58_RESAVE_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def batch_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(
            package, rows[package]["length"], rows[package]["sha256"].upper()
        )
        for package in EXPECTED_ORDER
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def package_file(content_root, package):
    relative = package[len("/Game/") :].replace("/", os.sep) + ".uasset"
    return os.path.realpath(os.path.join(content_root, relative))


def relative_package_file(package):
    return (package[len("/Game/") :] + ".uasset").lower()


def snapshot_package_files(content_root):
    rows = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            rows[relative] = {"length": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    return rows


def inventory_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(name, rows[name]["length"], rows[name]["mtime_ns"])
        for name in sorted(rows)
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


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


def registry_state(registry):
    dependencies = {}
    class_counts = {}
    registered = {}
    redirectors = []
    options = dependency_options()
    for package in sorted(EXPECTED):
        assets = registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
        redirectors.extend(
            package for item in assets if class_name(item) == "ObjectRedirector"
        )
        primary = [
            item for item in assets if class_name(item) == EXPECTED[package]["class"]
        ]
        if len(primary) != 1:
            fail("Expected one registered primary asset for " + package)
        registered[package] = primary[0]
        value = class_name(primary[0])
        class_counts[value] = class_counts.get(value, 0) + 1
        dependencies[package] = sorted(
            {str(value) for value in registry.get_dependencies(package, options)}
        )
    if redirectors:
        fail("Redirectors found in exact DirtyPawn batch: " + repr(redirectors))
    if class_counts != EXPECTED_CLASS_COUNTS:
        fail("UE 5.8 class counts differ: " + repr(class_counts))
    return registered, class_counts, dependencies


def texture_dimension(texture, axis):
    method_names = (
        "blueprint_get_size_" + axis.lower(),
        "get_size_" + axis.lower(),
    )
    for name in method_names:
        method = getattr(texture, name, None)
        if callable(method):
            try:
                return int(method()), name
            except Exception:
                pass
    fail("Texture dimension API is unavailable for " + texture.get_path_name())


def texture_alpha_probe(texture):
    method = getattr(texture, "has_alpha_channel", None)
    if callable(method):
        try:
            return bool(method()), "has_alpha_channel"
        except Exception as exc:
            return None, "PENDING_API_EXCEPTION=" + repr(exc)
    return None, "PENDING_API_UNAVAILABLE"


def dirty_content_packages():
    try:
        packages = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    except Exception as exc:
        return [], repr(exc)
    result = []
    for package in packages:
        try:
            result.append(package.get_name())
        except Exception:
            result.append(str(package))
    return sorted(set(result)), ""


if len(EXPECTED) != EXPECTED_PACKAGE_COUNT or set(LOAD_ORDER) != set(EXPECTED):
    raise RuntimeError("Internal DirtyPawn load order differs from the exact allowlist")
if len(LOAD_ORDER) != EXPECTED_PACKAGE_COUNT:
    raise RuntimeError("Internal DirtyPawn load order contains duplicates")
if sum(row["length"] for row in EXPECTED.values()) != EXPECTED_SOURCE_BYTES:
    raise RuntimeError("Internal DirtyPawn byte total differs")
if batch_fingerprint(EXPECTED) != EXPECTED_SOURCE_FINGERPRINT:
    raise RuntimeError("Internal DirtyPawn fingerprint differs")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    fail("Project Content invariant failed")

phase_root = os.path.join(
    project_root, "Saved", "Migration", "Phase4", "DirtyPawnAssets"
)
migration_evidence_path = os.path.join(
    phase_root, "DirtyPawnAssets57Migration.json"
)
post_migration_gate_path = os.path.join(
    phase_root, "Gates", "POST_MIGRATION57_SafetyGate.json"
)
evidence_path = os.path.join(phase_root, "DirtyPawnAssets58Resave.json")
for required in (migration_evidence_path, post_migration_gate_path):
    if not os.path.isfile(required):
        fail("Required migration input is absent: " + required)

with open(migration_evidence_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
with open(post_migration_gate_path, "r", encoding="utf-8-sig") as handle:
    post_migration_gate = json.load(handle)
expected_relatives = sorted(relative_package_file(package) for package in EXPECTED)
if (
    migration.get("status")
    != "ASSETTOOLS_EXACT_DIRTYPAWN_ASSETS_MIGRATION_PASS"
    or migration.get("package_count") != EXPECTED_PACKAGE_COUNT
    or migration.get("created_package_count") != EXPECTED_PACKAGE_COUNT
    or migration.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or migration.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or migration.get("class_counts") != EXPECTED_CLASS_COUNTS
    or migration.get("ignore_dependencies") is not True
    or migration.get("target_delta_exact") is not True
    or migration.get("created_package_files") != expected_relatives
    or migration.get("removed_package_files")
    or migration.get("modified_existing_package_files")
    or migration.get("unexpected_game_dependencies")
    or migration.get("target_sidecars_created")
):
    fail("UE 5.7 AssetTools evidence does not match the frozen exact batch")
if (
    post_migration_gate.get("status")
    != "DIRTYPAWN_ASSETS_SOURCE_PROTECTED_SAFETY_PASS"
    or post_migration_gate.get("stage") != "POST_MIGRATION57"
    or post_migration_gate.get("source_fingerprint")
    != EXPECTED_SOURCE_FINGERPRINT
):
    fail("POST_MIGRATION57 source/protected gate is not PASS")

migration_rows = {row["package"]: row for row in migration.get("packages", [])}
if set(migration_rows) != set(EXPECTED) or len(migration_rows) != EXPECTED_PACKAGE_COUNT:
    fail("UE 5.7 migration package set differs from the exact allowlist")
expected_dependencies = migration.get("dependencies", {})
if set(expected_dependencies) != set(EXPECTED):
    fail("UE 5.7 migration dependency evidence is incomplete")

for package in sorted(EXPECTED):
    path = package_file(content_root, package)
    row = migration_rows[package]
    if not os.path.isfile(path):
        fail("Migrated target asset is absent: " + package)
    if os.path.realpath(row.get("file", "")).lower() != path.lower():
        fail("Migration evidence target path differs: " + package)
    if os.path.getsize(path) != row.get("length") or sha256(path) != row.get(
        "sha256"
    ):
        fail("Target bytes changed before UE 5.8 resave: " + package)
    if not isinstance(row.get("bytes_match_source"), bool):
        fail("Migration evidence lacks byte-identity classification: " + package)
    for extension in (".uexp", ".ubulk", ".uptnl"):
        sidecar = os.path.splitext(path)[0] + extension
        if os.path.exists(sidecar):
            fail("Unexpected pre-resave target sidecar exists: " + sidecar)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/_Game/Textures", "/Game/DirtyPawnSystem"], True)
registry.wait_for_completion()
registered_before, class_counts, dependencies_before = registry_state(registry)
if dependencies_before != expected_dependencies:
    fail("UE 5.8 pre-resave dependencies differ from UE 5.7 evidence")

inventory_before = snapshot_package_files(content_root)
inventory_fingerprint_before = inventory_fingerprint(inventory_before)
dirty_before, dirty_before_error = dirty_content_packages()
loaded_assets = {}
texture_rows = []
compile_rows = []
for package in LOAD_ORDER:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load in UE 5.8: " + package)
    actual_class = asset.get_class().get_name()
    if actual_class != EXPECTED[package]["class"]:
        fail(
            "Loaded class differs for {}: {} != {}".format(
                package, actual_class, EXPECTED[package]["class"]
            )
        )
    loaded_assets[package] = asset
    if actual_class == "Texture2D":
        size_x, size_x_api = texture_dimension(asset, "X")
        size_y, size_y_api = texture_dimension(asset, "Y")
        if size_x <= 0 or size_y <= 0:
            fail("Texture has invalid dimensions: " + package)
        alpha_value, alpha_api = texture_alpha_probe(asset)
        if package == BLOOD_TEXTURE:
            if (size_x, size_y) != (2048, 2048):
                fail("T_BloodScratch_Alpha is not 2048x2048")
            if alpha_value is False:
                fail("T_BloodScratch_Alpha reports no alpha channel")
        try:
            compression = str(asset.get_editor_property("compression_settings"))
        except Exception:
            compression = "PENDING_API_UNAVAILABLE"
        try:
            srgb = bool(asset.get_editor_property("srgb"))
        except Exception:
            srgb = None
        texture_rows.append(
            {
                "package": package,
                "class": actual_class,
                "size_x": size_x,
                "size_y": size_y,
                "size_x_api": size_x_api,
                "size_y_api": size_y_api,
                "has_alpha": alpha_value,
                "alpha_probe": alpha_api,
                "compression_settings": compression,
                "srgb": srgb,
                "validation": (
                    "PASS"
                    if package != BLOOD_TEXTURE or alpha_value is not None
                    else "PASS_DIMENSIONS_ALPHA_PENDING_API_UNAVAILABLE"
                ),
            }
        )

for package in LOAD_ORDER:
    actual_class = EXPECTED[package]["class"]
    if actual_class != "MaterialFunction":
        continue
    asset = loaded_assets[package]
    try:
        unreal.MaterialEditingLibrary.update_material_function(asset)
    except Exception as exc:
        fail("MaterialFunction compile/update failed for {}: {!r}".format(package, exc))
    compile_rows.append(
        {
            "package": package,
            "class": actual_class,
            "api": "MaterialEditingLibrary.update_material_function",
            "result": "NO_EXCEPTION",
        }
    )

try:
    unreal.MaterialEditingLibrary.recompile_material(loaded_assets[WRAPPER])
except Exception as exc:
    fail("DAZ wrapper material recompile failed: " + repr(exc))
compile_rows.append(
    {
        "package": WRAPPER,
        "class": "Material",
        "api": "MaterialEditingLibrary.recompile_material",
        "result": "NO_EXCEPTION",
    }
)
if len(compile_rows) != 9:
    fail("Expected eight MaterialFunction updates and one Material recompile")

save_rows = []
for package in LOAD_ORDER:
    path = package_file(content_root, package)
    before_sha256 = sha256(path)
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("UE 5.8 save failed: " + package)
    save_rows.append(
        {
            "package": package,
            "sha256_before_resave": before_sha256,
            "length_after_resave": os.path.getsize(path),
            "sha256_after_resave": sha256(path),
        }
    )

inventory_after_save = snapshot_package_files(content_root)
inventory_fingerprint_after_save = inventory_fingerprint(inventory_after_save)
created = sorted(set(inventory_after_save) - set(inventory_before))
removed = sorted(set(inventory_before) - set(inventory_after_save))
modified = sorted(
    name
    for name in set(inventory_before).intersection(inventory_after_save)
    if inventory_before[name] != inventory_after_save[name]
)
if created or removed or modified != expected_relatives:
    fail(
        "UE 5.8 resave delta is not exactly fifteen assets; created={!r}, "
        "removed={!r}, modified={!r}".format(created, removed, modified)
    )

for package in sorted(EXPECTED):
    path = package_file(content_root, package)
    for extension in (".uexp", ".ubulk", ".uptnl"):
        sidecar = os.path.splitext(path)[0] + extension
        if os.path.exists(sidecar):
            fail("UE 5.8 resave created an unexpected sidecar: " + sidecar)

modified_files = [package_file(content_root, package) for package in EXPECTED]
try:
    registry.scan_modified_asset_files(modified_files)
except Exception:
    registry.scan_paths_synchronous(
        ["/Game/_Game/Textures", "/Game/DirtyPawnSystem"], True
    )
registry.wait_for_completion()
registered_after, class_counts_after, dependencies_after = registry_state(registry)
if class_counts_after != EXPECTED_CLASS_COUNTS:
    fail("UE 5.8 post-resave class counts differ")
if dependencies_after != expected_dependencies:
    fail("UE 5.8 post-resave dependencies differ from UE 5.7 evidence")

loaded_assets.clear()
try:
    unreal.SystemLibrary.collect_garbage()
except Exception:
    pass
reloaded_rows = []
for package in LOAD_ORDER:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != EXPECTED[package]["class"]:
        fail("Post-resave reload failed: " + package)
    reloaded_rows.append(
        {
            "package": package,
            "class": asset.get_class().get_name(),
            "object_path": asset.get_path_name(),
        }
    )
inventory_after_reload = snapshot_package_files(content_root)
if inventory_after_reload != inventory_after_save:
    fail("Post-resave reload changed target Content package inventory")
dirty_after, dirty_after_error = dirty_content_packages()
dirty_allowlisted = sorted(set(dirty_after).intersection(EXPECTED))
if dirty_allowlisted:
    fail("Resaved DirtyPawn assets remain dirty: " + repr(dirty_allowlisted))

save_by_package = {row["package"]: row for row in save_rows}
target_rows = {}
package_rows = []
for package in sorted(EXPECTED):
    path = package_file(content_root, package)
    length = os.path.getsize(path)
    current_sha256 = sha256(path)
    target_rows[package] = {"length": length, "sha256": current_sha256}
    package_rows.append(
        {
            "package": package,
            "class": EXPECTED[package]["class"],
            "file": path,
            "length": length,
            "sha256": current_sha256,
            "source_length": EXPECTED[package]["length"],
            "source_sha256": EXPECTED[package]["sha256"],
            "migration_length": migration_rows[package]["length"],
            "migration_sha256": migration_rows[package]["sha256"],
            "sha256_before_resave": save_by_package[package][
                "sha256_before_resave"
            ],
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_DIRTYPAWN_ASSETS_LOAD_COMPILE_RESAVE_RELOAD_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "package_count": len(package_rows),
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "target_bytes_after_resave": sum(row["length"] for row in target_rows.values()),
    "target_fingerprint_after_resave": batch_fingerprint(target_rows),
    "class_counts": class_counts_after,
    "packages": package_rows,
    "dependencies_before_resave": dependencies_before,
    "dependencies_after_resave": dependencies_after,
    "unexpected_game_dependencies": [],
    "redirectors": [],
    "texture_validation": texture_rows,
    "texture_validation_count": len(texture_rows),
    "blood_texture_dimensions": [2048, 2048],
    "blood_texture_alpha_gate": next(
        row["validation"] for row in texture_rows if row["package"] == BLOOD_TEXTURE
    ),
    "material_compile_operations": compile_rows,
    "material_function_compile_count": 8,
    "material_compile_count": 1,
    "material_compile_api_result": "NO_EXCEPTION",
    "shader_log_error_scan": "PENDING_EXTERNAL_LOG_GATE",
    "asset_reload_requested": True,
    "reloaded_assets": reloaded_rows,
    "dirty_content_packages_before_load": dirty_before,
    "dirty_content_packages_after_reload": dirty_after,
    "dirty_content_probe_error_before": dirty_before_error,
    "dirty_content_probe_error_after": dirty_after_error,
    "dirty_allowlisted_packages_after_reload": [],
    "target_inventory_algorithm": "lowercase relative package path|length|mtime_ns",
    "target_package_file_count_before": len(inventory_before),
    "target_package_file_count_after": len(inventory_after_save),
    "target_inventory_fingerprint_before": inventory_fingerprint_before,
    "target_inventory_fingerprint_after": inventory_fingerprint_after_save,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": True,
    "target_sidecars_created": [],
    "post_migration_source_protected_gate": "PASS",
    "post_resave_source_protected_gate": "PENDING_EXTERNAL_GATE",
    "pie_requested": False,
    "visual_qa_requested": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DIRTYPAWN_ASSETS58_RESAVE_PASS: " + evidence_path)
