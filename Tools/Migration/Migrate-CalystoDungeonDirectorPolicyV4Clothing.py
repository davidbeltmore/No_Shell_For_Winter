"""Add the initial Clothing pickup to an existing V4 policy, and nothing else.

The migration is deliberately narrow and idempotent:

* all six Clothing catalogs empty -> add the exact starter entry and save once;
* all six already contain that exact single entry -> validate read-only;
* any partial, non-empty, or conflicting authoring -> fail closed without save.

Run only after Create-CalystoDungeonDirectorRuntimeAssetsV4.py has created and
validated BP_CalystoArmorPickupV4. The script never touches /Game/Calysto.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import shutil
import traceback
from pathlib import Path

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
POLICY_PATH = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_ROOT / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
PICKUP_CLASS_PATH = (
    "/Game/_Game/Items/Clothing/"
    "BP_CalystoArmorPickupV4.BP_CalystoArmorPickupV4_C"
)
STABLE_ID = "Clothing.Armor.ACF.Default"
RESULT_PATH = (
    SAVED_ROOT
    / "Migration/CalystoDungeonDirectorV4/MigratePolicyV4Clothing.json"
)
BACKUP_ROOT = (
    SAVED_ROOT
    / "Migration/CalystoDungeonDirectorV4/PolicyBackupsBeforeClothing"
)

PROTECTED_FILES = (
    "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SpawnStartAndEnd.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SetRoomTheme.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_DungeonSpawner.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.uasset",
    "Content/Procedural/Maps/DungeonGeneration.umap",
    "Content/Procedural/DoorToLevel.uasset",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def file_record(path: Path) -> dict:
    return {
        "path": str(path),
        "exists": path.is_file(),
        "length": int(path.stat().st_size) if path.is_file() else None,
        "sha256": sha256(path) if path.is_file() else None,
    }


def protected_snapshot() -> dict:
    return {
        relative: file_record(PROJECT_ROOT / relative)
        for relative in PROTECTED_FILES
    }


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    result = {}
    for pattern in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(pattern):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
                sha256(path),
            )
    return result


def changed_packages(before, after) -> list[str]:
    return sorted(
        "/Game/" + relative.rsplit(".", 1)[0]
        for relative in set(before) | set(after)
        if before.get(relative) != after.get(relative)
    )


def prop(owner, name: str):
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()
    for candidate in (name, snake, name[0].lower() + name[1:]):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
    fail(f"Missing reflected property {name}")


def set_prop(owner, name: str, value) -> None:
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()
    for candidate in (name, snake, name[0].lower() + name[1:]):
        try:
            owner.set_editor_property(candidate, value)
            return
        except Exception:
            pass
    fail(f"Cannot set reflected property {name}")


def reference_text(value) -> str:
    if value is None:
        return ""
    for method_name in ("to_soft_object_path", "get_asset_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    get_path_name = getattr(value, "get_path_name", None)
    if callable(get_path_name):
        return str(get_path_name())
    return str(value)


def package_is_dirty(asset) -> bool:
    package_name = asset.get_outermost().get_path_name()
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_name for package in dirty)


def enum_token(value) -> str:
    """Normalize UE Python enum text across 5.8 wrapper representations."""
    text = str(value).rsplit(".", 1)[-1]
    return text.split(":", 1)[0].strip("<> ").upper()


def exact_entry(entry) -> bool:
    return all(
        (
            str(prop(entry, "Name")) == STABLE_ID,
            str(prop(entry, "StableId")) == STABLE_ID,
            prop(entry, "Rule") == unreal.EFCalystoCatalogRuleV4.ALLOW,
            reference_text(prop(entry, "ActorClass")) == PICKUP_CLASS_PATH,
            prop(entry, "Tier") == unreal.EFCalystoRarityTierV4.COMMON,
            bool(prop(entry, "bTierAgnostic")),
            len(list(prop(entry, "AllowedTiers"))) == 0,
            str(prop(entry, "Archetype")) == "Armor",
            prop(entry, "Gender") == unreal.EFCalystoGenderV4.ANY,
            abs(float(prop(entry, "InitialFraction")) - 1.0) <= 0.0001,
            abs(float(prop(entry, "DeepShare")) - 1.0) <= 0.0001,
            int(prop(entry, "RampFloors")) == 0,
            abs(float(prop(entry, "BaseThreatCost")) - 1.0) <= 0.0001,
            int(prop(entry, "ReferenceLevel")) == 50,
            int(prop(entry, "FirstEligibleFloor")) == 1,
            int(prop(entry, "MaxPerVariant")) == 10,
            int(prop(entry, "CooldownFloors")) == 0,
            prop(entry, "Lifecycle") == unreal.EFCalystoLifecycleV4.FLOOR_LOCAL,
        )
    )


def make_entry(pickup_class):
    entry = unreal.EFCalystoCatalogEntryV4()
    values = {
        "Name": STABLE_ID,
        "StableId": STABLE_ID,
        "Rule": unreal.EFCalystoCatalogRuleV4.ALLOW,
        "ActorClass": pickup_class,
        "Tier": unreal.EFCalystoRarityTierV4.COMMON,
        "bTierAgnostic": True,
        "AllowedTiers": [],
        "Archetype": "Armor",
        "Gender": unreal.EFCalystoGenderV4.ANY,
        "InitialFraction": 1.0,
        "DeepShare": 1.0,
        "RampFloors": 0,
        "BaseThreatCost": 1.0,
        "ReferenceLevel": 50,
        "FirstEligibleFloor": 1,
        "MaxPerVariant": 10,
        "CooldownFloors": 0,
        "Lifecycle": unreal.EFCalystoLifecycleV4.FLOOR_LOCAL,
    }
    for name, value in values.items():
        set_prop(entry, name, value)
    if not exact_entry(entry):
        fail("Constructed Clothing catalog entry did not round-trip exactly")
    return entry


def clothing_targets(asset) -> list[tuple[str, str, object, list, int, object]]:
    targets = []
    expected = {
        "Styles": {"STANDARD", "COMPACT", "BRANCHING"},
        "Themes": {"DEFAULT", "FORGE", "SHRINE"},
    }
    for group_name, id_property in (("Styles", "Style"), ("Themes", "Theme")):
        profiles = list(prop(asset, group_name))
        seen = set()
        for profile_index, profile in enumerate(profiles):
            profile_id = enum_token(prop(profile, id_property))
            seen.add(profile_id)
            categories = list(prop(profile, "Categories"))
            matches = [
                index
                for index, category in enumerate(categories)
                if prop(category, "Category")
                == unreal.EFCalystoContentCategoryV4.CLOTHING
            ]
            if len(matches) != 1:
                fail(
                    f"{group_name}.{profile_id} has {len(matches)} Clothing "
                    "categories; expected exactly one"
                )
            category_index = matches[0]
            targets.append(
                (
                    group_name,
                    profile_id,
                    profile,
                    categories,
                    category_index,
                    categories[category_index],
                )
            )
        if seen != expected[group_name]:
            fail(
                f"{group_name} IDs differ: actual={sorted(seen)} "
                f"expected={sorted(expected[group_name])}"
            )
    if len(targets) != 6:
        fail(f"Expected six Style/Theme Clothing targets; found {len(targets)}")
    return targets


def call_native_validation(asset) -> tuple[bool, str]:
    result = asset.validate_policy()
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return bool(result), ""


if PROJECT_ROOT != EXPECTED_ROOT:
    fail(f"Wrong writable target: {PROJECT_ROOT}")
if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
    fail("Dungeon Director V4 Clothing migration requires Unreal Engine 5.8")
if not POLICY_FILE.is_file():
    fail(f"Missing existing V4 policy package: {POLICY_FILE}")

# Python commandlets can start before the Blueprint inheritance registry has
# finished scanning.  Policy validation relies on those derived-class records
# and must never misclassify a valid soft actor class because of startup timing.
asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
asset_registry.wait_for_completion()

before_content = content_snapshot()
before_protected = protected_snapshot()
before_policy = file_record(POLICY_FILE)
saved = False
result = {
    "schema_version": 4,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "asset": POLICY_PATH,
    "operation": "add_only_clothing_catalog",
    "targeted_fields": [
        "Styles[*].Categories[Clothing].Catalog",
        "Themes[*].Categories[Clothing].Catalog",
    ],
    "before_policy": before_policy,
    "protected_before": before_protected,
    "asset_mutations": [],
    "asset_saves": [],
    "status": "FAIL",
}

try:
    policy_class = unreal.load_class(None, POLICY_CLASS_PATH)
    pickup_class = unreal.load_class(None, PICKUP_CLASS_PATH)
    if policy_class is None or policy_class.get_path_name() != POLICY_CLASS_PATH:
        fail(f"Missing exact policy class {POLICY_CLASS_PATH}")
    if pickup_class is None or pickup_class.get_path_name() != PICKUP_CLASS_PATH:
        fail(
            "Run Create-CalystoDungeonDirectorRuntimeAssetsV4.py first; "
            f"missing exact pickup class {PICKUP_CLASS_PATH}"
        )

    asset = unreal.load_asset(POLICY_PATH)
    if asset is None or asset.get_class() != policy_class:
        fail(f"{POLICY_PATH} is missing or has the wrong exact class")
    if package_is_dirty(asset):
        fail("V4 policy is dirty; refusing to merge into unsaved authoring")

    targets = clothing_targets(asset)
    catalogs = [list(prop(target[5], "Catalog")) for target in targets]
    all_empty = all(len(catalog) == 0 for catalog in catalogs)
    all_exact = all(len(catalog) == 1 and exact_entry(catalog[0]) for catalog in catalogs)
    if not all_empty and not all_exact:
        fail(
            "Clothing catalogs are partial, non-empty, or conflicting; "
            "no package was saved"
        )

    if all_exact:
        result["mode"] = "validate_already_migrated_read_only"
    else:
        result["mode"] = "migrate_all_six_empty_catalogs"
        backup_path = BACKUP_ROOT / (
            f"DA_CalystoDungeonDirectorPolicy_{before_policy['sha256']}.uasset"
        )
        BACKUP_ROOT.mkdir(parents=True, exist_ok=True)
        if backup_path.exists() and sha256(backup_path) != before_policy["sha256"]:
            fail(f"Existing backup has the wrong hash: {backup_path}")
        if not backup_path.exists():
            shutil.copy2(POLICY_FILE, backup_path)
        result["byte_exact_backup"] = file_record(backup_path)

        grouped = {"Styles": list(prop(asset, "Styles")), "Themes": list(prop(asset, "Themes"))}
        for group_name, profile_id, profile, categories, category_index, category in targets:
            set_prop(category, "Catalog", [make_entry(pickup_class)])
            categories[category_index] = category
            set_prop(profile, "Categories", categories)
            profiles = grouped[group_name]
            profile_index = next(
                index
                for index, candidate in enumerate(profiles)
                if str(prop(candidate, "Style" if group_name == "Styles" else "Theme"))
                .split(".")[-1]
                .upper()
                == profile_id
            )
            profiles[profile_index] = profile
        set_prop(asset, "Styles", grouped["Styles"])
        set_prop(asset, "Themes", grouped["Themes"])

        migrated_targets = clothing_targets(asset)
        if not all(
            len(list(prop(target[5], "Catalog"))) == 1
            and exact_entry(list(prop(target[5], "Catalog"))[0])
            for target in migrated_targets
        ):
            fail("In-memory add-only verification failed before save")
        valid, validation_error = call_native_validation(asset)
        if not valid:
            fail(f"Native V4 validation rejected add-only migration: {validation_error}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            fail(f"Could not save migrated policy {POLICY_PATH}")
        saved = True

    asset = unreal.load_asset(POLICY_PATH)
    if package_is_dirty(asset):
        fail("V4 policy remained dirty after Clothing migration/validation")
    final_targets = clothing_targets(asset)
    if not all(
        len(list(prop(target[5], "Catalog"))) == 1
        and exact_entry(list(prop(target[5], "Catalog"))[0])
        for target in final_targets
    ):
        fail("Saved/read-only V4 policy does not contain six exact Clothing entries")
    valid, validation_error = call_native_validation(asset)
    if not valid:
        fail(f"Final native policy validation failed: {validation_error}")

    after_content = content_snapshot()
    mutations = changed_packages(before_content, after_content)
    expected_mutations = [POLICY_PATH] if saved else []
    if mutations != expected_mutations:
        fail(
            f"Content delta differs: actual={mutations} "
            f"expected={expected_mutations}"
        )
    after_protected = protected_snapshot()
    if after_protected != before_protected:
        fail("A protected package hash changed during Clothing migration")

    result["asset_mutations"] = mutations
    result["asset_saves"] = [POLICY_PATH] if saved else []
    result["after_policy"] = file_record(POLICY_FILE)
    result["protected_after"] = after_protected
    result["protected_mismatches"] = []
    result["profile_count"] = 6
    result["entry"] = {
        "stable_id": STABLE_ID,
        "actor_class": PICKUP_CLASS_PATH,
        "tier": "Common",
        "first_eligible_floor": 1,
        "max_per_variant": 10,
        "tier_agnostic": True,
    }
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    after_content = content_snapshot()
    result["asset_mutations"] = changed_packages(before_content, after_content)
    result["asset_saves"] = [POLICY_PATH] if saved else []
    result["after_policy"] = file_record(POLICY_FILE)
    result["protected_after"] = protected_snapshot()
    result["protected_mismatches"] = [
        name
        for name in before_protected
        if before_protected[name] != result["protected_after"][name]
    ]
finally:
    RESULT_PATH.parent.mkdir(parents=True, exist_ok=True)
    RESULT_PATH.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    unreal.log("CALYSTO_V4_CLOTHING_MIGRATION_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Unknown V4 Clothing migration failure"))
