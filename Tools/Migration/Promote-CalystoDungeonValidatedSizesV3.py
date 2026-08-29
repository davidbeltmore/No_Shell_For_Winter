"""Atomically promote a certified Dungeon Director V3 size allowlist.

The size matrix runs against a transient candidate policy.  This tool accepts
only a PASS receipt whose candidate list and PolicyHash reproduce exactly from
the current authored asset.  It backs up the package under Saved, modifies and
saves only the V3 policy, and records a byte-sensitive before/after receipt.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
import shutil
import traceback
from pathlib import Path

import unreal


ASSET_PATH = "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy"
CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy"
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
ASSET_FILE = (
    CONTENT_ROOT
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V3"
    / "DA_CalystoDungeonDirectorPolicy.uasset"
)
MATRIX_RECEIPT_ENV = "CODEX_CALYSTO_POLICY_PROMOTION_RECEIPT"
OUTPUT_ENV = "CODEX_CALYSTO_POLICY_PROMOTION_OUTPUT"
DEFAULT_OUTPUT = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV3"
    / "PromoteValidatedDungeonSizesV3.json"
)
BACKUP_ROOT = (
    SAVED_ROOT
    / "Migration"
    / "CalystoDungeonDirectorV3"
    / "PolicyPromotionBackups"
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def require_under_saved(path: Path, label: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(SAVED_ROOT)
    except ValueError:
        fail(f"{label} must stay under {SAVED_ROOT}: {resolved}")
    return resolved


def editor_property(owner, name: str):
    candidates = [name, name[0].lower() + name[1:]]
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


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for suffix in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
                sha256_file(path),
            )
    return result


def changed_content_paths(
    before: dict[str, tuple[int, int, str]],
    after: dict[str, tuple[int, int, str]],
) -> list[str]:
    return sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )


def package_from_relative(relative_path: str) -> str:
    return "/Game/" + relative_path.rsplit(".", 1)[0]


def valid_sha256(value: str) -> bool:
    return bool(re.fullmatch(r"[0-9A-Fa-f]{64}", value or ""))


def extract_matrix_contract(path: Path) -> dict:
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if document.get("status") != "PASS" or document.get("success") is not True:
        fail("The size-matrix receipt is not a successful PASS")
    if document.get("asset_mutations") or document.get("asset_saves"):
        fail("The size-matrix receipt reports asset mutations or saves")
    if (document.get("protected_assets") or {}).get("mismatches"):
        fail("The size-matrix receipt reports protected-asset mismatches")
    if not str(document.get("engine_version", "")).startswith("5.8"):
        fail("The size-matrix receipt was not produced by Unreal Engine 5.8")

    policy = document.get("policy") or {}
    initial = policy.get("initial") or {}
    candidate_sizes = policy.get("candidate_validated_dungeon_sizes")
    if candidate_sizes is None:
        candidate_sizes = (policy.get("candidate") or {}).get(
            "validated_dungeon_sizes"
        )
    if candidate_sizes is None:
        fail(
            "The receipt lacks policy.candidate_validated_dungeon_sizes; an "
            "exact-size override against a different allowlist cannot be promoted"
        )
    candidate_sizes = sorted(int(value) for value in candidate_sizes)
    certified_sizes = sorted(int(value) for value in document.get("certified_sizes", []))
    if (
        not candidate_sizes
        or candidate_sizes != certified_sizes
        or len(candidate_sizes) != len(set(candidate_sizes))
        or any(value < 18 or value > 30 for value in candidate_sizes)
    ):
        fail("Candidate sizes must be unique, in 18..30, and exactly equal certified_sizes")

    summaries = document.get("size_summary") or {}
    for size in candidate_sizes:
        summary = summaries.get(str(size)) or {}
        if (
            summary.get("certified") is not True
            or summary.get("passed_requested_matrix") is not True
            or int(summary.get("unique_seed_count", 0)) < 20
            or int(summary.get("failed_seed_count", 0)) != 0
        ):
            fail(f"The receipt does not prove a clean 20-seed matrix for size {size}")

    cases = document.get("cases") or []
    for size in candidate_sizes:
        size_cases = [row for row in cases if int(row.get("edge", -1)) == size]
        if len({int(row.get("run_seed", -1)) for row in size_cases}) < 20:
            fail(f"The receipt has fewer than 20 unique case seeds for size {size}")
        if any(row.get("success") is not True for row in size_cases):
            fail(f"The receipt contains a failed case for size {size}")

    policy_hashes = sorted(set(str(value).upper() for value in policy.get("policy_hashes", [])))
    if len(policy_hashes) != 1 or not valid_sha256(policy_hashes[0]):
        fail("The receipt must contain exactly one candidate PolicyHash")
    explicit_candidate_hash = str(policy.get("candidate_policy_hash", "")).upper()
    if explicit_candidate_hash and explicit_candidate_hash != policy_hashes[0]:
        fail("The receipt's candidate_policy_hash disagrees with resolved policy_hashes")

    schema_version = int(policy.get("schema_version", initial.get("schema_version", 0)))
    generator_version = int(
        policy.get("generator_version", initial.get("generator_version", 0))
    )
    policy_id = str(policy.get("policy_id", initial.get("policy_id", "")))
    if schema_version != 3 or generator_version != 3 or policy_id != "CalystoDungeonDirectorV3":
        fail("The receipt's V3 policy identity is invalid")

    source_sha = str(initial.get("uasset_sha256", "")).upper()
    if not valid_sha256(source_sha):
        fail("The receipt lacks the source policy uasset SHA-256")
    source_sizes = sorted(
        int(value) for value in initial.get("validated_dungeon_sizes", [])
    )
    return {
        "document": document,
        "candidate_sizes": candidate_sizes,
        "candidate_policy_hash": policy_hashes[0],
        "source_uasset_sha256": source_sha,
        "source_validated_dungeon_sizes": source_sizes,
        "schema_version": schema_version,
        "generator_version": generator_version,
        "policy_id": policy_id,
    }


def policy_document(asset, allow_invalid_policy_hash: bool = False) -> dict:
    get_hash = getattr(asset, "get_policy_hash", None)
    if not callable(get_hash):
        fail("The compiled V3 policy lacks GetPolicyHash")
    policy_hash = str(get_hash()).upper()
    if not valid_sha256(policy_hash) and not allow_invalid_policy_hash:
        fail("The compiled V3 policy returned an invalid PolicyHash")
    return {
        "class": asset.get_class().get_path_name(),
        "schema_version": int(editor_property(asset, "SchemaVersion")),
        "generator_version": int(editor_property(asset, "GeneratorVersion")),
        "policy_id": str(editor_property(asset, "PolicyId")),
        "validated_dungeon_sizes": sorted(
            int(value) for value in list(editor_property(asset, "ValidatedDungeonSizes"))
        ),
        "policy_hash": policy_hash,
        "uasset_sha256": sha256_file(ASSET_FILE),
        "dirty": is_asset_dirty(asset),
    }


receipt_path_raw = os.environ.get(MATRIX_RECEIPT_ENV, "").strip()
if not receipt_path_raw:
    raise RuntimeError(f"Missing required environment variable {MATRIX_RECEIPT_ENV}")
receipt_path = require_under_saved(Path(receipt_path_raw), "Matrix receipt")
if not receipt_path.is_file():
    raise RuntimeError(f"Matrix receipt does not exist: {receipt_path}")
output_path = require_under_saved(
    Path(os.environ.get(OUTPUT_ENV, str(DEFAULT_OUTPUT))), "Promotion output"
)

timestamp = datetime.datetime.now(datetime.timezone.utc)
result = {
    "schema_version": 1,
    "generated_utc": timestamp.isoformat(),
    "operation": "promote_validated_dungeon_sizes",
    "asset_path": ASSET_PATH,
    "class_path": CLASS_PATH,
    "matrix_receipt": {
        "path": str(receipt_path),
        "sha256": sha256_file(receipt_path),
    },
    "asset_mutations": [],
    "asset_saves": [],
    "calysto_asset_mutations": [],
    "rollback": {"attempted": False, "succeeded": None},
    "status": "FAIL",
}

asset = None
original_sizes: list[int] = []
initial_dirty = False
property_changed = False
save_completed = False
initial_content = None

try:
    contract = extract_matrix_contract(receipt_path)
    result["certificate"] = {
        key: value for key, value in contract.items() if key != "document"
    }
    if not ASSET_FILE.is_file():
        fail(f"The V3 policy package is missing: {ASSET_FILE}")
    if unreal.EditorLevelLibrary.get_game_world() is not None:
        fail("Policy promotion is forbidden while PIE or Simulate is running")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    policy_class = unreal.load_class(None, CLASS_PATH)
    asset = unreal.load_asset(ASSET_PATH)
    if policy_class is None or asset is None or asset.get_class() != policy_class:
        fail("The sole authored asset is missing or uses the wrong native V3 class")
    initial_dirty = is_asset_dirty(asset)
    if initial_dirty:
        fail("The V3 policy is already dirty; refusing to promote an ambiguous state")

    # The authored source is deliberately fail-closed while its certified
    # allowlist is empty, so GetPolicyHash may correctly return no runtime hash.
    # Its bytes, identity, source list, and transient candidate hash remain the
    # authoritative pre-promotion contract.
    before = policy_document(asset, allow_invalid_policy_hash=True)
    result["before"] = before
    original_sizes = [
        int(value) for value in list(editor_property(asset, "ValidatedDungeonSizes"))
    ]
    if before["uasset_sha256"] != contract["source_uasset_sha256"]:
        fail("The current policy bytes differ from the matrix source SHA-256")
    if sorted(original_sizes) != contract["source_validated_dungeon_sizes"]:
        fail("The current allowlist differs from the matrix source allowlist")
    if (
        before["schema_version"] != contract["schema_version"]
        or before["generator_version"] != contract["generator_version"]
        or before["policy_id"] != contract["policy_id"]
    ):
        fail("The current policy identity differs from the matrix receipt")

    candidate_hash_method = getattr(
        asset, "get_policy_hash_with_validated_dungeon_sizes", None
    )
    if not callable(candidate_hash_method):
        fail("The compiled V3 policy lacks transient candidate hashing")
    reproduced_hash = str(
        candidate_hash_method(contract["candidate_sizes"])
    ).upper()
    if reproduced_hash != contract["candidate_policy_hash"]:
        fail("The exact post-promotion PolicyHash does not reproduce from the current asset")
    result["candidate"] = {
        "validated_dungeon_sizes": contract["candidate_sizes"],
        "policy_hash": reproduced_hash,
    }

    initial_content = content_snapshot()
    BACKUP_ROOT.mkdir(parents=True, exist_ok=True)
    backup_name = "{}_{}.uasset".format(
        timestamp.strftime("%Y%m%dT%H%M%SZ"), before["uasset_sha256"]
    )
    backup_path = BACKUP_ROOT / backup_name
    if backup_path.exists():
        fail(f"Refusing to overwrite an existing policy backup: {backup_path}")
    shutil.copy2(ASSET_FILE, backup_path)
    if sha256_file(backup_path) != before["uasset_sha256"]:
        fail("The byte-exact policy backup failed SHA-256 verification")
    result["backup"] = {
        "path": str(backup_path),
        "sha256": sha256_file(backup_path),
        "length": backup_path.stat().st_size,
    }

    asset.set_editor_property(
        "validated_dungeon_sizes", contract["candidate_sizes"]
    )
    property_changed = True
    validate = getattr(asset, "validate_policy", None)
    if not callable(validate) or validate() is not True:
        fail("The candidate asset failed native V3 validation before save")
    if str(asset.get_policy_hash()).upper() != reproduced_hash:
        fail("The candidate asset hash changed while applying the certified list")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        fail("Unreal could not save the promoted V3 policy")
    save_completed = True

    after = policy_document(asset)
    result["after"] = after
    if after["validated_dungeon_sizes"] != contract["candidate_sizes"]:
        fail("The reloaded promoted allowlist differs from the certificate")
    if after["policy_hash"] != reproduced_hash or after["dirty"]:
        fail("The promoted policy hash/dirty-state postcondition failed")

    final_content = content_snapshot()
    changed_files = changed_content_paths(initial_content, final_content)
    mutations = [package_from_relative(path) for path in changed_files]
    if mutations != [ASSET_PATH]:
        fail(
            "Promotion Content delta must contain exactly the V3 policy: "
            f"actual={mutations}"
        )
    result["asset_mutations"] = mutations
    result["asset_saves"] = [ASSET_PATH]
    result["calysto_asset_mutations"] = [
        package for package in mutations if package.startswith("/Game/Calysto/")
    ]
    if result["calysto_asset_mutations"]:
        fail("Promotion touched a protected Calysto package")
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    if asset is not None and property_changed:
        result["rollback"]["attempted"] = True
        try:
            asset.set_editor_property("validated_dungeon_sizes", original_sizes)
            if save_completed:
                rollback_saved = unreal.EditorAssetLibrary.save_loaded_asset(
                    asset, only_if_is_dirty=False
                )
                result["rollback"]["mode"] = "logical_editor_save"
                rollback_sha = (
                    sha256_file(ASSET_FILE) if ASSET_FILE.is_file() else None
                )
                result["rollback"]["uasset_sha256"] = rollback_sha
                result["rollback"]["succeeded"] = bool(
                    rollback_saved
                    and rollback_sha == result.get("before", {}).get("uasset_sha256")
                    and sorted(
                        int(value)
                        for value in list(
                            editor_property(asset, "ValidatedDungeonSizes")
                        )
                    ) == sorted(original_sizes)
                )
            else:
                package = asset.get_outermost()
                set_dirty = getattr(package, "set_dirty_flag", None)
                if callable(set_dirty):
                    set_dirty(initial_dirty)
                result["rollback"]["mode"] = "unsaved_property_restore"
                result["rollback"]["succeeded"] = not is_asset_dirty(asset)
        except Exception as rollback_exc:
            result["rollback"]["succeeded"] = False
            result["rollback"]["error"] = str(rollback_exc)
    try:
        if asset is not None:
            result["after_failure"] = policy_document(
                asset, allow_invalid_policy_hash=True
            )
        if initial_content is not None:
            failure_content = content_snapshot()
            failure_files = changed_content_paths(initial_content, failure_content)
            failure_mutations = [
                package_from_relative(path) for path in failure_files
            ]
            result["asset_mutations"] = failure_mutations
            result["calysto_asset_mutations"] = [
                package
                for package in failure_mutations
                if package.startswith("/Game/Calysto/")
            ]
        if save_completed or result["rollback"].get("mode") == "logical_editor_save":
            result["asset_saves"] = [ASSET_PATH]
    except Exception as evidence_exc:
        result["failure_evidence_error"] = str(evidence_exc)
finally:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log(
        "CALYSTO_POLICY_SIZE_PROMOTION_RESULT=" + json.dumps(result, sort_keys=True)
    )

auto_close_editor = os.environ.get(
    "CODEX_CALYSTO_POLICY_PROMOTION_AUTOCLOSE", ""
).strip() == "1"
if auto_close_editor:
    if result["status"] != "PASS":
        unreal.log_error(
            result.get("error", "Unknown V3 policy promotion failure")
        )
    unreal.SystemLibrary.quit_editor()
elif result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Unknown V3 policy promotion failure"))
