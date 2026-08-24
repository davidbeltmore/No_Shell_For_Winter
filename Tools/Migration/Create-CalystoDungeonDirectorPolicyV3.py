"""Create or validate the single project-owned Dungeon Director V3 policy.

The operation is deliberately create-only.  When the policy already exists the
script validates it in place and refuses to save it, so authored Editor tuning
is never overwritten.  It never loads or mutates a /Game/Calysto vendor asset.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
import traceback
from pathlib import Path

import unreal


DESTINATION = "/Game/_Game/Data/CalystoDungeon/V3"
ASSET_NAME = "DA_CalystoDungeonDirectorPolicy"
ASSET_PATH = f"{DESTINATION}/{ASSET_NAME}"
CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy"
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
RESULT_PATH = Path(unreal.Paths.project_saved_dir()) / "Migration" / (
    "CalystoDungeonDirectorV3"
) / "CreateDirectorPolicyV3.json"
CREATION_CERTIFICATE_ENV = "CODEX_CALYSTO_POLICY_CREATION_CERTIFICATE"


def fail(message: str) -> None:
    raise RuntimeError(message)


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    """Capture a byte-sensitive Content snapshot for the create-only gate."""
    result: dict[str, tuple[int, int, str]] = {}
    for suffix in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
                hashlib.sha256(path.read_bytes()).hexdigest().upper(),
            )
    return result


def changed_content_paths(
    before: dict[str, tuple[int, int, str]],
    after: dict[str, tuple[int, int, str]],
) -> list[str]:
    return sorted(
        path
        for path in set(before) | set(after)
        if before.get(path) != after.get(path)
    )


def package_from_relative(relative_path: str) -> str:
    stem = relative_path.rsplit(".", 1)[0]
    return "/Game/" + stem


def sha256_for_asset() -> str:
    asset_file = CONTENT_ROOT / "_Game" / "Data" / "CalystoDungeon" / "V3" / (
        ASSET_NAME + ".uasset"
    )
    if not asset_file.is_file():
        fail(f"The V3 policy package was not written to disk: {asset_file}")
    return hashlib.sha256(asset_file.read_bytes()).hexdigest().upper()


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def is_under_saved(path: Path) -> bool:
    saved_root = Path(unreal.Paths.project_saved_dir()).resolve()
    try:
        path.resolve().relative_to(saved_root)
        return True
    except ValueError:
        return False


def load_creation_certificate() -> dict:
    raw_path = os.environ.get(CREATION_CERTIFICATE_ENV, "").strip()
    if not raw_path:
        fail(
            "The native V3 CDO intentionally has no validated sizes. Creating "
            f"the asset requires {CREATION_CERTIFICATE_ENV} to reference a "
            "PASS size-matrix receipt under Saved."
        )
    path = Path(raw_path).resolve()
    if not path.is_file() or not is_under_saved(path):
        fail(f"Creation certificate must be a file under Saved: {path}")
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if document.get("status") != "PASS" or document.get("success") is not True:
        fail("Creation certificate is not a successful PASS receipt")
    if document.get("asset_mutations") or document.get("asset_saves"):
        fail("Creation certificate reports asset mutations or saves")
    if (document.get("protected_assets") or {}).get("mismatches"):
        fail("Creation certificate reports protected-asset mismatches")

    policy = document.get("policy") or {}
    sizes = policy.get("candidate_validated_dungeon_sizes")
    if sizes is None:
        sizes = document.get("certified_sizes")
    sizes = sorted(int(value) for value in (sizes or []))
    certified = sorted(int(value) for value in document.get("certified_sizes", []))
    if not sizes or sizes != certified or len(sizes) != len(set(sizes)):
        fail("Creation certificate candidate sizes must exactly equal certified_sizes")
    summaries = document.get("size_summary") or {}
    for size in sizes:
        row = summaries.get(str(size)) or {}
        if row.get("certified") is not True or int(row.get("unique_seed_count", 0)) < 20:
            fail(f"Creation certificate does not prove 20 unique passing seeds for size {size}")

    policy_hashes = sorted(set(str(value) for value in policy.get("policy_hashes", [])))
    if len(policy_hashes) != 1 or not re.fullmatch(r"[0-9A-Fa-f]{64}", policy_hashes[0]):
        fail("Creation certificate must contain exactly one candidate PolicyHash")
    return {
        "path": str(path),
        "sha256": file_sha256(path),
        "validated_dungeon_sizes": sizes,
        "policy_hash": policy_hashes[0].upper(),
    }


def editor_property(owner, name: str):
    candidates = [name, name[0].lower() + name[1:]]
    if len(name) > 1 and name[0] == "b" and name[1].isupper():
        candidates.append(name[1].lower() + name[2:])
    snake = []
    for character in name:
        if character.isupper() and snake:
            snake.append("_")
        snake.append(character.lower())
    candidates.append("".join(snake))
    for candidate in dict.fromkeys(candidates):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
    fail(f"{ASSET_PATH} is missing required property {name}")


def is_asset_dirty(asset) -> bool:
    package_path = asset.get_outermost().get_path_name()
    dirty_packages = list(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_path for package in dirty_packages)


def soft_reference_path(value) -> str:
    """Return a stable, human-readable soft reference without resolving it."""
    for method_name in ("to_soft_object_path", "get_asset_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return str(value)


def population_catalog_report(asset, property_name: str) -> list[dict]:
    report = []
    for entry in list(editor_property(asset, property_name)):
        report.append(
            {
                "stable_id": str(editor_property(entry, "StableId")),
                "enabled": bool(editor_property(entry, "bEnabled")),
                "actor_class": soft_reference_path(
                    editor_property(entry, "ActorClass")
                ),
                "base_weight": int(editor_property(entry, "BaseWeight")),
                "cost": float(editor_property(entry, "Cost")),
                "minimum_floor": int(editor_property(entry, "MinimumFloor")),
                "max_per_floor": int(editor_property(entry, "MaxPerFloor")),
                "rarity": float(editor_property(entry, "Rarity")),
                "cooldown_floors": int(
                    editor_property(entry, "CooldownFloors")
                ),
            }
        )
    return report


def theme_catalog_report(asset) -> list[dict]:
    report = []
    for entry in list(editor_property(asset, "ThemeCatalog")):
        report.append(
            {
                "stable_id": str(editor_property(entry, "StableId")),
                "room_type": soft_reference_path(
                    editor_property(entry, "RoomType")
                ),
                "base_weight": int(editor_property(entry, "BaseWeight")),
                "bias_axis": float(editor_property(entry, "BiasAxis")),
            }
        )
    return report


def validate_policy(asset, policy_class, require_saved_package: bool) -> dict:
    if asset is None:
        fail(f"Missing V3 policy asset: {ASSET_PATH}")
    if asset.get_class() != policy_class:
        fail(
            f"{ASSET_PATH} uses {asset.get_class().get_path_name()} instead of "
            f"{CLASS_PATH}"
        )

    schema_version = int(editor_property(asset, "SchemaVersion"))
    generator_version = int(editor_property(asset, "GeneratorVersion"))
    policy_id = str(editor_property(asset, "PolicyId"))
    validated_sizes = list(editor_property(asset, "ValidatedDungeonSizes"))
    if schema_version != 3 or generator_version != 3:
        fail(
            "V3 policy version mismatch: schema={} generator={}".format(
                schema_version, generator_version
            )
        )
    if policy_id != "CalystoDungeonDirectorV3":
        fail(
            "V3 PolicyId must be the frozen contract "
            "CalystoDungeonDirectorV3"
        )
    if not validated_sizes:
        fail("V3 ValidatedDungeonSizes must not be empty")

    policy_hash_method = getattr(asset, "get_policy_hash", None)
    if not callable(policy_hash_method):
        fail("V3 policy does not expose the required native GetPolicyHash wrapper")
    policy_hash = str(policy_hash_method()).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", policy_hash):
        fail("V3 policy returned an invalid canonical SHA-256")

    validator = getattr(asset, "validate_policy", None)
    if not callable(validator):
        fail("V3 policy does not expose the required native ValidatePolicy wrapper")
    validation_result = validator()
    validation_error = ""
    if isinstance(validation_result, tuple):
        if not validation_result:
            fail("ValidatePolicy returned an empty tuple")
        validation_ok = bool(validation_result[0])
        if len(validation_result) > 1:
            validation_error = str(validation_result[1])
    elif isinstance(validation_result, bool):
        validation_ok = validation_result
    else:
        fail(
            "ValidatePolicy returned unsupported Python value {!r}".format(
                validation_result
            )
        )
    if not validation_ok:
        fail(
            "UEFCalystoDungeonDirectorPolicy::ValidatePolicy rejected the asset: "
            + validation_error
        )

    details = {
        "class": asset.get_class().get_path_name(),
        "schema_version": schema_version,
        "generator_version": generator_version,
        "policy_id": policy_id,
        "validated_dungeon_sizes": sorted(int(value) for value in validated_sizes),
        "validated_dungeon_size_count": len(validated_sizes),
        "policy_hash": policy_hash,
        "native_validation": "PASS",
        "catalogs": {
            "enemies": population_catalog_report(asset, "EnemyCatalog"),
            "food": population_catalog_report(asset, "FoodCatalog"),
            "chests": population_catalog_report(asset, "ChestCatalog"),
            "loot": population_catalog_report(asset, "LootCatalog"),
            "special_events": population_catalog_report(
                asset, "SpecialEventCatalog"
            ),
            "themes": theme_catalog_report(asset),
        },
    }
    if require_saved_package:
        details["sha256"] = sha256_for_asset()
    return details


before = content_snapshot()
created = False
save_completed = False
initial_asset_dirty = False
result = {
    "schema_version": 3,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "destination": DESTINATION,
    "asset_path": ASSET_PATH,
    "class_path": CLASS_PATH,
    "mode": "validate_existing_read_only",
    "asset_mutations": [],
    "asset_saves": [],
    "calysto_asset_mutations": [],
    "status": "FAIL",
}

try:
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.search_all_assets(True)
    asset_registry.wait_for_completion()

    policy_class = unreal.load_class(None, CLASS_PATH)
    if policy_class is None:
        fail(f"Missing compiled V3 policy class: {CLASS_PATH}")

    asset = unreal.load_asset(ASSET_PATH)
    if asset is not None:
        initial_asset_dirty = is_asset_dirty(asset)
        if initial_asset_dirty:
            fail(
                "Existing V3 policy loaded dirty; refusing to validate a state "
                "that differs from the package on disk"
            )
    if asset is None:
        result["mode"] = "create_new"
        creation_certificate = load_creation_certificate()
        result["creation_certificate"] = creation_certificate
        unreal.EditorAssetLibrary.make_directory(DESTINATION)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", policy_class)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ASSET_NAME,
            DESTINATION,
            policy_class,
            factory,
        )
        if asset is None:
            fail(f"AssetTools could not create {ASSET_PATH}")
        created = True
        asset.set_editor_property(
            "validated_dungeon_sizes",
            creation_certificate["validated_dungeon_sizes"],
        )
        # Fail closed before the first disk write. Native draft defaults plus
        # the certified allowlist must satisfy the complete V3 contract.
        result["pre_save_policy"] = validate_policy(
            asset, policy_class, require_saved_package=False
        )
        if result["pre_save_policy"]["policy_hash"] != creation_certificate["policy_hash"]:
            fail(
                "Creation certificate PolicyHash does not match the exact "
                "candidate asset that would be saved"
            )
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            asset, only_if_is_dirty=False
        ):
            fail(f"Could not save newly created policy {ASSET_PATH}")
        save_completed = True
        asset = unreal.load_asset(ASSET_PATH)

    result["policy"] = validate_policy(
        asset, policy_class, require_saved_package=True
    )
    final_asset_dirty = is_asset_dirty(asset)
    result["dirty_state"] = {
        "initial": initial_asset_dirty,
        "final": final_asset_dirty,
        "newly_dirty": final_asset_dirty and not initial_asset_dirty,
    }
    if final_asset_dirty:
        fail("V3 policy package remained dirty after create/validation")

    after = content_snapshot()
    changed_files = changed_content_paths(before, after)
    mutations = [package_from_relative(path) for path in changed_files]
    expected_mutations = [ASSET_PATH] if created else []
    if mutations != expected_mutations:
        fail(
            "Create-only V3 Content delta differs: actual={} expected={}".format(
                mutations, expected_mutations
            )
        )
    result["asset_mutations"] = mutations
    result["asset_saves"] = [ASSET_PATH] if save_completed else []
    result["calysto_asset_mutations"] = [
        package for package in mutations if package.startswith("/Game/Calysto/")
    ]
    if result["calysto_asset_mutations"]:
        fail("The V3 authoring operation touched protected Calysto packages")
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    after = content_snapshot()
    changed_files = changed_content_paths(before, after)
    result["asset_mutations"] = [
        package_from_relative(path) for path in changed_files
    ]
    result["asset_saves"] = [ASSET_PATH] if save_completed else []
    result["calysto_asset_mutations"] = [
        package
        for package in result["asset_mutations"]
        if package.startswith("/Game/Calysto/")
    ]
finally:
    RESULT_PATH.parent.mkdir(parents=True, exist_ok=True)
    RESULT_PATH.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("CALYSTO_DIRECTOR_POLICY_V3_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Unknown V3 policy authoring failure"))
