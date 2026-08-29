"""Create or validate the project-owned Dungeon Director V4 policy.

This operation is deliberately create-only:

* If the V4 asset is absent, it is created from the compiled native defaults,
  validated before the first save, and saved exactly once.
* If the asset already exists, it is validated read-only.  Authored values are
  never copied, reset, normalized, overwritten, or saved by this script.
* Every run measures the complete Content delta and records dirty state plus
  actual package mutations under Saved/Migration.

The script never loads or mutates /Game/Calysto vendor assets.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import traceback
from pathlib import Path

import unreal


EXPECTED_PROJECT_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
DESTINATION = "/Game/_Game/Data/CalystoDungeon/V4"
ASSET_NAME = "DA_CalystoDungeonDirectorPolicy"
ASSET_PATH = f"{DESTINATION}/{ASSET_NAME}"
CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
EXPECTED_SCHEMA_VERSION = 4
EXPECTED_GENERATOR_VERSION = 4
EXPECTED_POLICY_ID = "CalystoDungeonDirectorV4"
REQUIRED_RECALL_CLASS_PATH = (
    "/Game/_Game/Items/Companions/"
    "BP_Item_WintersRecall.BP_Item_WintersRecall_C"
)
REQUIRED_RECALL_PARENT_PATH = (
    "/Script/EFProjectSystemsGameplay.ProjectCompanionRevivalConsumable"
)
REQUIRED_CHEST_PARENT_PATH = "/Script/EFProjectSystemsGameplay.ProjectCalystoChestV4"
REQUIRED_CLOTHING_PARENT_PATH = "/Script/InventorySystem.ACFWorldItem"
REQUIRED_RUNTIME_BLUEPRINTS = (
    (REQUIRED_RECALL_CLASS_PATH, REQUIRED_RECALL_PARENT_PATH),
    (
        "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4."
        "BP_CalystoLockedChestV4_C",
        REQUIRED_CHEST_PARENT_PATH,
    ),
    (
        "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4."
        "BP_CalystoLockPickChestV4_C",
        REQUIRED_CHEST_PARENT_PATH,
    ),
    (
        "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4."
        "BP_CalystoArmorPickupV4_C",
        REQUIRED_CLOTHING_PARENT_PATH,
    ),
)

CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
ASSET_FILE = (
    CONTENT_ROOT
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V4"
    / f"{ASSET_NAME}.uasset"
)
RESULT_PATH = (
    Path(unreal.Paths.project_saved_dir())
    / "Migration"
    / "CalystoDungeonDirectorV4"
    / "CreateDirectorPolicyV4.json"
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def validate_execution_context() -> None:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Dungeon Director V4 policy authoring requires Unreal Engine 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong Unreal project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_PROJECT_ROOT).casefold():
        fail(
            "Refusing to run outside the writable NoShellForWinter target: "
            f"actual={PROJECT_ROOT} expected={EXPECTED_PROJECT_ROOT}"
        )
    expected_content_root = (EXPECTED_PROJECT_ROOT / "Content").resolve()
    if str(CONTENT_ROOT).casefold() != str(expected_content_root).casefold():
        fail(
            "Project Content root does not match the protected V4 target: "
            f"actual={CONTENT_ROOT} expected={expected_content_root}"
        )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    """Capture a byte-sensitive snapshot of every cooked Content package."""
    result: dict[str, tuple[int, int, str]] = {}
    for suffix in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
                sha256(path),
            )
    return result


def changed_content_paths(
    before: dict[str, tuple[int, int, str]],
    after: dict[str, tuple[int, int, str]],
) -> list[str]:
    return sorted(
        relative_path
        for relative_path in set(before) | set(after)
        if before.get(relative_path) != after.get(relative_path)
    )


def package_from_relative(relative_path: str) -> str:
    return "/Game/" + relative_path.rsplit(".", 1)[0]


def editor_property(owner, name: str):
    candidates = [name, name[0].lower() + name[1:]]
    if len(name) > 1 and name[0] == "b" and name[1].isupper():
        candidates.append(name[1].lower() + name[2:])

    snake: list[str] = []
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


def package_is_dirty(asset) -> bool:
    package_path = asset.get_outermost().get_path_name()
    dirty_packages = list(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_path for package in dirty_packages)


def call_native_validation(asset) -> tuple[bool, str]:
    validator = getattr(asset, "validate_policy", None)
    if not callable(validator):
        fail("V4 policy does not expose the required native ValidatePolicy wrapper")

    validation_result = validator()
    if isinstance(validation_result, tuple):
        if not validation_result:
            fail("ValidatePolicy returned an empty tuple")
        validation_ok = bool(validation_result[0])
        validation_error = (
            str(validation_result[1]) if len(validation_result) > 1 else ""
        )
    elif isinstance(validation_result, bool):
        validation_ok = validation_result
        validation_error = ""
    else:
        fail(
            "ValidatePolicy returned unsupported Python value "
            f"{validation_result!r}"
        )
    return validation_ok, validation_error


def validate_policy(asset, policy_class, require_saved_package: bool) -> dict:
    if asset is None:
        fail(f"Missing V4 policy asset: {ASSET_PATH}")
    if asset.get_class() != policy_class:
        fail(
            f"{ASSET_PATH} uses {asset.get_class().get_path_name()} instead of "
            f"{CLASS_PATH}"
        )

    schema_version = int(editor_property(asset, "SchemaVersion"))
    generator_version = int(editor_property(asset, "GeneratorVersion"))
    policy_id = str(editor_property(asset, "PolicyId"))
    if schema_version != EXPECTED_SCHEMA_VERSION:
        fail(
            f"V4 SchemaVersion is {schema_version}; expected "
            f"{EXPECTED_SCHEMA_VERSION}"
        )
    if generator_version != EXPECTED_GENERATOR_VERSION:
        fail(
            f"V4 GeneratorVersion is {generator_version}; expected "
            f"{EXPECTED_GENERATOR_VERSION}"
        )
    if policy_id != EXPECTED_POLICY_ID:
        fail(f"V4 PolicyId is {policy_id!r}; expected {EXPECTED_POLICY_ID!r}")

    validation_ok, validation_error = call_native_validation(asset)
    if not validation_ok:
        suffix = f": {validation_error}" if validation_error else ""
        fail(f"Native V4 policy validation failed{suffix}")

    policy_hash_method = getattr(asset, "get_policy_hash", None)
    if not callable(policy_hash_method):
        fail("V4 policy does not expose the required native GetPolicyHash wrapper")
    policy_hash = str(policy_hash_method()).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", policy_hash):
        fail("V4 policy returned an invalid canonical SHA-256")

    report = {
        "class": asset.get_class().get_path_name(),
        "schema_version": schema_version,
        "generator_version": generator_version,
        "policy_id": policy_id,
        "policy_hash": policy_hash,
        "native_validation": "PASS",
    }
    if require_saved_package:
        if not ASSET_FILE.is_file():
            fail(f"V4 policy package is missing on disk: {ASSET_FILE}")
        report["package_sha256"] = sha256(ASSET_FILE)
        report["package_size"] = int(ASSET_FILE.stat().st_size)
    return report


validate_execution_context()
before = content_snapshot()
created = False
save_api_succeeded = False
initial_asset_dirty = False

result = {
    "schema_version": 4,
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
        fail(f"Missing compiled V4 policy class: {CLASS_PATH}")
    if policy_class.get_path_name() != CLASS_PATH:
        fail(
            f"{CLASS_PATH} resolved through a redirect to "
            f"{policy_class.get_path_name()}"
        )

    for required_class_path, required_parent_path in REQUIRED_RUNTIME_BLUEPRINTS:
        required_parent = unreal.load_class(None, required_parent_path)
        required_class = unreal.load_class(None, required_class_path)
        if (
            required_parent is None
            or required_class is None
            or required_parent.get_path_name() != required_parent_path
            or required_class.get_path_name() != required_class_path
            or not unreal.MathLibrary.class_is_child_of(
                required_class, required_parent
            )
        ):
            fail(
                "Run Create-CalystoDungeonDirectorRuntimeAssetsV4.py before "
                f"policy authoring; {required_class_path} is missing or has "
                "the wrong native parent"
            )

    asset = unreal.load_asset(ASSET_PATH)
    if asset is not None:
        initial_asset_dirty = package_is_dirty(asset)
        if initial_asset_dirty:
            fail(
                "Existing V4 policy is dirty; refusing to validate an in-memory "
                "state that differs from the package on disk"
            )
    else:
        result["mode"] = "create_new"

        # Creation copies native defaults.  Validate the CDO before allocating
        # or dirtying a package so an invalid compiled schema fails closed.
        default_policy = unreal.get_default_object(policy_class)
        result["native_defaults"] = validate_policy(
            default_policy,
            policy_class,
            require_saved_package=False,
        )

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

        result["pre_save_policy"] = validate_policy(
            asset,
            policy_class,
            require_saved_package=False,
        )
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            asset,
            only_if_is_dirty=False,
        ):
            fail(f"Could not save newly created V4 policy {ASSET_PATH}")
        save_api_succeeded = True
        asset = unreal.load_asset(ASSET_PATH)

    result["policy"] = validate_policy(
        asset,
        policy_class,
        require_saved_package=True,
    )

    final_asset_dirty = package_is_dirty(asset)
    result["dirty_state"] = {
        "initial": initial_asset_dirty,
        "final": final_asset_dirty,
        "newly_dirty": final_asset_dirty and not initial_asset_dirty,
    }
    if final_asset_dirty:
        fail("V4 policy package remained dirty after create/read-only validation")

    after = content_snapshot()
    changed_files = changed_content_paths(before, after)
    mutations = [package_from_relative(path) for path in changed_files]
    expected_mutations = [ASSET_PATH] if created else []
    if mutations != expected_mutations:
        fail(
            "Create-only V4 Content delta differs: "
            f"actual={mutations} expected={expected_mutations}"
        )

    result["asset_mutations"] = mutations
    # A save is reported only when its expected on-disk byte mutation was
    # actually measured; a successful API return alone is not evidence.
    result["asset_saves"] = (
        [ASSET_PATH]
        if save_api_succeeded and ASSET_PATH in mutations
        else []
    )
    result["calysto_asset_mutations"] = [
        package for package in mutations if package.startswith("/Game/Calysto/")
    ]
    if result["calysto_asset_mutations"]:
        fail("V4 authoring touched protected /Game/Calysto packages")

    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    after = content_snapshot()
    changed_files = changed_content_paths(before, after)
    result["asset_mutations"] = [
        package_from_relative(path) for path in changed_files
    ]
    result["asset_saves"] = (
        [ASSET_PATH]
        if save_api_succeeded and ASSET_PATH in result["asset_mutations"]
        else []
    )
    result["calysto_asset_mutations"] = [
        package
        for package in result["asset_mutations"]
        if package.startswith("/Game/Calysto/")
    ]
finally:
    RESULT_PATH.parent.mkdir(parents=True, exist_ok=True)
    RESULT_PATH.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    unreal.log(
        "CALYSTO_DIRECTOR_POLICY_V4_RESULT="
        + json.dumps(result, sort_keys=True)
    )

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Unknown V4 policy authoring failure"))
