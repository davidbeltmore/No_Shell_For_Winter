"""Capture the exact authored Calysto V6 cook closure without saving assets.

Run this through the UE 5.8 Editor after every deliberate Director/material
edit and before a fresh package. The resulting receipt is the package script's
authoritative list of authored soft references. This keeps material authoring
extensible while proving that Calysto's RealisticBlood closure remains exactly
T_Splat_04 + T_Splat_N_04 through project-owned decal materials.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT = (PROJECT_ROOT / "Plugins" / "EFProcedural" / "Content").resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()

POLICY_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
POLICY_OBJECT = f"{POLICY_PACKAGE}.DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
POLICY_FILE = CONTENT_ROOT / "_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset"
EXPECTED_BUNDLES = (
    "CalystoFloorV6", "CalystoStyleV6", "CalystoThemeV6", "CalystoDecalsV6",
)
EXPECTED_BLOOD_PACKAGES = (
    "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04",
    "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04",
)
DECAL_ROOT = "/EFProcedural/Calysto/Internal/Materials/Decals"
DECAL_PACKAGES = (
    f"{DECAL_ROOT}/M_CalystoBloodDecal",
    f"{DECAL_ROOT}/MI_CalystoBloodDecal_Ceiling",
    f"{DECAL_ROOT}/MI_CalystoBloodDecal_Floor",
    f"{DECAL_ROOT}/MI_CalystoBloodDecal_Wall",
)
INTERNAL_PCG_PACKAGES = (
    "/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe",
    "/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe",
)
RUNTIME_BLUEPRINT_PACKAGES = (
    "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
)
FORBIDDEN_BUNDLE_TOKENS = tuple(
    f"/CalystoDungeon/V{version}/" for version in range(3, 6)
) + tuple(
    f"{blueprint}V{2 + 2}"
    for blueprint in (
        "BP_CalystoLockedChest",
        "BP_CalystoLockPickChest",
        "BP_CalystoArmorPickup",
    )
) + ("/RealisticBlood/Demo/", "/Niagara/")
RECEIPT = (
    SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" /
    "ValidateCookClosureV6.json"
)
CREATOR_RECEIPTS = {
    "Internal PCG creator": SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" /
        "CreateInternalPCGClosureV6.json",
    "Runtime Blueprint creator": SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" /
        "CreateRuntimeAssetsV6.json",
    "Decal material creator": SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" /
        "CreateDecalMaterialsV6.json",
    "Policy creator": SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" /
        "CreateDirectorPolicyV6.json",
}
CREATOR_FIRST_RUN_MODES = {
    "Internal PCG creator": "CREATE_ONCE_FROM_FROZEN_VENDOR_GRAPHS",
    "Runtime Blueprint creator": "CREATE_ONCE_FROM_FROZEN_BLUEPRINTS",
    "Decal material creator": "CREATE_ONCE",
    "Policy creator": "CREATE_ONCE_FROM_NATIVE_V6_DEFAULTS",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def package_name(object_path: str) -> str:
    return object_path.split(".", 1)[0]


def object_path(value: Any) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def package_file(package: str) -> Path | None:
    if package.startswith("/Game/"):
        return CONTENT_ROOT / (package[len("/Game/") :] + ".uasset")
    if package.startswith("/EFProcedural/"):
        return PLUGIN_CONTENT / (package[len("/EFProcedural/") :] + ".uasset")
    return None


def call_bool(asset: Any, method: str, label: str) -> None:
    function = getattr(asset, method, None)
    if not callable(function) or function() is not True:
        fail(f"{label} failed through {method}()")


def call_string(asset: Any, method: str, label: str) -> str:
    function = getattr(asset, method, None)
    value = str(function()) if callable(function) else ""
    if len(value) != 64 or any(char not in "0123456789ABCDEFabcdef" for char in value):
        fail(f"{label} is not SHA-256: {value!r}")
    return value.upper()


def call_string_list(asset: Any, method: str, label: str) -> list[str]:
    function = getattr(asset, method, None)
    if not callable(function):
        fail(f"Missing native Python function for {label}: {method}()")
    values = [str(value) for value in function()]
    if not values or len(values) != len(set(values)):
        fail(f"{label} is empty or contains duplicates: {values}")
    return values


def dirty_packages() -> set[str]:
    utility = getattr(unreal, "EditorLoadingAndSavingUtils", None)
    getter = getattr(utility, "get_dirty_content_packages", None)
    if not callable(getter):
        fail("EditorLoadingAndSavingUtils.get_dirty_content_packages is unavailable")
    return {str(package.get_path_name()) for package in getter()}


def dependency_options() -> Any:
    value = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references", "include_soft_package_references",
        "include_hard_management_references", "include_soft_management_references",
    ):
        value.set_editor_property(name, True)
    return value


def asset_hash_inventory(packages: tuple[str, ...] | list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for package in packages:
        disk = package_file(package)
        if disk is None:
            continue
        if not disk.is_file():
            fail(f"Required project-owned package is missing on disk: {package} -> {disk}")
        result[package] = sha256(disk)
    return dict(sorted(result.items()))


def creator_asset_hashes(label: str, document: dict[str, Any]) -> dict[str, str]:
    if label == "Runtime Blueprint creator":
        values = {
            str(row.get("package", "")): str(row.get("package_sha256", "")).upper()
            for row in document.get("blueprints", [])
        }
        expected = set(RUNTIME_BLUEPRINT_PACKAGES)
    elif label in {"Decal material creator", "Internal PCG creator"}:
        values = {
            str(package): str(row.get("package_sha256", "")).upper()
            for package, row in document.get("assets", {}).items()
        }
        expected = set(
            DECAL_PACKAGES if label == "Decal material creator"
            else INTERNAL_PCG_PACKAGES
        )
    elif label == "Policy creator":
        values = {
            POLICY_PACKAGE: str(
                document.get("policy", {}).get("package_sha256", "")
            ).upper()
        }
        expected = {POLICY_PACKAGE}
    else:
        fail(f"Unknown V6 creator receipt label: {label}")
    if set(values) != expected:
        fail(
            f"{label} asset hash inventory mismatch: "
            f"actual={sorted(values)} expected={sorted(expected)}"
        )
    for package, expected_hash in values.items():
        disk = package_file(package)
        if (
            disk is None
            or not disk.is_file()
            or len(expected_hash) != 64
            or sha256(disk) != expected_hash
        ):
            fail(f"{label} is stale for current package {package}")
    return dict(sorted(values.items()))


def validate_creator_receipts() -> dict[str, dict[str, Any]]:
    history_root = (
        SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" / "ReceiptHistory"
    ).resolve()
    report: dict[str, dict[str, Any]] = {}
    for label, canonical in CREATOR_RECEIPTS.items():
        if not canonical.is_file():
            fail(f"{label} receipt is missing: {canonical}")
        document = json.loads(canonical.read_text(encoding="utf-8"))
        if (
            document.get("status") != "PASS"
            or document.get("mode") != "VALIDATE_EXISTING_READ_ONLY"
            or document.get("created")
            or document.get("saved")
            or document.get("asset_mutations")
            or document.get("asset_mutations_final")
            or document.get("asset_saves")
            or int(document.get("save_api_calls", 0)) != 0
        ):
            fail(f"{label} is not the required zero-mutation idempotency receipt")
        history_value = str(document.get("append_only_receipt", "")).strip()
        if not history_value:
            fail(f"{label} has no append-only receipt path")
        history = Path(history_value).resolve()
        creator_history_root = (history_root / canonical.stem).resolve()
        try:
            history.relative_to(creator_history_root)
        except ValueError:
            fail(
                f"{label} append-only receipt escapes creator history root "
                f"{creator_history_root}: {history}"
            )
        if not history.is_file():
            fail(f"{label} append-only receipt is missing: {history}")
        if history.read_bytes() != canonical.read_bytes():
            fail(f"{label} canonical and append-only receipts differ")
        current_asset_hashes = creator_asset_hashes(label, document)

        first_run_mode = CREATOR_FIRST_RUN_MODES[label]
        first_run_receipts: list[Path] = []
        for candidate in sorted(creator_history_root.glob("*.json")):
            candidate_document = json.loads(candidate.read_text(encoding="utf-8"))
            if (
                candidate_document.get("status") == "PASS"
                and candidate_document.get("mode") == first_run_mode
            ):
                if Path(
                    str(candidate_document.get("append_only_receipt", ""))
                ).resolve() != candidate.resolve():
                    fail(f"{label} first-run receipt is not self-bound: {candidate}")
                created_evidence = (
                    candidate_document.get("created")
                    or candidate_document.get("asset_mutations")
                )
                saved_evidence = (
                    candidate_document.get("saved")
                    or candidate_document.get("asset_saves")
                )
                if not created_evidence or not saved_evidence:
                    fail(f"{label} first-run receipt has no creation/save evidence")
                first_run_receipts.append(candidate.resolve())
        if len(first_run_receipts) != 1:
            fail(
                f"{label} requires exactly one preserved successful first-run receipt "
                f"for {first_run_mode}; actual={first_run_receipts}"
            )
        first_run = first_run_receipts[0]
        report[label] = {
            "canonical": str(canonical),
            "canonical_sha256": sha256(canonical),
            "append_only": str(history),
            "append_only_sha256": sha256(history),
            "first_run": str(first_run),
            "first_run_sha256": sha256(first_run),
            "first_run_mode": first_run_mode,
            "current_asset_hashes": current_asset_hashes,
        }
    return dict(sorted(report.items()))


def validate_exact_bundle_references(
    registry: Any, bundle_paths: list[str]
) -> dict[str, dict[str, Any]]:
    """Resolve every authored soft reference and reject redirects or missing packages."""
    report: dict[str, dict[str, Any]] = {}
    for reference in bundle_paths:
        package = package_name(reference)
        if not package.startswith(("/Game/", "/EFProcedural/")):
            fail(f"V6 cook reference uses an unsupported mount: {reference}")
        if any(token.casefold() in reference.casefold() for token in FORBIDDEN_BUNDLE_TOKENS):
            fail(f"V6 cook reference contains a forbidden legacy/vendor token: {reference}")
        disk = package_file(package)
        if disk is None or not disk.is_file():
            fail(f"V6 cook reference package is missing on disk: {reference}")
        rows = list(
            registry.get_assets_by_package_name(
                package, include_only_on_disk_assets=True
            )
        )
        if not rows:
            fail(f"V6 cook reference has no on-disk Asset Registry row: {reference}")
        resolved = unreal.load_object(None, reference)
        if resolved is None and reference.endswith("_C"):
            resolved = unreal.load_class(None, reference)
        actual = object_path(resolved)
        if resolved is None or actual != reference:
            fail(
                "V6 cook reference is missing or resolves through a redirect: "
                f"requested={reference} actual={actual}"
            )
        report[reference] = {
            "package": package,
            "resolved_object": actual,
            "class": object_path(resolved.get_class()),
            "package_sha256": sha256(disk),
            "asset_registry_row_count": len(rows),
        }
    return dict(sorted(report.items()))


def asset_bundle_registry_tag(
    registry: Any, bundle_paths: list[str]
) -> dict[str, Any]:
    """Validate UE 5.8's typed, serialized on-disk AssetBundleData payload."""
    rows = list(
        registry.get_assets_by_package_name(
            POLICY_PACKAGE, include_only_on_disk_assets=True
        )
    )
    policy_rows = [
        row for row in rows
        if str(row.asset_name) == "DA_CalystoDungeonDirectorPolicy"
    ]
    if len(policy_rows) != 1:
        fail(f"Expected one V6 policy Asset Registry row; actual={len(policy_rows)}")
    bridge = getattr(unreal, "EFCalystoV6EditorValidationLibrary", None)
    names_api = getattr(bridge, "get_serialized_asset_bundle_names", None)
    paths_api = getattr(
        bridge, "get_serialized_asset_bundle_asset_paths", None
    )
    if not callable(names_api) or not callable(paths_api):
        fail("Project-owned typed AssetBundleData validation bridge is unavailable")
    bundle_names = [str(value) for value in names_api(POLICY_PACKAGE)]
    if not bundle_names:
        fail("V6 policy has no typed serialized AssetBundleData payload")
    if len(bundle_names) != len(set(bundle_names)):
        fail(f"Serialized V6 AssetBundleData repeats bundle names: {bundle_names}")
    bundle_map: dict[str, list[str]] = {}
    for bundle_name in bundle_names:
        paths = [
            str(value)
            for value in paths_api(POLICY_PACKAGE, bundle_name)
        ]
        if len(paths) != len(set(paths)):
            fail(f"Serialized V6 AssetBundleData repeats paths in {bundle_name}")
        bundle_map[bundle_name] = sorted(paths)

    expected_names = set(EXPECTED_BUNDLES)
    actual_names = set(bundle_map)
    if actual_names != expected_names:
        fail(
            "Serialized V6 AssetBundleData bundle names differ from native policy: "
            f"actual={sorted(actual_names)} expected={sorted(expected_names)}"
        )
    authored_paths = set(bundle_paths)
    floor_paths = set(bundle_map["CalystoFloorV6"])
    if floor_paths != authored_paths:
        fail(
            "Serialized CalystoFloorV6 must contain every authored soft reference "
            f"exactly once: missing={sorted(authored_paths - floor_paths)} "
            f"unexpected={sorted(floor_paths - authored_paths)}"
        )
    unexpected_by_bundle = {
        name: sorted(set(paths) - authored_paths)
        for name, paths in bundle_map.items()
        if set(paths) - authored_paths
    }
    empty_specialized = [
        name for name in EXPECTED_BUNDLES
        if name != "CalystoFloorV6" and not bundle_map[name]
    ]
    if unexpected_by_bundle or empty_specialized:
        fail(
            "Serialized V6 specialized bundles are invalid: "
            f"unexpected={unexpected_by_bundle} empty={empty_specialized}"
        )
    decal_packages = {
        package_name(path) for path in bundle_map["CalystoDecalsV6"]
    }
    expected_decal_packages = set(EXPECTED_BLOOD_PACKAGES) | {
        package for package in DECAL_PACKAGES if "/MI_" in package
    }
    if decal_packages != expected_decal_packages:
        fail(
            "Serialized CalystoDecalsV6 closure is not exact: "
            f"actual={sorted(decal_packages)} "
            f"expected={sorted(expected_decal_packages)}"
        )
    return {
        "storage": "FAssetData.TaggedAssetBundles",
        "bundles": dict(sorted(bundle_map.items())),
        "bundle_counts": {
            name: len(bundle_map[name]) for name in EXPECTED_BUNDLES
        },
    }


def realistic_blood_dependency_closure(registry: Any) -> tuple[list[str], dict[str, list[str]]]:
    options = dependency_options()
    queue = list(DECAL_PACKAGES)
    visited: set[str] = set()
    rows: dict[str, list[str]] = {}
    blood: set[str] = set()
    while queue:
        current = queue.pop(0)
        if current in visited:
            continue
        visited.add(current)
        dependencies = sorted({
            str(value) for value in registry.get_dependencies(current, options)
        })
        rows[current] = dependencies
        for dependency in dependencies:
            if dependency.startswith("/Game/RealisticBlood/"):
                blood.add(dependency)
            if dependency.startswith(DECAL_ROOT + "/") and dependency not in visited:
                queue.append(dependency)
    return sorted(blood), dict(sorted(rows.items()))


result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "status": "FAIL",
    "mode": "READ_ONLY_COOK_CLOSURE_VALIDATION",
    "asset_mutations": [],
    "asset_saves": [],
    "save_api_calls": 0,
}

before_hashes: dict[str, str] = {}
try:
    if PROJECT_ROOT != EXPECTED_ROOT:
        fail(f"Wrong project: actual={PROJECT_ROOT} expected={EXPECTED_ROOT}")
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8"):
        fail("Calysto V6 cook-closure validation requires Unreal Engine 5.8")
    creator_receipts = validate_creator_receipts()

    guarded_packages = (
        POLICY_PACKAGE, *DECAL_PACKAGES, *INTERNAL_PCG_PACKAGES,
        *EXPECTED_BLOOD_PACKAGES,
        *RUNTIME_BLUEPRINT_PACKAGES,
    )
    before_hashes = asset_hash_inventory(list(guarded_packages))
    dirty = dirty_packages()
    guarded_dirty = sorted(set(guarded_packages) & dirty)
    if guarded_dirty:
        fail(f"Save deliberate V6 authoring changes before closure validation: {guarded_dirty}")

    policy_class = unreal.load_class(None, POLICY_CLASS)
    policy = unreal.load_asset(POLICY_PACKAGE)
    if policy_class is None or policy is None:
        fail("Exact V6 policy class or object is missing")
    if str(policy.get_path_name()) != POLICY_OBJECT:
        fail(f"V6 policy object mismatch: {policy.get_path_name()}")
    if policy.get_class() != policy_class:
        fail(
            f"V6 policy class mismatch: actual={policy.get_class().get_path_name()} "
            f"expected={POLICY_CLASS}"
        )
    call_bool(policy, "validate_policy", "Native V6 policy validation")

    pcg_library = getattr(
        unreal, "EFCalystoPCGInternalClosureEditorLibrary", None
    )
    pcg_validator = getattr(
        pcg_library, "validate_internal_cooked_closure", None
    ) if pcg_library is not None else None
    if not callable(pcg_validator):
        fail("Native read-only V6 internal PCG closure validator is unavailable")
    pcg_error = str(pcg_validator())
    if pcg_error:
        fail(f"Internal cooked-safe PCG closure is invalid: {pcg_error}")

    hashes = {
        "gameplay": call_string(policy, "get_gameplay_hash", "Gameplay hash"),
        "authoring": call_string(policy, "get_authoring_hash", "Authoring hash"),
        "materials": call_string(policy, "get_material_hash", "Material hash"),
        "decals": call_string(policy, "get_decal_hash", "Decal hash"),
    }
    bundle_names = call_string_list(
        policy, "get_cook_bundle_names", "V6 cook bundle names"
    )
    if bundle_names != list(EXPECTED_BUNDLES):
        fail(f"Unexpected V6 bundle names: {bundle_names}")
    bundle_paths = call_string_list(
        policy, "get_cook_bundle_asset_paths", "V6 cook bundle asset paths"
    )
    bundle_packages = sorted({package_name(path) for path in bundle_paths})
    bundle_blood = sorted(
        package for package in bundle_packages
        if package.startswith("/Game/RealisticBlood/")
    )
    bundle_decal_materials = sorted(
        package for package in bundle_packages
        if package.startswith(DECAL_ROOT + "/")
    )
    bundle_internal_pcg = sorted(
        package for package in bundle_packages
        if package.startswith("/EFProcedural/Calysto/Internal/PCG/")
    )
    if bundle_blood != sorted(EXPECTED_BLOOD_PACKAGES):
        fail(
            "Authored V6 policy RealisticBlood closure must be exactly "
            f"T_Splat_04 + T_Splat_N_04: actual={bundle_blood}"
        )
    expected_instances = sorted(
        package for package in DECAL_PACKAGES if "/MI_" in package
    )
    if bundle_decal_materials != expected_instances:
        fail(
            "Authored V6 policy must use the three shared project-owned MIs: "
            f"actual={bundle_decal_materials} expected={expected_instances}"
        )
    if bundle_internal_pcg != sorted(INTERNAL_PCG_PACKAGES):
        fail(
            "Director V6 must own exactly the two cooked-safe internal PCG graphs: "
            f"actual={bundle_internal_pcg} expected={sorted(INTERNAL_PCG_PACKAGES)}"
        )
    missing_runtime_blueprints = sorted(
        set(RUNTIME_BLUEPRINT_PACKAGES) - set(bundle_packages)
    )
    if missing_runtime_blueprints:
        fail(
            "Director V6 cook closure is missing definitive runtime Blueprints: "
            f"{missing_runtime_blueprints}"
        )

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    exact_references = validate_exact_bundle_references(registry, bundle_paths)
    asset_bundle_validation = asset_bundle_registry_tag(registry, bundle_paths)
    dependency_blood, dependency_rows = realistic_blood_dependency_closure(registry)
    if dependency_blood != sorted(EXPECTED_BLOOD_PACKAGES):
        fail(
            "Project-owned decal material dependency closure must be exactly "
            f"T_Splat_04 + T_Splat_N_04: actual={dependency_blood}"
        )

    after_hashes = asset_hash_inventory(list(guarded_packages))
    if after_hashes != before_hashes:
        fail("A guarded V6/RealisticBlood asset changed during read-only validation")
    dirty_after = sorted(set(guarded_packages) & dirty_packages())
    if dirty_after:
        fail(f"Read-only closure validation dirtied packages: {dirty_after}")

    result["policy"] = {
        "object": POLICY_OBJECT,
        "class": POLICY_CLASS,
        "package_sha256": sha256(POLICY_FILE),
        "hashes": hashes,
        "bundle_names": bundle_names,
        "bundle_asset_paths": bundle_paths,
        "bundle_asset_count": len(bundle_paths),
        "bundle_asset_paths_sha256": hashlib.sha256(
            "\n".join(bundle_paths).encode("utf-8")
        ).hexdigest().upper(),
        "bundle_packages": bundle_packages,
        "serialized_asset_bundle_storage": asset_bundle_validation["storage"],
        "asset_bundle_data_validation": asset_bundle_validation,
        "exact_reference_validation": exact_references,
    }
    result["decal_dependency_audit"] = {
        "project_owned_packages": list(DECAL_PACKAGES),
        "realistic_blood_dependency_closure": dependency_blood,
        "expected_realistic_blood_dependency_closure": list(EXPECTED_BLOOD_PACKAGES),
        "dependencies_by_project_owned_package": dependency_rows,
    }
    result["internal_pcg_closure"] = {
        "status": "PASS",
        "packages": list(INTERNAL_PCG_PACKAGES),
        "runtime_validator": "ValidateInternalCookedClosure",
        "asset_hashes": asset_hash_inventory(list(INTERNAL_PCG_PACKAGES)),
    }
    result["guarded_asset_hashes"] = after_hashes
    result["creator_idempotency_receipts"] = creator_receipts
    result["dirty_packages_before"] = []
    result["dirty_packages_after"] = []
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
finally:
    result["asset_mutations"] = []
    result["asset_saves"] = []
    result["save_api_calls"] = 0
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    RECEIPT.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("CALYSTO_V6_COOK_CLOSURE_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Calysto V6 cook-closure validation failed"))
