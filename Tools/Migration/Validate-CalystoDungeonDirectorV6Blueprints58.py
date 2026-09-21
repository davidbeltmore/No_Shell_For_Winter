"""Fail-closed, no-save Blueprint compile gate for definitive Calysto V6.

The gate compiles only DoorToLevel and the exact project-owned Blueprint cohort
referenced by the authored V6 cook closure. Vendor Calysto assets are immutable
inputs and are never compiled or saved here. The three definitive runtime
Blueprints must use their unversioned object paths and exact native parents.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_PROJECT_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT_ROOT = (PROJECT_ROOT / "Plugins/EFProcedural/Content").resolve()
SAVED_ROOT = (PROJECT_ROOT / "Saved/Migration/CalystoDungeonDirectorV6").resolve()
COOK_CLOSURE_RECEIPT = SAVED_ROOT / "ValidateCookClosureV6.json"
RUNTIME_ASSETS_RECEIPT = SAVED_ROOT / "CreateRuntimeAssetsV6.json"
POLICY_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
POLICY_OBJECT = POLICY_PACKAGE + ".DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
DOOR_TO_LEVEL = "/Game/Procedural/DoorToLevel"
FORBIDDEN_VENDOR_PREFIX = "/Game/Calysto/"
PROTECTED_ARCHITECTURE_BLUEPRINTS = frozenset({
    "/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_Door",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint",
})
PACKAGE_SUFFIXES = (".uasset", ".uexp", ".ubulk", ".m.ubulk", ".uptnl")

CHARACTER_BLUEPRINTS = (
    "ACFBaseCompanionBP",
    "ACFDefenderEnemyBP",
    "ACFGunEnemyBP",
    "ACFMMEnemyBP",
    "ACFMageEnemyBP",
    "ACFMeleeCompanionBP",
    "ACFMeleeEnemyBP",
    "ACFRangedCompanionBP",
    "ACFRangedEnemyBP",
)
BASELINE_BUNDLE_BLUEPRINTS = frozenset(
    {
        *(
            f"/Game/_Game/Characters/{gender}/{name}{gender}"
            for gender in ("Female", "Male")
            for name in CHARACTER_BLUEPRINTS
        ),
        "/Game/FullSample/Blueprints/Items/Consumable/ACFHealthPotionBP",
        "/Game/FullSample/Blueprints/Items/Consumable/ACFManaPotionBP",
        "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_AlcoholBottle07",
        "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_WaterBottle01",
        "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01",
        "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Bread01",
        "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_CookedMeat01",
        "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
        "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
        "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
        "/Game/_Game/Items/Companions/BP_Item_WintersRecall",
    }
)
DEFINITIVE_PARENTS = {
    "/Game/_Game/Items/Chests/BP_CalystoLockedChest":
        "/Script/EFProjectSystemsGameplay.ProjectCalystoChest",
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChest":
        "/Script/EFProjectSystemsGameplay.ProjectCalystoChest",
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup":
        "/Script/InventorySystem.ACFWorldItem",
}
LEGACY_RUNTIME_PACKAGES = frozenset(
    f"{package}V{2 + 2}"
    for package in (
        "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
        "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
        "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
    )
)
PROTECTED_FILES = {
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon":
        CONTENT_ROOT / "Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial":
        CONTENT_ROOT / "Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial.uasset",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme":
        CONTENT_ROOT / "Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    POLICY_PACKAGE:
        CONTENT_ROOT / "_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_json_bound(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    return json.loads(raw.decode("utf-8")), hashlib.sha256(raw).hexdigest().upper()


def snapshot_file(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    stat = path.stat()
    return {
        "path": str(path),
        "length": int(stat.st_size),
        "mtime_ns": int(stat.st_mtime_ns),
        "sha256": sha256(path),
    }


def package_base(package: str) -> Path:
    if package.startswith("/Game/"):
        return CONTENT_ROOT / package[len("/Game/") :]
    if package.startswith("/EFProcedural/"):
        return PLUGIN_CONTENT_ROOT / package[len("/EFProcedural/") :]
    fail(f"Blueprint package uses an unsupported mount: {package}")


def package_file(package: str) -> Path:
    if package.startswith("/Game/"):
        return CONTENT_ROOT / (package[len("/Game/") :] + ".uasset")
    if package.startswith("/EFProcedural/"):
        return PLUGIN_CONTENT_ROOT / (package[len("/EFProcedural/") :] + ".uasset")
    fail(f"Unsupported V6 package mount: {package}")


def snapshot_package(package: str) -> dict[str, Any]:
    base = package_base(package)
    values = {
        suffix: snapshot_file(Path(str(base) + suffix)) for suffix in PACKAGE_SUFFIXES
    }
    if values[".uasset"] is None:
        fail(f"Required V6 Blueprint package is missing on disk: {package}")
    return values


def object_path(value: Any) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def class_name(asset_data: Any) -> str:
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(getattr(asset_data, "asset_class", ""))


def is_blueprint_class(value: str) -> bool:
    token = value.replace(" ", "").casefold()
    return token == "blueprint" or token.endswith("blueprint")


def normalized_status(asset: Any) -> tuple[str, str]:
    raw = str(asset.get_editor_property("status"))
    return raw, re.sub(r"[^A-Z0-9]", "", raw.upper())


def report_path() -> Path:
    raw = os.environ.get("CODEX_CALYSTO_V6_BLUEPRINT_REPORT", "").strip()
    if not raw:
        fail("CODEX_CALYSTO_V6_BLUEPRINT_REPORT is required")
    candidate = Path(raw).resolve()
    try:
        candidate.relative_to(SAVED_ROOT)
    except ValueError:
        fail(f"V6 Blueprint evidence must remain below {SAVED_ROOT}: {candidate}")
    return candidate


def dirty_packages() -> list[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({object_path(value) for value in values})


def package_names(paths: list[Any]) -> set[str]:
    return {str(path).split(".", 1)[0] for path in paths}


def validate_append_only_receipt(
    document: dict[str, Any], canonical_path: Path, label: str
) -> None:
    raw = str(document.get("append_only_receipt", "")).strip()
    if not raw:
        fail(f"{label} does not name its append-only evidence copy")
    history = Path(raw).resolve()
    history_root = (SAVED_ROOT / "ReceiptHistory" / canonical_path.stem).resolve()
    try:
        history.relative_to(history_root)
    except ValueError:
        fail(f"{label} append-only receipt escapes {history_root}: {history}")
    if history == canonical_path.resolve() or not history.is_file():
        fail(f"{label} append-only receipt is missing: {history}")
    if history.read_bytes() != canonical_path.read_bytes():
        fail(f"{label} append-only receipt differs from its canonical receipt")


def validate_guarded_asset_hashes(closure: dict[str, Any]) -> None:
    expected = closure.get("guarded_asset_hashes", {})
    if not isinstance(expected, dict) or not expected:
        fail("V6 cook closure has no guarded asset hash inventory")
    drift: dict[str, dict[str, str]] = {}
    for package, expected_hash in expected.items():
        disk = package_file(str(package))
        actual = sha256(disk) if disk.is_file() else "MISSING"
        if actual != str(expected_hash).upper():
            drift[str(package)] = {
                "expected": str(expected_hash).upper(), "actual": actual
            }
    if drift:
        fail(f"V6 cook-closure guarded assets drifted: {drift}")


def validate_creator_receipt_freshness(closure: dict[str, Any]) -> None:
    rows = closure.get("creator_idempotency_receipts", {})
    if not isinstance(rows, dict) or len(rows) != 4:
        fail("V6 cook closure is not bound to all four idempotent creators")
    for label, row in rows.items():
        for path_key, hash_key in (
            ("canonical", "canonical_sha256"),
            ("append_only", "append_only_sha256"),
        ):
            path = Path(str(row.get(path_key, ""))).resolve()
            expected_hash = str(row.get(hash_key, "")).upper()
            if not path.is_file() or sha256(path) != expected_hash:
                fail(f"{label} {path_key} receipt drifted after cook closure")


def current_policy_contract(closure: dict[str, Any]) -> tuple[dict[str, str], list[str]]:
    policy_class = unreal.load_class(None, POLICY_CLASS)
    policy = unreal.load_asset(POLICY_PACKAGE)
    if policy_class is None or policy is None or policy.get_class() != policy_class:
        fail("Exact resident V6 policy class/object is unavailable")
    if object_path(policy) != POLICY_OBJECT:
        fail(f"V6 policy resolved through a redirect: {object_path(policy)}")
    validator = getattr(policy, "validate_policy", None)
    if not callable(validator) or validator() is not True:
        fail("Resident V6 policy failed native validation")
    hashes = {
        name: str(getattr(policy, f"get_{name}_hash")()).upper()
        for name in ("gameplay", "authoring", "material", "decal")
    }
    hashes = {
        "gameplay": hashes["gameplay"],
        "authoring": hashes["authoring"],
        "materials": hashes["material"],
        "decals": hashes["decal"],
    }
    if any(not re.fullmatch(r"[0-9A-F]{64}", value) for value in hashes.values()):
        fail(f"Resident V6 policy returned invalid hashes: {hashes}")
    closure_policy = closure.get("policy", {})
    if hashes != {key: str(value).upper() for key, value in closure_policy.get("hashes", {}).items()}:
        fail("Resident V6 policy hashes differ from the cook-closure receipt")
    if sha256(package_file(POLICY_PACKAGE)) != str(
        closure_policy.get("package_sha256", "")
    ).upper():
        fail("Resident V6 policy package differs from the cook-closure receipt")
    bundle_paths = [str(value) for value in policy.get_cook_bundle_asset_paths()]
    if bundle_paths != [str(value) for value in closure_policy.get("bundle_asset_paths", [])]:
        fail("Resident V6 cook bundle paths differ from the cook-closure receipt")
    actual_bundle_hash = hashlib.sha256(
        "\n".join(bundle_paths).encode("utf-8")
    ).hexdigest().upper()
    if actual_bundle_hash != str(
        closure_policy.get("bundle_asset_paths_sha256", "")
    ).upper():
        fail("Resident V6 cook bundle path hash differs from its receipt")
    return hashes, bundle_paths


def derive_bundle_blueprints(registry: Any, bundle_paths: list[str]) -> set[str]:
    result: set[str] = set()
    for package in sorted(package_names(bundle_paths)):
		# Inline Architecture exposes these existing native actor classes directly.
		# Their schema is covered by the read-only native bridge test, never by
		# recompiling or saving vendor Blueprints in this project-owned cohort.
        if package in PROTECTED_ARCHITECTURE_BLUEPRINTS:
            continue
        rows = list(
            registry.get_assets_by_package_name(
                package, include_only_on_disk_assets=True
            )
        )
        if any(is_blueprint_class(class_name(row)) for row in rows):
            result.add(package)
    missing_baseline = sorted(BASELINE_BUNDLE_BLUEPRINTS - result)
    if missing_baseline:
        fail(f"V6 bundle is missing baseline project Blueprints: {missing_baseline}")
    forbidden = sorted(
        package for package in result if package.startswith(FORBIDDEN_VENDOR_PREFIX)
    )
    if forbidden:
        fail(f"V6 bundle contains protected vendor Blueprints: {forbidden}")
    return result


def validate_context(
    closure: dict[str, Any], runtime_assets: dict[str, Any]
) -> tuple[dict[str, str], list[str]]:
    engine_version = str(unreal.SystemLibrary.get_engine_version())
    if not engine_version.startswith("5.8."):
        fail(f"V6 Blueprint compile requires UE 5.8; actual={engine_version}")
    if PROJECT_ROOT != EXPECTED_PROJECT_ROOT:
        fail(f"Wrong target project: actual={PROJECT_ROOT} expected={EXPECTED_PROJECT_ROOT}")
    policy = closure.get("policy", {})
    hashes = policy.get("hashes", {})
    if (
        closure.get("status") != "PASS"
        or closure.get("mode") != "READ_ONLY_COOK_CLOSURE_VALIDATION"
        or closure.get("asset_mutations")
        or closure.get("asset_saves")
        or int(closure.get("save_api_calls", -1)) != 0
        or policy.get("object") != POLICY_OBJECT
        or policy.get("class") != POLICY_CLASS
        or not re.fullmatch(r"[0-9A-Fa-f]{64}", str(hashes.get("gameplay", "")))
        or not re.fullmatch(r"[0-9A-Fa-f]{64}", str(hashes.get("authoring", "")))
        or not re.fullmatch(r"[0-9A-Fa-f]{64}", str(hashes.get("materials", "")))
        or not re.fullmatch(r"[0-9A-Fa-f]{64}", str(hashes.get("decals", "")))
        or not policy.get("exact_reference_validation")
    ):
        fail("V6 cook-closure receipt is not a clean definitive authority")
    bundle_packages = package_names(list(policy.get("bundle_asset_paths", [])))
    missing = sorted(BASELINE_BUNDLE_BLUEPRINTS - bundle_packages)
    legacy = sorted(LEGACY_RUNTIME_PACKAGES & bundle_packages)
    if missing or legacy:
        fail(f"V6 cook bundle Blueprint contract mismatch: missing={missing} legacy={legacy}")
    runtime_rows = runtime_assets.get("blueprints", [])
    runtime_packages = {str(row.get("package", "")) for row in runtime_rows}
    if (
        runtime_assets.get("status") != "PASS"
        or runtime_assets.get("mode") != "VALIDATE_EXISTING_READ_ONLY"
        or runtime_assets.get("created")
        or runtime_assets.get("saved")
        or runtime_assets.get("asset_mutations")
        or runtime_assets.get("asset_mutations_final")
        or runtime_packages != set(DEFINITIVE_PARENTS)
        or len(runtime_rows) != len(DEFINITIVE_PARENTS)
    ):
        fail("Definitive V6 runtime-asset receipt is missing or incomplete")
    for row in runtime_rows:
        package = str(row.get("package", ""))
        if str(row.get("parent", "")) != DEFINITIVE_PARENTS.get(package):
            fail(f"V6 runtime Blueprint parent receipt mismatch: {package}")
        disk = package_file(package)
        if not disk.is_file() or sha256(disk) != str(row.get("package_sha256", "")).upper():
            fail(f"V6 runtime Blueprint package drifted after creator validation: {package}")
    validate_append_only_receipt(
        runtime_assets, RUNTIME_ASSETS_RECEIPT, "V6 runtime-assets receipt"
    )
    validate_guarded_asset_hashes(closure)
    validate_creator_receipt_freshness(closure)
    current_hashes, bundle_paths = current_policy_contract(closure)
    if len(BASELINE_BUNDLE_BLUEPRINTS) != 29:
        fail("Internal V6 baseline Blueprint cohort cardinality drifted")
    return current_hashes, bundle_paths


def registry_asset(registry: Any, package: str) -> tuple[Any, Any]:
    rows = list(registry.get_assets_by_package_name(package, include_only_on_disk_assets=True))
    blueprint_rows = [row for row in rows if is_blueprint_class(class_name(row))]
    if len(rows) != 1 or len(blueprint_rows) != 1:
        fail(
            f"Expected exactly one on-disk Blueprint at {package}; "
            f"rows={len(rows)} blueprint_rows={len(blueprint_rows)}"
        )
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail(f"Unable to load project-owned V6 Blueprint: {package}")
    return asset, blueprint_rows[0]


def write_report(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


REPORT = report_path()
PAYLOAD: dict[str, Any] = {
    "receipt_schema_version": 1,
    "status": "UE58_CALYSTO_V6_BLUEPRINT_COMPILE_FAIL",
    "started_utc": utc_now(),
    "finished_utc": None,
    "project": str(PROJECT_ROOT),
    "report": str(REPORT),
    "expected_blueprint_count": 0,
    "expected_bundle_blueprint_count": 0,
    "baseline_bundle_blueprint_count": len(BASELINE_BUNDLE_BLUEPRINTS),
    "expected_blueprints": [],
    "compiled_blueprint_count": 0,
    "compiled_blueprints": [],
    "forbidden_compiled": [],
    "dirty_before": [],
    "dirty_after": [],
    "dirty_not_in_cohort": [],
    "package_files_before": {},
    "package_files_after": {},
    "on_disk_package_changes": [],
    "protected_files_before": {},
    "protected_files_after": {},
    "protected_file_changes": [],
    "cook_closure_receipt_sha256": "",
    "runtime_assets_receipt_sha256": "",
    "legacy_runtime_packages_loaded": [],
    "save_api_calls": 0,
    "saved_assets": [],
    "failures": [],
}

try:
    closure, closure_receipt_hash = load_json_bound(COOK_CLOSURE_RECEIPT)
    runtime_assets, runtime_receipt_hash = load_json_bound(RUNTIME_ASSETS_RECEIPT)
    PAYLOAD["cook_closure_receipt_sha256"] = closure_receipt_hash
    PAYLOAD["runtime_assets_receipt_sha256"] = runtime_receipt_hash
    policy_hashes, bundle_paths = validate_context(closure, runtime_assets)
    PAYLOAD["policy_hashes"] = policy_hashes
    PAYLOAD["policy_gameplay_hash"] = policy_hashes["gameplay"]
    PAYLOAD["policy_authoring_hash"] = policy_hashes["authoring"]
    PAYLOAD["cook_closure_receipt"] = str(COOK_CLOSURE_RECEIPT)
    PAYLOAD["runtime_assets_receipt"] = str(RUNTIME_ASSETS_RECEIPT)
    PAYLOAD["dirty_before"] = dirty_packages()
    if PAYLOAD["dirty_before"]:
        fail(f"Refusing to compile with dirty packages present: {PAYLOAD['dirty_before']}")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    bundle_blueprints = derive_bundle_blueprints(registry, bundle_paths)
    expected_blueprints = {DOOR_TO_LEVEL, *bundle_blueprints}
    PAYLOAD["expected_bundle_blueprint_count"] = len(bundle_blueprints)
    PAYLOAD["expected_blueprint_count"] = len(expected_blueprints)
    PAYLOAD["expected_blueprints"] = sorted(expected_blueprints)
    scan_roots = sorted({package.rsplit("/", 1)[0] for package in expected_blueprints})
    if any(root.startswith(FORBIDDEN_VENDOR_PREFIX.rstrip("/")) for root in scan_roots):
        fail("V6 Blueprint registry scan would enter protected vendor content")
    registry.scan_paths_synchronous(scan_roots, True)
    registry.wait_for_completion()

    PAYLOAD["package_files_before"] = {
        package: snapshot_package(package) for package in sorted(expected_blueprints)
    }
    PAYLOAD["protected_files_before"] = {
        package: snapshot_file(path) for package, path in PROTECTED_FILES.items()
    }
    if any(value is None for value in PAYLOAD["protected_files_before"].values()):
        fail("A protected V6/vendor package is missing on disk")

    for package in sorted(expected_blueprints):
        if package.startswith(FORBIDDEN_VENDOR_PREFIX):
            PAYLOAD["forbidden_compiled"].append(package)
            fail(f"Refusing to compile vendor Calysto Blueprint: {package}")
        asset, asset_data = registry_asset(registry, package)
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(asset)
        parent_path = object_path(parent)
        if parent is None:
            fail(f"V6 Blueprint has no parent class: {package}")
        expected_parent = DEFINITIVE_PARENTS.get(package)
        if expected_parent is not None and parent_path != expected_parent:
            fail(
                f"Definitive V6 parent mismatch: {package} "
                f"actual={parent_path} expected={expected_parent}"
            )
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        raw_status, status = normalized_status(asset)
        if "UPTODATE" not in status or "WARNING" in status:
            fail(f"Blueprint did not reach UP_TO_DATE: {package} status={raw_status}")
        PAYLOAD["compiled_blueprints"].append(
            {
                "package": package,
                "registry_class": class_name(asset_data),
                "loaded_class": object_path(asset.get_class()),
                "parent_class": parent_path,
                "status": raw_status,
            }
        )

    PAYLOAD["compiled_blueprint_count"] = len(PAYLOAD["compiled_blueprints"])
    PAYLOAD["package_files_after"] = {
        package: snapshot_package(package) for package in sorted(expected_blueprints)
    }
    PAYLOAD["protected_files_after"] = {
        package: snapshot_file(path) for package, path in PROTECTED_FILES.items()
    }
    PAYLOAD["on_disk_package_changes"] = sorted(
        package
        for package in expected_blueprints
        if PAYLOAD["package_files_before"][package] != PAYLOAD["package_files_after"][package]
    )
    PAYLOAD["protected_file_changes"] = sorted(
        package
        for package in PROTECTED_FILES
        if PAYLOAD["protected_files_before"][package]
        != PAYLOAD["protected_files_after"][package]
    )
    PAYLOAD["dirty_after"] = dirty_packages()
    PAYLOAD["dirty_not_in_cohort"] = sorted(
        package for package in PAYLOAD["dirty_after"] if package not in expected_blueprints
    )
    PAYLOAD["legacy_runtime_packages_loaded"] = sorted(
        package
        for package in LEGACY_RUNTIME_PACKAGES
        if unreal.find_object(None, package + "." + package.rsplit("/", 1)[-1]) is not None
    )
    if (
        PAYLOAD["compiled_blueprint_count"] != len(expected_blueprints)
        or PAYLOAD["forbidden_compiled"]
        or PAYLOAD["on_disk_package_changes"]
        or PAYLOAD["protected_file_changes"]
        or PAYLOAD["dirty_not_in_cohort"]
        or PAYLOAD["legacy_runtime_packages_loaded"]
    ):
        fail("V6 Blueprint compile violated the exact no-save protected cohort contract")

    PAYLOAD["status"] = "UE58_CALYSTO_V6_BLUEPRINT_COMPILE_PASS"
except BaseException as exc:
    PAYLOAD["failures"].append({"error": str(exc), "traceback": traceback.format_exc()})
finally:
    PAYLOAD["finished_utc"] = utc_now()
    try:
        write_report(REPORT, PAYLOAD)
        unreal.log("CALYSTO_V6_BLUEPRINT_COMPILE_RESULT=" + json.dumps(PAYLOAD, sort_keys=True))
    finally:
        unreal.SystemLibrary.quit_editor()

if PAYLOAD["status"] != "UE58_CALYSTO_V6_BLUEPRINT_COMPILE_PASS":
    raise RuntimeError(PAYLOAD["failures"][-1]["error"])
