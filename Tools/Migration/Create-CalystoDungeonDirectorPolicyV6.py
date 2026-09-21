"""Create once, then validate read-only, the definitive Calysto V6 policy.

Run this script only inside the protected NoShellForWinter UE 5.8 Editor.
Creation is intentionally blocked until the native V6 policy and factory exist.
The first creation reads and hashes the immutable V5 authoring receipt as
configuration provenance, then initializes the transformed output exclusively
through native V6 defaults. It never loads or saves a legacy policy asset and
never treats the rejected V5 DataTable as authority.

Required native Python surface on UEFCalystoDungeonDirectorPolicyV6Asset:

* initialize_v6_defaults()
* validate_policy()
* get_gameplay_hash(), get_authoring_hash()
* get_material_hash(), get_decal_hash()
* get_cook_bundle_names(), get_cook_bundle_asset_paths()

The first successful run may create and save exactly the V6 package. Every
later run is validation-only and permits no Content mutation or save call.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT_ROOT = (PROJECT_ROOT / "Plugins" / "EFProcedural" / "Content").resolve()
ENGINE_CONTENT_ROOT = Path(unreal.Paths.engine_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()

V6_DIRECTORY = "/Game/_Game/Data/CalystoDungeon/V6"
V6_ASSET_NAME = "DA_CalystoDungeonDirectorPolicy"
V6_PACKAGE = f"{V6_DIRECTORY}/{V6_ASSET_NAME}"
V6_OBJECT = f"{V6_PACKAGE}.{V6_ASSET_NAME}"
V6_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
V6_FACTORY_CLASS = (
    "/Script/EFProceduralEditor.EFCalystoDungeonDirectorPolicyV6AssetFactory"
)
V6_FILE = (
    CONTENT_ROOT
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V6"
    / f"{V6_ASSET_NAME}.uasset"
)

# These are the byte-exact migration inputs audited on 2026-09-04. They are
# required only as a pre-cutover preservation gate while creating V6. V6 is
# initialized from native V6 defaults and never reads legacy policy semantics.
# Once V6 exists, this tool no longer requires any legacy asset to remain.
LEGACY_INPUTS = {
    "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy": {
        "relative_file": "_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy.uasset",
        "sha256": "9824B1EFC3EF8B24D5C33DDF0813B0EC999B3C9F5331BDB8E48D771120868D3A",
    },
    "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy": {
        "relative_file": "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset",
        "sha256": "C6DDB0A100012108F170BA8F566E17D1EFF60A8FAF3C724904FE091B028739A1",
    },
    "/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy": {
        "relative_file": "_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy.uasset",
        "sha256": "CAA73BAD16A8562ED03AA8EDD2E66335F05778AE467DF73BAE9C40B5873551F3",
    },
    "/Game/_Game/Data/CalystoDungeon/V5/DT_CalystoDungeonDirectorPolicy": {
        "relative_file": "_Game/Data/CalystoDungeon/V5/DT_CalystoDungeonDirectorPolicy.uasset",
        "sha256": "5A5106980447387FE1B1F2F5648063361F8FA41B5B7D0E51C3D8816BE76DB4B5",
    },
}

PACKAGE_SUFFIXES = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
RECEIPT_PATH = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV6"
    / "CreateDirectorPolicyV6.json"
)
V5_AUTHORING_RECEIPT = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV5"
    / "CreateDirectorPolicyV5.json"
)
EXPECTED_V5_AUTHORING_RECEIPT_SHA256 = (
    "E98A22320DAC4C3C51DD6C9A87DDFD3740488B7936687910935EF7D65CD80A6E"
)
EXPECTED_V5_POLICY = (
    "/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy"
)
EXPECTED_V5_CLASS = (
    "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV5Asset"
)
EXPECTED_V5_GAMEPLAY_HASH = (
    "6892CD13FD63F965CDFED56538DEFF55370F89E43D3E2DE852C61525B73A7976"
)
EXPECTED_V5_AUTHORING_HASH = (
    "B2CEAF87D69B0680C3164DCC86D417BCA6830196D3B85E75079B1551C621862E"
)
EXPECTED_V5_BUNDLE_HASH = (
    "E456FE6C1A0EAFCEBFCB4ED3A350AC00446A9D85DD5B1BE2A29424EADA4AF92D"
)
V5_STONE_MATERIAL = (
    "/Game/ShareTextures/Wall/1K/MI_Stone_Wall_21.MI_Stone_Wall_21"
)

# This is an auditable semantic transform, not executable legacy compatibility.
# Native InitializeV6Defaults remains the only writer of the destination policy.
V5_TO_V6_MAPPING = {
    "mapping_schema_version": 1,
    "source": {
        "format": "immutable_authoring_receipt_json",
        "authority_package": EXPECTED_V5_POLICY,
        "authority_class": EXPECTED_V5_CLASS,
        "gameplay_hash": EXPECTED_V5_GAMEPLAY_HASH,
        "authoring_hash": EXPECTED_V5_AUTHORING_HASH,
        "hash_schema_version": 2,
        "semantic_record_count": 78,
        "bundle_names": ["CalystoFloorV5"],
        "bundle_asset_count": 33,
        "bundle_asset_paths_sha256": EXPECTED_V5_BUNDLE_HASH,
        "global_surface_material": V5_STONE_MATERIAL,
        "room_theme_material_profiles": ["Forge", "Shrine"],
    },
    "destination": {
        "initializer": "initialize_v6_defaults",
        "authority_package": V6_PACKAGE,
        "authority_class": V6_CLASS,
        "policy_id": "CalystoDungeonDirectorV6",
        "schema_version": 6,
        "generator_version": 6,
        "styles": [
            {
                "style_id": "Standard",
                "selection_weight": 0.50,
                "room_theme_chance": 0.25,
                "surface_material": "/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles",
            },
            {
                "style_id": "Compact",
                "selection_weight": 0.25,
                "room_theme_chance": 0.25,
                "surface_material": "/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles",
            },
            {
                "style_id": "Branching",
                "selection_weight": 0.25,
                "room_theme_chance": 0.25,
                "surface_material": "/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles",
            },
        ],
        "room_themes": [
            {
                "theme_id": "Forge",
                "selection_weight": 5.0,
                "conditional_probability": 0.625,
                "eligible_room_probability": 0.15625,
                "surface_material": "/Game/Calysto/Dungeon/Material/MI_RedTiles.MI_RedTiles",
            },
            {
                "theme_id": "Shrine",
                "selection_weight": 3.0,
                "conditional_probability": 0.375,
                "eligible_room_probability": 0.09375,
                "surface_material": "/Game/Calysto/Dungeon/Material/MI_GreenTile.MI_GreenTile",
            },
        ],
        "internal_no_theme_probability": 0.75,
        "cook_bundles": [
            "CalystoFloorV6",
            "CalystoStyleV6",
            "CalystoThemeV6",
            "CalystoDecalsV6",
        ],
        "runtime_blueprint_replacements": {
            "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4":
                "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
            "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4":
                "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
            "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4":
                "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
        },
    },
    "transformations": [
        "Replace the V5 floor-global Theme result with room-local NoTheme/Forge/Shrine decisions.",
        "Separate the 25 percent Theme-presence hash from the weighted Theme-type hash.",
        "Move dungeon materials inside each Style and room materials inside each Theme.",
        "Replace the shared V5 stone material with grey Style, red Forge, and green Shrine defaults.",
        "Split the single V5 cook bundle into session, Style, Theme, and decal V6 bundles.",
        "Replace versioned chest and armor Blueprint paths with definitive unversioned paths.",
        "Retain catalog intent as Style catalogs plus Forge/Shrine overlays under V6 hard floor budgets.",
        "Drop V5-to-V4 compiled runtime compatibility; V6 is the only runtime IR.",
    ],
}


class V6Blocked(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def fail(message: str) -> None:
    raise RuntimeError(message)


def block(code: str, message: str) -> None:
    raise V6Blocked(code, message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def validate_context() -> None:
    engine = str(unreal.SystemLibrary.get_engine_version())
    if not engine.startswith("5.8."):
        fail(f"Calysto V6 authoring requires UE 5.8; actual={engine}")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
        fail(
            "Refusing to run outside the writable target: "
            f"actual={PROJECT_ROOT} expected={EXPECTED_ROOT}"
        )
    if str(CONTENT_ROOT).casefold() != str((EXPECTED_ROOT / "Content").resolve()).casefold():
        fail(f"Unexpected Content root: {CONTENT_ROOT}")


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for path in CONTENT_ROOT.rglob("*"):
        if not path.is_file() or path.suffix.casefold() not in PACKAGE_SUFFIXES:
            continue
        stat = path.stat()
        result[path.relative_to(CONTENT_ROOT).as_posix()] = (
            int(stat.st_size),
            int(stat.st_mtime_ns),
            sha256(path),
        )
    return result


def changed_packages(
    before: dict[str, tuple[int, int, str]],
    after: dict[str, tuple[int, int, str]],
) -> tuple[list[str], list[str]]:
    files = sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )
    packages = sorted({"/Game/" + path.rsplit(".", 1)[0] for path in files})
    return files, packages


def dirty_package_names() -> list[str]:
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({str(package.get_path_name()) for package in packages})


def write_receipt_append_only(path: Path, payload: dict[str, Any]) -> Path:
    """Preserve every creator result before updating the canonical latest receipt."""
    stamp = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("generated_utc", "unknown"))
    ).strip("_")
    mode = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("mode", "UNKNOWN"))
    ).strip("_")
    history = path.parent / "ReceiptHistory" / path.stem / f"{stamp}_{mode}.json"
    history.parent.mkdir(parents=True, exist_ok=True)
    payload["append_only_receipt"] = str(history)
    serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    with history.open("x", encoding="utf-8") as stream:
        stream.write(serialized)
    path.write_text(serialized, encoding="utf-8")
    return history


def package_is_dirty(asset: Any) -> bool:
    package = str(asset.get_outermost().get_path_name())
    return package in dirty_package_names()


def load_exact_class(path: str, label: str) -> Any:
    value = unreal.load_class(None, path)
    if value is None:
        block(
            "BLOCKED_NATIVE_V6_CLASS_MISSING",
            f"Missing compiled {label}: {path}",
        )
    resolved = str(value.get_path_name())
    if resolved != path:
        fail(f"{path} resolved through a redirect to {resolved}")
    return value


def parse_native_bool(value: Any, operation: str) -> None:
    if isinstance(value, tuple):
        succeeded = bool(value[0]) if value else False
        error = str(value[1]) if len(value) > 1 and value[1] else ""
    elif isinstance(value, bool):
        succeeded, error = value, ""
    elif isinstance(value, str):
        succeeded, error = not value, value
    elif value is None:
        # UE 5.8 exposes a successful bool + empty FString& as None in some
        # reflected signatures. Native validation immediately follows.
        succeeded, error = True, ""
    else:
        fail(f"{operation} returned unsupported value {value!r}")
    if not succeeded:
        fail(f"{operation} failed" + (f": {error}" if error else ""))


def call_bool(owner: Any, method_name: str, operation: str, *args: Any) -> None:
    method = getattr(owner, method_name, None)
    if not callable(method):
        block(
            "BLOCKED_NATIVE_V6_INTERFACE_INCOMPLETE",
            f"{owner.get_class().get_path_name()} does not expose {method_name}()",
        )
    parse_native_bool(method(*args), operation)


def call_hash(owner: Any, method_name: str, label: str) -> str:
    method = getattr(owner, method_name, None)
    if not callable(method):
        block(
            "BLOCKED_NATIVE_V6_INTERFACE_INCOMPLETE",
            f"{owner.get_class().get_path_name()} does not expose {method_name}()",
        )
    value = str(method()).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", value):
        fail(f"{label} returned an invalid SHA-256: {value!r}")
    return value


def call_string_list(owner: Any, method_name: str, label: str) -> list[str]:
    method = getattr(owner, method_name, None)
    if not callable(method):
        block(
            "BLOCKED_NATIVE_V6_INTERFACE_INCOMPLETE",
            f"{owner.get_class().get_path_name()} does not expose {method_name}()",
        )
    values = sorted({str(item) for item in method()})
    if not values:
        fail(f"{label} is empty")
    return values


def package_file_for_reference(reference: str) -> Path:
    package = reference.split(".", 1)[0]
    if package.startswith("/Game/"):
        return CONTENT_ROOT / (package[len("/Game/") :] + ".uasset")
    if package.startswith("/EFProcedural/"):
        return PLUGIN_CONTENT_ROOT / (package[len("/EFProcedural/") :] + ".uasset")
    if package.startswith("/Engine/"):
        return ENGINE_CONTENT_ROOT / (package[len("/Engine/") :] + ".uasset")
    fail(f"V6 default reference uses an unsupported mount: {reference}")


def validate_reference_preconditions(asset: Any) -> dict[str, str]:
    """Make policy creation last by requiring its complete default closure first."""
    references = call_string_list(
        asset, "get_cook_bundle_asset_paths", "V6 default cook bundle paths"
    )
    report: dict[str, str] = {}
    for reference in references:
        disk = package_file_for_reference(reference)
        if not disk.is_file():
            block(
                "BLOCKED_V6_DEPENDENCY_MISSING",
                f"Create V6 runtime/PCG/decal dependencies before the policy: {reference}",
            )
        resolved = unreal.load_object(None, reference)
        if resolved is None and reference.endswith("_C"):
            resolved = unreal.load_class(None, reference)
        actual = "" if resolved is None else str(resolved.get_path_name())
        if actual != reference:
            fail(
                "V6 default reference is missing or redirected: "
                f"requested={reference} actual={actual}"
            )
        report[reference] = sha256(disk)
    return dict(sorted(report.items()))


def inspect_legacy_inputs(require_exact: bool) -> dict[str, Any]:
    report: dict[str, Any] = {}
    dirty = set(dirty_package_names())
    for package, spec in LEGACY_INPUTS.items():
        file_path = CONTENT_ROOT / Path(str(spec["relative_file"]))
        exists = file_path.is_file()
        row: dict[str, Any] = {
            "package": package,
            "file": str(file_path),
            "exists": exists,
            "dirty": package in dirty,
            "expected_sha256": spec["sha256"],
        }
        if exists:
            row["sha256"] = sha256(file_path)
            row["size"] = int(file_path.stat().st_size)
        report[package] = row
        if require_exact:
            if not exists:
                fail(f"Required immutable migration input is missing: {package}")
            if row["dirty"]:
                fail(f"Required immutable migration input is dirty: {package}")
            if row["sha256"] != spec["sha256"]:
                fail(
                    f"Migration input drift for {package}: "
                    f"actual={row['sha256']} expected={spec['sha256']}"
                )
    return report


def canonical_json_hash(value: Any) -> str:
    canonical = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    )
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest().upper()


def validate_v5_authoring_receipt(document: dict[str, Any]) -> None:
    policy = document.get("policy", {})
    visuals = policy.get("visual_materials", {})
    global_materials = visuals.get("global", {})
    theme_overrides = visuals.get("room_theme_overrides", {})
    bundle = document.get("asset_bundle_data", {})
    if (
        document.get("status") != "PASS"
        or document.get("mode") != "validate_existing_read_only"
        or document.get("idempotency_contract") != "read_only_zero_saves"
        or document.get("asset_path") != EXPECTED_V5_POLICY
        or document.get("class_path") != EXPECTED_V5_CLASS
        or policy.get("class_path") != EXPECTED_V5_CLASS
        or policy.get("gameplay_hash") != EXPECTED_V5_GAMEPLAY_HASH
        or policy.get("authoring_hash") != EXPECTED_V5_AUTHORING_HASH
        or int(policy.get("hash_schema_version", 0)) != 2
        or int(policy.get("semantic_record_count", 0)) != 78
        or policy.get("native_validation") != "PASS"
        or int(document.get("save_api_calls", -1)) != 0
        or document.get("asset_mutations")
        or document.get("asset_saves")
        or document.get("calysto_asset_mutations")
        or bundle.get("status") != "PASS"
        or not bool(bundle.get("closure_complete"))
        or bundle.get("bundle_names") != ["CalystoFloorV5"]
        or int(bundle.get("bundle_asset_count", 0)) != 33
        or bundle.get("bundle_asset_paths_sha256") != EXPECTED_V5_BUNDLE_HASH
        or any(
            global_materials.get(surface) != V5_STONE_MATERIAL
            for surface in ("floor", "wall", "roof")
        )
        or sorted(theme_overrides) != ["Forge", "Shrine"]
    ):
        fail("Immutable V5 authoring receipt does not match the reviewed migration source")

    for theme_name in ("Forge", "Shrine"):
        theme = theme_overrides.get(theme_name, {})
        for surface in ("floor", "wall", "roof"):
            slot = theme.get(surface, {})
            if not bool(slot.get("enabled")) or slot.get("material") != V5_STONE_MATERIAL:
                fail(
                    f"Immutable V5 {theme_name} {surface} material provenance drifted"
                )

    bundle_paths = [str(value) for value in bundle.get("bundle_asset_paths", [])]
    if (
        len(bundle_paths) != 33
        or hashlib.sha256("\n".join(bundle_paths).encode("utf-8")).hexdigest().upper()
        != EXPECTED_V5_BUNDLE_HASH
    ):
        fail("Immutable V5 bundle path order/hash does not match its receipt")
    required_versioned_runtime_paths = {
        "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4.BP_CalystoLockedChestV4_C",
        "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4.BP_CalystoLockPickChestV4_C",
        "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4.BP_CalystoArmorPickupV4_C",
    }
    if not required_versioned_runtime_paths.issubset(bundle_paths):
        fail("Immutable V5 receipt is missing the three reviewed runtime-path inputs")


def validate_migration_provenance(provenance: dict[str, Any]) -> None:
    mapping = provenance.get("mapping")
    if (
        provenance.get("source_receipt_sha256")
        != EXPECTED_V5_AUTHORING_RECEIPT_SHA256
        or mapping != V5_TO_V6_MAPPING
        or provenance.get("mapping_sha256") != canonical_json_hash(mapping)
        or provenance.get("source_kind") != "READ_ONLY_JSON_RECEIPT"
        or provenance.get("legacy_assets_loaded") != []
        or provenance.get("legacy_assets_saved") != []
        or provenance.get("runtime_dependency") is not False
    ):
        fail("Stored V5-to-V6 migration provenance is invalid or incomplete")


def migration_provenance(require_source_receipt: bool) -> dict[str, Any]:
    if V5_AUTHORING_RECEIPT.is_file():
        actual_hash = sha256(V5_AUTHORING_RECEIPT)
        if actual_hash != EXPECTED_V5_AUTHORING_RECEIPT_SHA256:
            fail(
                "Immutable V5 authoring receipt drift: "
                f"actual={actual_hash} expected={EXPECTED_V5_AUTHORING_RECEIPT_SHA256}"
            )
        try:
            document = json.loads(V5_AUTHORING_RECEIPT.read_text(encoding="utf-8"))
        except Exception as exc:
            fail(f"Immutable V5 authoring receipt is invalid JSON: {exc}")
        validate_v5_authoring_receipt(document)
        provenance = {
            "provenance_schema_version": 1,
            "source_kind": "READ_ONLY_JSON_RECEIPT",
            "source_receipt": str(V5_AUTHORING_RECEIPT),
            "source_receipt_present": True,
            "source_receipt_sha256": actual_hash,
            "mapping": V5_TO_V6_MAPPING,
            "mapping_sha256": canonical_json_hash(V5_TO_V6_MAPPING),
            "legacy_assets_loaded": [],
            "legacy_assets_saved": [],
            "runtime_dependency": False,
            "destination_authority": "NATIVE_V6_DEFAULTS",
        }
        validate_migration_provenance(provenance)
        return provenance

    if require_source_receipt:
        fail(
            "First V6 creation requires the immutable read-only V5 authoring receipt: "
            f"{V5_AUTHORING_RECEIPT}"
        )
    if not RECEIPT_PATH.is_file():
        fail("V5 receipt is retired and no durable V6 migration provenance exists")
    try:
        previous = json.loads(RECEIPT_PATH.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"Existing V6 authoring receipt is invalid JSON: {exc}")
    provenance = previous.get("migration_provenance", {})
    validate_migration_provenance(provenance)
    provenance = dict(provenance)
    provenance["source_receipt_present"] = False
    provenance["source_receipt"] = str(V5_AUTHORING_RECEIPT)
    return provenance


def validate_v6(asset: Any, policy_class: Any) -> dict[str, Any]:
    if asset is None or str(asset.get_path_name()) != V6_OBJECT:
        fail(f"Missing exact V6 object: {V6_OBJECT}")
    if asset.get_class() != policy_class:
        fail(
            f"V6 class mismatch: actual={asset.get_class().get_path_name()} "
            f"expected={V6_CLASS}"
        )
    call_bool(asset, "validate_policy", "Native V6 policy validation")
    hashes = {
        "gameplay": call_hash(asset, "get_gameplay_hash", "V6 gameplay hash"),
        "authoring": call_hash(asset, "get_authoring_hash", "V6 authoring hash"),
        "materials": call_hash(asset, "get_material_hash", "V6 material hash"),
        "decals": call_hash(asset, "get_decal_hash", "V6 decal hash"),
    }
    bundle_names = call_string_list(
        asset, "get_cook_bundle_names", "V6 cook bundle names"
    )
    bundle_assets = call_string_list(
        asset, "get_cook_bundle_asset_paths", "V6 cook bundle paths"
    )
    if "CalystoFloorV6" not in bundle_names:
        fail(f"V6 policy is missing CalystoFloorV6 bundle: {bundle_names}")
    if package_is_dirty(asset):
        fail("V6 policy is dirty; save deliberate authoring changes before validation")
    return {
        "status": "PASS",
        "object": str(asset.get_path_name()),
        "class": str(asset.get_class().get_path_name()),
        "hashes": hashes,
        "bundle_names": bundle_names,
        "bundle_asset_count": len(bundle_assets),
        "bundle_asset_paths_sha256": hashlib.sha256(
            "\n".join(bundle_assets).encode("utf-8")
        ).hexdigest().upper(),
        "package_file": str(V6_FILE),
        "package_sha256": sha256(V6_FILE) if V6_FILE.is_file() else "",
    }


def new_object(object_class: Any, label: str) -> Any:
    constructor = getattr(unreal, "new_object", None)
    if not callable(constructor):
        fail("Unreal Python does not expose new_object()")
    value = constructor(object_class)
    if value is None or value.get_class() != object_class:
        fail(f"Could not allocate exact transient {label}")
    return value


def v6_registered_assets(registry: Any) -> list[dict[str, str]]:
    registry.scan_paths_synchronous([V6_DIRECTORY], True)
    registry.wait_for_completion()
    rows = registry.get_assets_by_path(
        V6_DIRECTORY, recursive=True, include_only_on_disk_assets=False
    )

    def class_path_string(row: Any) -> str:
        value = row.asset_class_path
        try:
            package_name = str(value.get_editor_property("package_name"))
            asset_name = str(value.get_editor_property("asset_name"))
        except Exception:
            package_name = str(value.package_name)
            asset_name = str(value.asset_name)
        if not package_name.startswith("/Script/") or not asset_name:
            fail(f"Could not normalize AssetData class path: {value}")
        return f"{package_name}.{asset_name}"

    return sorted(
        (
            {
                "package": str(row.package_name),
                "asset": str(row.asset_name),
                "class": class_path_string(row),
            }
            for row in rows
        ),
        key=lambda row: (row["package"], row["asset"]),
    )


validate_context()
before = content_snapshot()
created = False
saved = False
created_asset: Any = None
result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "mode": "PREFLIGHT",
    "status": "FAIL",
    "v6_object": V6_OBJECT,
    "v6_class": V6_CLASS,
    "v6_factory_class": V6_FACTORY_CLASS,
    "initialization_source": "NATIVE_V6_DEFAULTS_NO_LEGACY_RUNTIME_DEPENDENCY",
    "asset_mutations": [],
    "asset_saves": [],
    "save_api_calls": 0,
}

try:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    registry.scan_paths_synchronous([V6_DIRECTORY], True)
    registry.wait_for_completion()
    # The Asset Registry can lag plugin/project package discovery across
    # unattended editor processes. Disk identity chooses create versus
    # validate; the exact UObject class and full policy contract are still
    # validated below before an existing authority is accepted.
    existing = unreal.load_asset(V6_PACKAGE) if V6_FILE.is_file() else None
    if V6_FILE.is_file() and existing is None:
        fail(f"V6 policy exists on disk but cannot be loaded: {V6_FILE}")
    require_legacy = existing is None
    result["legacy_inputs"] = inspect_legacy_inputs(require_exact=require_legacy)
    result["migration_provenance"] = migration_provenance(
        require_source_receipt=require_legacy
    )

    policy_class = load_exact_class(V6_CLASS, "V6 policy class")
    if existing is not None:
        result["mode"] = "VALIDATE_EXISTING_READ_ONLY"
        result["policy"] = validate_v6(existing, policy_class)
        result["dependency_preflight"] = validate_reference_preconditions(existing)
    else:
        result["mode"] = "CREATE_ONCE_FROM_NATIVE_V6_DEFAULTS"
        transient = new_object(policy_class, "native-default V6 policy")
        call_bool(
            transient,
            "initialize_v6_defaults",
            "Transient native V6 initialization",
        )
        call_bool(transient, "validate_policy", "Transient V6 validation")
        source_hashes = {
            "gameplay": call_hash(transient, "get_gameplay_hash", "Migrated gameplay hash"),
            "authoring": call_hash(transient, "get_authoring_hash", "Migrated authoring hash"),
            "materials": call_hash(transient, "get_material_hash", "Migrated material hash"),
            "decals": call_hash(transient, "get_decal_hash", "Migrated decal hash"),
        }
        result["native_default_hashes"] = source_hashes
        result["dependency_preflight"] = validate_reference_preconditions(transient)

        factory_class = load_exact_class(V6_FACTORY_CLASS, "typed V6 factory")
        factory = new_object(factory_class, "typed V6 factory")
        unreal.EditorAssetLibrary.make_directory(V6_DIRECTORY)
        created_asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            V6_ASSET_NAME,
            V6_DIRECTORY,
            policy_class,
            factory,
        )
        if created_asset is None:
            fail(f"Typed V6 factory failed to create {V6_PACKAGE}")
        created = True
        if str(created_asset.get_path_name()) != V6_OBJECT:
            fail(f"Typed factory created unexpected object {created_asset.get_path_name()}")
        call_bool(
            created_asset,
            "initialize_v6_defaults",
            "Persisted native V6 initialization",
        )
        call_bool(created_asset, "validate_policy", "Pre-save V6 validation")
        actual_hashes = {
            "gameplay": call_hash(created_asset, "get_gameplay_hash", "V6 gameplay hash"),
            "authoring": call_hash(created_asset, "get_authoring_hash", "V6 authoring hash"),
            "materials": call_hash(created_asset, "get_material_hash", "V6 material hash"),
            "decals": call_hash(created_asset, "get_decal_hash", "V6 decal hash"),
        }
        if actual_hashes != source_hashes:
            fail(
                "Persisted V6 migration differs from the transient oracle: "
                f"actual={actual_hashes} expected={source_hashes}"
            )
        result["save_api_calls"] = 1
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            created_asset, only_if_is_dirty=False
        ):
            fail(f"Could not save newly created V6 policy: {V6_PACKAGE}")
        saved = True
        existing = unreal.load_asset(V6_PACKAGE)
        result["policy"] = validate_v6(existing, policy_class)

    result["migration_provenance"]["destination_validation"] = {
        "object": result["policy"]["object"],
        "class": result["policy"]["class"],
        "hashes": result["policy"]["hashes"],
        "bundle_names": result["policy"]["bundle_names"],
        "native_policy_validation": result["policy"]["status"],
    }
    if result["migration_provenance"]["legacy_assets_loaded"] or result[
        "migration_provenance"
    ]["legacy_assets_saved"]:
        fail("V5-to-V6 receipt-only migration unexpectedly touched a legacy asset")

    registered = v6_registered_assets(registry)
    expected_registry = [
        {"package": V6_PACKAGE, "asset": V6_ASSET_NAME, "class": V6_CLASS}
    ]
    if registered != expected_registry:
        fail(
            "V6 folder must contain exactly the definitive policy asset: "
            f"actual={registered} expected={expected_registry}"
        )
    result["registered_v6_assets"] = registered

    legacy_after = inspect_legacy_inputs(require_exact=False)
    result["legacy_inputs_after"] = legacy_after
    if created and legacy_after != result["legacy_inputs"]:
        fail("One or more immutable legacy migration inputs changed during creation")

    after = content_snapshot()
    files, packages = changed_packages(before, after)
    result["changed_content_files"] = files
    result["asset_mutations"] = packages
    expected = [V6_PACKAGE] if created else []
    if packages != expected:
        fail(f"Unexpected Content delta: actual={packages} expected={expected}")
    result["asset_saves"] = [V6_PACKAGE] if saved else []
    if result["asset_saves"] != expected:
        fail(
            "Create/read-only save contract failed: "
            f"actual={result['asset_saves']} expected={expected}"
        )
    result["status"] = "PASS"
except V6Blocked as exc:
    result["status"] = exc.code
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
finally:
    try:
        after = content_snapshot()
        files, packages = changed_packages(before, after)
        result["changed_content_files"] = files
        result["asset_mutations"] = packages
    except Exception as delta_error:
        result["delta_error"] = str(delta_error)
    # Roll back only an asset created by this invocation when creation failed.
    # Legacy and pre-existing V6 assets are never deleted here.
    if result["status"] != "PASS" and created:
        try:
            result["failed_creation_cleanup_attempted"] = True
            result["failed_creation_cleanup_succeeded"] = bool(
                unreal.EditorAssetLibrary.delete_asset(V6_PACKAGE)
            )
        except Exception as cleanup_error:
            result["failed_creation_cleanup_error"] = str(cleanup_error)
    try:
        final = content_snapshot()
        final_files, final_packages = changed_packages(before, final)
        result["changed_content_files_final"] = final_files
        result["asset_mutations_final"] = final_packages
    except Exception as final_delta_error:
        result["final_delta_error"] = str(final_delta_error)
    RECEIPT_PATH.parent.mkdir(parents=True, exist_ok=True)
    write_receipt_append_only(RECEIPT_PATH, result)
    unreal.log("CALYSTO_DIRECTOR_POLICY_V6_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(f"{result['status']}: {result.get('error', 'unknown failure')}")
