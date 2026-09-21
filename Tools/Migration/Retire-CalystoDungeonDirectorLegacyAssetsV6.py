"""Audit or retire the exact seven legacy Calysto assets through Unreal.

Default behavior is read-only audit. Deletion requires all four process guards:

  CODEX_APPLY_CALYSTO_V6_LEGACY_RETIREMENT=1
  CODEX_EXPECTED_CALYSTO_V6_GAMEPLAY_HASH=<64 uppercase/lowercase hex>
  CODEX_CALYSTO_V6_ACCEPTANCE_RECEIPT=<absolute JSON below Saved/Migration>
  CODEX_CALYSTO_V6_RETIREMENT_EVIDENCE=<new absolute JSON below Saved/Migration>

The tool never uses raw filesystem deletion. It removes only the four policy
assets first, re-audits referencers, then removes the three V4-named runtime
Blueprints. Any partial operation is reported as FAIL_PARTIAL with the exact
completed and remaining package sets.
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


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_MIGRATION = (Path(unreal.Paths.project_saved_dir()) / "Migration").resolve()

V6_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
V6_OBJECT = f"{V6_PACKAGE}.DA_CalystoDungeonDirectorPolicy"
V6_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"

LEGACY_POLICIES = (
    "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy",
    "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy",
    "/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy",
    "/Game/_Game/Data/CalystoDungeon/V5/DT_CalystoDungeonDirectorPolicy",
)
LEGACY_BLUEPRINTS = (
    "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4",
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4",
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4",
)
ALL_TARGETS = LEGACY_POLICIES + LEGACY_BLUEPRINTS
EXPECTED_HASHES = {
    LEGACY_POLICIES[0]: "9824B1EFC3EF8B24D5C33DDF0813B0EC999B3C9F5331BDB8E48D771120868D3A",
    LEGACY_POLICIES[1]: "C6DDB0A100012108F170BA8F566E17D1EFF60A8FAF3C724904FE091B028739A1",
    LEGACY_POLICIES[2]: "CAA73BAD16A8562ED03AA8EDD2E66335F05778AE467DF73BAE9C40B5873551F3",
    LEGACY_POLICIES[3]: "5A5106980447387FE1B1F2F5648063361F8FA41B5B7D0E51C3D8816BE76DB4B5",
    LEGACY_BLUEPRINTS[0]: "3D7FA02BC0B3A11315E6CB130A479FA6099B792D79DE288B374CF62684853117",
    LEGACY_BLUEPRINTS[1]: "45D439633454B551979F594A7ADC00D399520598541163F425BD01B4B1078205",
    LEGACY_BLUEPRINTS[2]: "DC92929C9D5FA12B6672BBCB2168BBE360964D1D8A860806CF066A19D9F23DFD",
}
UNVERSIONED = (
    "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
)
LEGACY_DIRECTORIES = (
    "/Game/_Game/Data/CalystoDungeon/V3",
    "/Game/_Game/Data/CalystoDungeon/V4",
    "/Game/_Game/Data/CalystoDungeon/V5",
)
PACKAGE_SUFFIXES = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def package_file(package: str) -> Path:
    return CONTENT_ROOT / Path(package[len("/Game/") :] + ".uasset")


def snapshot_content() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for path in CONTENT_ROOT.rglob("*"):
        if not path.is_file() or path.suffix.casefold() not in PACKAGE_SUFFIXES:
            continue
        stat = path.stat()
        result[path.relative_to(CONTENT_ROOT).as_posix()] = (
            int(stat.st_size), int(stat.st_mtime_ns), sha256(path)
        )
    return result


def dirty_packages() -> list[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({str(value.get_path_name()) for value in values})


def options() -> Any:
    value = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
        "include_searchable_names",
    ):
        try:
            value.set_editor_property(name, True)
        except Exception:
            if name != "include_searchable_names":
                raise
    return value


def referencers(registry: Any, package: str) -> list[str]:
    registry_values = registry.get_referencers(package, options()) or []
    editor_values = unreal.EditorAssetLibrary.find_package_referencers_for_asset(
        package, True
    ) or []
    return sorted({str(value) for value in list(registry_values) + list(editor_values)})


def inventory_directory(registry: Any, directory: str) -> list[str]:
    rows = registry.get_assets_by_path(
        directory, recursive=True, include_only_on_disk_assets=False
    )
    return sorted({str(row.package_name) for row in rows})


def validate_v6(expected_hash: str | None) -> dict[str, Any]:
    asset = unreal.load_asset(V6_PACKAGE)
    if asset is None or str(asset.get_path_name()) != V6_OBJECT:
        fail(f"Definitive V6 policy is missing: {V6_OBJECT}")
    if str(asset.get_class().get_path_name()) != V6_CLASS:
        fail(f"Definitive V6 class mismatch: {asset.get_class().get_path_name()}")
    validator = getattr(asset, "validate_policy", None)
    if not callable(validator):
        fail("V6 policy does not expose validate_policy()")
    validation = validator()
    valid = (
        bool(validation[0]) if isinstance(validation, tuple) and validation
        else not validation if isinstance(validation, str)
        else True if validation is None
        else bool(validation)
    )
    if not valid:
        fail(f"Native V6 policy validation failed: {validation!r}")
    getter = getattr(asset, "get_gameplay_hash", None)
    gameplay_hash = str(getter()).upper() if callable(getter) else ""
    if not re.fullmatch(r"[0-9A-F]{64}", gameplay_hash):
        fail("V6 policy returned an invalid gameplay hash")
    if expected_hash and gameplay_hash != expected_hash:
        fail(
            "V6 gameplay hash differs from the explicit retirement token: "
            f"actual={gameplay_hash} expected={expected_hash}"
        )
    return {
        "object": V6_OBJECT,
        "class": V6_CLASS,
        "gameplay_hash": gameplay_hash,
        "package_sha256": sha256(package_file(V6_PACKAGE)),
    }


def validate_acceptance(path: Path, expected_hash: str) -> dict[str, Any]:
    if not path.is_file():
        fail(f"V6 acceptance receipt is missing: {path}")
    try:
        path.relative_to(SAVED_MIGRATION)
    except ValueError:
        fail(f"Acceptance receipt must be below Saved/Migration: {path}")
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if str(data.get("status", "")).upper() not in {"PASS", "COMPLETE"}:
        fail("V6 acceptance receipt is not PASS/COMPLETE")
    receipt_hash = str(
        data.get("v6_gameplay_hash")
        or data.get("v6_policy", {}).get("gameplay_hash")
        or data.get("v6_policy", {}).get("hashes", {}).get("gameplay")
        or ""
    ).upper()
    if receipt_hash != expected_hash:
        fail(
            "Acceptance receipt gameplay hash mismatch: "
            f"actual={receipt_hash} expected={expected_hash}"
        )
    gates = data.get("required_gates")
    if not isinstance(gates, dict) or not gates:
        fail("Acceptance receipt has no required_gates object")
    failed_gates = []
    for name, value in gates.items():
        status = value.get("status") if isinstance(value, dict) else value
        if str(status).upper() != "PASS":
            failed_gates.append(f"{name}={status}")
    if failed_gates:
        fail(f"Acceptance receipt has incomplete gates: {failed_gates}")

    protected = data.get("protected_assets")
    if not isinstance(protected, dict) or not protected:
        fail("Acceptance receipt has no protected_assets hash map")
    verified: dict[str, str] = {}
    for path_text, value in protected.items():
        expected = str(value.get("sha256") if isinstance(value, dict) else value).upper()
        candidate = Path(path_text)
        if not candidate.is_absolute():
            candidate = PROJECT_ROOT / candidate
        candidate = candidate.resolve()
        try:
            candidate.relative_to(PROJECT_ROOT)
        except ValueError:
            fail(f"Protected invariant escapes project root: {candidate}")
        if not candidate.is_file() or not re.fullmatch(r"[0-9A-F]{64}", expected):
            fail(f"Invalid protected invariant entry: {candidate}")
        actual = sha256(candidate)
        if actual != expected:
            fail(
                f"Protected invariant drift: {candidate} "
                f"actual={actual} expected={expected}"
            )
        verified[str(candidate)] = actual
    return {
        "path": str(path),
        "sha256": sha256(path),
        "required_gates": gates,
        "protected_assets": verified,
    }


def validate_new_blueprints(registry: Any) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for package in UNVERSIONED:
        if not unreal.EditorAssetLibrary.does_asset_exist(package):
            fail(f"Definitive unversioned Blueprint is missing: {package}")
        refs = referencers(registry, package)
        if V6_PACKAGE not in refs:
            fail(f"V6 policy does not reference definitive Blueprint {package}: {refs}")
        result[package] = refs
    return result


def inspect_targets(registry: Any, require_present: bool) -> dict[str, Any]:
    dirty = set(dirty_packages())
    rows: dict[str, Any] = {}
    allowed = set(ALL_TARGETS)
    for package in ALL_TARGETS:
        path = package_file(package)
        exists = unreal.EditorAssetLibrary.does_asset_exist(package) or path.is_file()
        refs = referencers(registry, package) if exists else []
        row = {
            "exists": exists,
            "file": str(path),
            "dirty": package in dirty,
            "referencers": refs,
            "unexpected_referencers": sorted(set(refs) - allowed),
        }
        if path.is_file():
            row["sha256"] = sha256(path)
            row["baseline_hash_matches"] = row["sha256"] == EXPECTED_HASHES[package]
        rows[package] = row
        if require_present:
            if not exists:
                fail(f"Retirement target is missing before execution: {package}")
            if row["dirty"]:
                fail(f"Retirement target is dirty: {package}")
            if not row.get("baseline_hash_matches"):
                fail(f"Retirement target drifted from baseline: {package}")
            if row["unexpected_referencers"]:
                fail(
                    f"Retirement target has an out-of-set referencer: "
                    f"{package} -> {row['unexpected_referencers']}"
                )
            if V6_PACKAGE in refs:
                fail(f"V6 illegally references a legacy target: {package}")
    return rows


def redirectors(registry: Any, directories: tuple[str, ...]) -> list[str]:
    values: list[str] = []
    for directory in directories:
        for row in registry.get_assets_by_path(
            directory, recursive=True, include_only_on_disk_assets=False
        ):
            if str(row.asset_class_path).endswith("ObjectRedirector"):
                values.append(str(row.package_name))
    return sorted(set(values))


if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
    fail("Calysto V6 retirement requires UE 5.8")
if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
    fail(f"Wrong project: {PROJECT_FILE}")
if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
    fail(f"Wrong writable target: {PROJECT_ROOT}")

apply = os.environ.get("CODEX_APPLY_CALYSTO_V6_LEGACY_RETIREMENT", "") == "1"
expected_hash = os.environ.get("CODEX_EXPECTED_CALYSTO_V6_GAMEPLAY_HASH", "").strip().upper()
acceptance_value = os.environ.get("CODEX_CALYSTO_V6_ACCEPTANCE_RECEIPT", "").strip()
evidence_value = os.environ.get("CODEX_CALYSTO_V6_RETIREMENT_EVIDENCE", "").strip()
default_name = "LegacyRetirementApply.json" if apply else "LegacyRetirementAudit.json"
evidence = Path(evidence_value).resolve() if evidence_value else (
    SAVED_MIGRATION / "CalystoDungeonDirectorV6" / default_name
)
try:
    evidence.relative_to(SAVED_MIGRATION)
except ValueError:
    fail(f"Retirement evidence must remain below Saved/Migration: {evidence}")
if apply and evidence.exists():
    fail(f"Refusing to overwrite retirement evidence: {evidence}")
if apply and not re.fullmatch(r"[0-9A-F]{64}", expected_hash):
    fail("Apply mode requires CODEX_EXPECTED_CALYSTO_V6_GAMEPLAY_HASH")
if apply and not acceptance_value:
    fail("Apply mode requires CODEX_CALYSTO_V6_ACCEPTANCE_RECEIPT")

before = snapshot_content()
deleted: list[str] = []
result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "mode": "APPLY" if apply else "AUDIT_ONLY",
    "status": "FAIL",
    "targets": list(ALL_TARGETS),
    "deleted": [],
    "remaining": list(ALL_TARGETS),
}

try:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    if dirty_packages():
        fail(f"Editor has dirty packages: {dirty_packages()}")
    result["v6_before"] = validate_v6(expected_hash if apply else None)
    result["definitive_blueprint_referencers"] = validate_new_blueprints(registry)
    result["targets_before"] = inspect_targets(registry, require_present=apply)
    initial_redirectors = redirectors(
        registry,
        LEGACY_DIRECTORIES + (
            "/Game/_Game/Items/Chests",
            "/Game/_Game/Items/Clothing",
        ),
    )
    if initial_redirectors:
        fail(f"Pre-existing redirectors block retirement: {initial_redirectors}")
    result["redirectors_before"] = initial_redirectors

    if not apply:
        result["status"] = "AUDIT_PASS_NO_DELETION"
    else:
        result["acceptance"] = validate_acceptance(Path(acceptance_value).resolve(), expected_hash)
        expected_directory_inventory = {
            LEGACY_DIRECTORIES[0]: [LEGACY_POLICIES[0]],
            LEGACY_DIRECTORIES[1]: [LEGACY_POLICIES[1]],
            LEGACY_DIRECTORIES[2]: sorted(LEGACY_POLICIES[2:]),
        }
        result["legacy_directory_inventory_before"] = {}
        for directory, expected in expected_directory_inventory.items():
            actual = inventory_directory(registry, directory)
            result["legacy_directory_inventory_before"][directory] = actual
            if actual != expected:
                fail(
                    f"Legacy directory inventory is not exact for {directory}: "
                    f"actual={actual} expected={expected}"
                )

        for package in LEGACY_POLICIES:
            if not unreal.EditorAssetLibrary.delete_asset(package):
                fail(f"Editor asset deletion failed: {package}")
            deleted.append(package)

        registry.search_all_assets(True)
        registry.wait_for_completion()
        for package in LEGACY_BLUEPRINTS:
            refs = referencers(registry, package)
            if refs:
                fail(f"Legacy Blueprint still has referencers after policy removal: {package} -> {refs}")
        for package in LEGACY_BLUEPRINTS:
            if not unreal.EditorAssetLibrary.delete_asset(package):
                fail(f"Editor Blueprint deletion failed: {package}")
            deleted.append(package)

        registry.search_all_assets(True)
        registry.wait_for_completion()
        for directory in LEGACY_DIRECTORIES:
            if inventory_directory(registry, directory):
                fail(f"Legacy policy directory is not empty: {directory}")
            if unreal.EditorAssetLibrary.does_directory_exist(directory):
                if not unreal.EditorAssetLibrary.delete_directory(directory):
                    fail(f"Could not remove empty legacy directory through Unreal: {directory}")

        registry.search_all_assets(True)
        registry.wait_for_completion()
        remaining = [
            package for package in ALL_TARGETS
            if unreal.EditorAssetLibrary.does_asset_exist(package) or package_file(package).exists()
        ]
        if remaining:
            fail(f"Legacy targets remain after retirement: {remaining}")
        final_redirectors = redirectors(
            registry,
            (
                "/Game/_Game/Data/CalystoDungeon",
                "/Game/_Game/Items/Chests",
                "/Game/_Game/Items/Clothing",
            ),
        )
        if final_redirectors:
            fail(f"Retirement created redirectors: {final_redirectors}")
        result["redirectors_after"] = final_redirectors
        result["v6_after"] = validate_v6(expected_hash)
        if result["v6_after"] != result["v6_before"]:
            fail("V6 policy changed during legacy retirement")
        if dirty_packages():
            fail(f"Retirement left dirty packages: {dirty_packages()}")
        result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    if deleted:
        result["status"] = "FAIL_PARTIAL"
finally:
    result["deleted"] = list(deleted)
    result["remaining"] = [package for package in ALL_TARGETS if package not in deleted]
    after = snapshot_content()
    added = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    modified = sorted(path for path in set(before) & set(after) if before[path] != after[path])
    result["content_delta"] = {"added": added, "removed": removed, "modified": modified}
    if apply and result["status"] == "PASS":
        removed_packages = sorted({"/Game/" + path.rsplit(".", 1)[0] for path in removed})
        if added or modified or removed_packages != sorted(ALL_TARGETS):
            result["status"] = "FAIL_PARTIAL"
            result["error"] = (
                "Final Content delta is outside the exact retirement set: "
                f"added={added} modified={modified} removed_packages={removed_packages}"
            )
    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V6_LEGACY_RETIREMENT_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] not in {"PASS", "AUDIT_PASS_NO_DELETION"}:
    raise RuntimeError(f"{result['status']}: {result.get('error', 'unknown failure')}")
