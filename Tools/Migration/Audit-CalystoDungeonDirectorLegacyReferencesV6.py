"""Read-only UE 5.8 audit for the definitive Calysto V6 cutover.

The script never saves, renames, duplicates, or deletes a Content asset. It
writes one JSON receipt under Saved/Migration. Set
CODEX_CALYSTO_V6_LEGACY_AUDIT_MODE=FINAL to make any remaining legacy asset,
legacy active-code reference, missing unversioned runtime Blueprint, or invalid
V6 authority a hard failure. The default TRANSITIONAL mode inventories drift
without pretending that the cutover is complete.
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
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()

V6_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
V6_OBJECT = f"{V6_PACKAGE}.DA_CalystoDungeonDirectorPolicy"
V6_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"

LEGACY_ASSETS = {
    "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy": {
        "class": "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy",
        "sha256": "9824B1EFC3EF8B24D5C33DDF0813B0EC999B3C9F5331BDB8E48D771120868D3A",
    },
    "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy": {
        "class": "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4",
        "sha256": "C6DDB0A100012108F170BA8F566E17D1EFF60A8FAF3C724904FE091B028739A1",
    },
    "/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy": {
        "class": "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV5Asset",
        "sha256": "CAA73BAD16A8562ED03AA8EDD2E66335F05778AE467DF73BAE9C40B5873551F3",
    },
    "/Game/_Game/Data/CalystoDungeon/V5/DT_CalystoDungeonDirectorPolicy": {
        "class": "/Script/Engine.DataTable",
        "sha256": "5A5106980447387FE1B1F2F5648063361F8FA41B5B7D0E51C3D8816BE76DB4B5",
    },
    "/Game/_Game/Items/Chests/BP_CalystoLockedChestV4": {
        "class": "/Script/Engine.Blueprint",
        "sha256": "3D7FA02BC0B3A11315E6CB130A479FA6099B792D79DE288B374CF62684853117",
    },
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4": {
        "class": "/Script/Engine.Blueprint",
        "sha256": "45D439633454B551979F594A7ADC00D399520598541163F425BD01B4B1078205",
    },
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4": {
        "class": "/Script/Engine.Blueprint",
        "sha256": "DC92929C9D5FA12B6672BBCB2168BBE360964D1D8A860806CF066A19D9F23DFD",
    },
}

UNVERSIONED_BLUEPRINTS = {
    "/Game/_Game/Items/Chests/BP_CalystoLockedChest": (
        "/Script/EFProjectSystemsGameplay.ProjectCalystoChest"
    ),
    "/Game/_Game/Items/Chests/BP_CalystoLockPickChest": (
        "/Script/EFProjectSystemsGameplay.ProjectCalystoChest"
    ),
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup": (
        "/Script/InventorySystem.ACFWorldItem"
    ),
}

TEXT_ROOTS = (
    PROJECT_ROOT / "Config",
    PROJECT_ROOT / "Plugins" / "EFProcedural" / "Source",
    PROJECT_ROOT / "Plugins" / "EFLevelFlow" / "Source",
    PROJECT_ROOT / "Plugins" / "EFProjectSystems" / "Source",
    PROJECT_ROOT / "Tools" / "Migration",
    PROJECT_ROOT / ".agents" / "skills" / "calysto-dungeon-master",
)
TEXT_EXTENSIONS = {
    ".h", ".hpp", ".cpp", ".cs", ".ini", ".py", ".ps1", ".md", ".yaml", ".yml"
}
LEGACY_PATTERNS = (
    re.compile(r"EFCalysto[^\r\n\"']*(?:V3|V4|V5)"),
    re.compile(r"CalystoDungeonDirector(?:Policy)?V(?:3|4|5)"),
    re.compile(r"CalystoDungeon/(?:V3|V4|V5)(?:/|\b)"),
    re.compile(r"BP_Calysto(?:LockedChest|LockPickChest|ArmorPickup)V4"),
    re.compile(r"NoShellForWinter\.CalystoDungeon\.V(?:3|4|5)"),
)

# These transition-only tools must name the assets they audit. Archive or
# remove them after the final receipt if a literal zero-token repository is
# desired. No runtime, Config, active skill, or production test is exempt.
TRANSITION_TOOL_EXEMPTIONS = {
    "Tools/Migration/Create-CalystoDungeonDirectorPolicyV6.py",
    "Tools/Migration/Create-CalystoDungeonDirectorRuntimeAssetsV6.py",
    "Tools/Migration/Audit-CalystoDungeonDirectorLegacyReferencesV6.py",
    "Tools/Migration/Retire-CalystoDungeonDirectorLegacyAssetsV6.py",
    "Tools/Migration/Run-CalystoDungeonDirectorLegacyRetirementV6.ps1",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def validate_context() -> None:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Calysto V6 legacy audit requires UE 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
        fail(f"Wrong project root: {PROJECT_ROOT}")


def package_file(package: str) -> Path:
    if not package.startswith("/Game/"):
        fail(f"Unsupported non-project package: {package}")
    return CONTENT_ROOT / Path(package[len("/Game/") :] + ".uasset")


def dirty_packages() -> set[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {str(value.get_path_name()) for value in values}


def dependency_options(enabled: set[str]) -> Any:
    result = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
        "include_searchable_names",
    ):
        try:
            result.set_editor_property(name, name in enabled)
        except Exception:
            if name != "include_searchable_names":
                raise
    return result


DEPENDENCY_KINDS = {
    "hard_package": {"include_hard_package_references"},
    "soft_package": {"include_soft_package_references"},
    "hard_management": {"include_hard_management_references"},
    "soft_management": {"include_soft_management_references"},
    "all": {
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
        "include_searchable_names",
    },
}


def registry_edges(registry: Any, package: str, direction: str) -> dict[str, list[str]]:
    method = registry.get_referencers if direction == "referencers" else registry.get_dependencies
    result: dict[str, list[str]] = {}
    for label, flags in DEPENDENCY_KINDS.items():
        values = method(package, dependency_options(flags)) or []
        result[label] = sorted({str(value) for value in values})
    return result


def asset_class_path(asset: Any) -> str:
    return "" if asset is None else str(asset.get_class().get_path_name())


def inspect_asset(registry: Any, package: str, spec: dict[str, str], dirty: set[str]) -> dict[str, Any]:
    path = package_file(package)
    asset = unreal.load_asset(package)
    exists = asset is not None or path.is_file()
    row: dict[str, Any] = {
        "package": package,
        "exists": exists,
        "file": str(path),
        "dirty": package in dirty,
        "expected_class": spec["class"],
        "actual_class": asset_class_path(asset),
        "expected_sha256": spec["sha256"],
        "referencers": registry_edges(registry, package, "referencers"),
        "dependencies": registry_edges(registry, package, "dependencies"),
    }
    try:
        row["editor_referencers"] = sorted(
            {
                str(value)
                for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                    package, True
                )
            }
        )
    except Exception as exc:
        row["editor_referencer_error"] = str(exc)
        row["editor_referencers"] = []
    if path.is_file():
        row["sha256"] = sha256(path)
        row["size"] = int(path.stat().st_size)
        row["baseline_hash_matches"] = row["sha256"] == spec["sha256"]
    else:
        row["sha256"] = ""
        row["baseline_hash_matches"] = False
    row["class_matches"] = not exists or row["actual_class"] == spec["class"]
    return row


def blueprint_status(package: str, expected_parent: str) -> dict[str, Any]:
    blueprint = unreal.load_asset(package)
    if blueprint is None:
        return {"package": package, "exists": False, "expected_parent": expected_parent}
    parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    parent_path = "" if parent is None else str(parent.get_path_name())
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(ch for ch in status.upper() if ch.isalnum())
    return {
        "package": package,
        "exists": True,
        "class": asset_class_path(blueprint),
        "expected_parent": expected_parent,
        "parent": parent_path,
        "parent_matches": parent_path == expected_parent,
        "compile_status": status,
        "compile_up_to_date": "UPTODATE" in normalized,
    }


def validate_v6() -> dict[str, Any]:
    asset = unreal.load_asset(V6_PACKAGE)
    if asset is None:
        return {"exists": False, "package": V6_PACKAGE, "status": "MISSING"}
    row: dict[str, Any] = {
        "exists": True,
        "package": V6_PACKAGE,
        "object": str(asset.get_path_name()),
        "class": asset_class_path(asset),
        "dirty": V6_PACKAGE in dirty_packages(),
        "status": "FAIL",
    }
    if row["object"] != V6_OBJECT or row["class"] != V6_CLASS:
        row["error"] = "V6 object/class mismatch"
        return row
    validator = getattr(asset, "validate_policy", None)
    if not callable(validator):
        row["error"] = "validate_policy() is unavailable"
        return row
    try:
        value = validator()
        if isinstance(value, tuple):
            valid = bool(value[0])
        elif isinstance(value, str):
            valid = not value
        elif value is None:
            valid = True
        else:
            valid = bool(value)
        if not valid:
            row["error"] = f"Native validation failed: {value!r}"
            return row
        hashes: dict[str, str] = {}
        for label, method_name in (
            ("gameplay", "get_gameplay_hash"),
            ("authoring", "get_authoring_hash"),
            ("materials", "get_material_hash"),
            ("decals", "get_decal_hash"),
        ):
            method = getattr(asset, method_name, None)
            value = str(method()).upper() if callable(method) else ""
            if not re.fullmatch(r"[0-9A-F]{64}", value):
                row["error"] = f"Invalid or missing {method_name}()"
                return row
            hashes[label] = value
        row["hashes"] = hashes
        row["status"] = "PASS"
    except Exception as exc:
        row["error"] = str(exc)
    return row


def scan_active_text() -> dict[str, Any]:
    findings: list[dict[str, Any]] = []
    scanned = 0
    for root in TEXT_ROOTS:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.casefold() not in TEXT_EXTENSIONS:
                continue
            relative = path.relative_to(PROJECT_ROOT).as_posix()
            if "/Archive/" in f"/{relative}/":
                continue
            scanned += 1
            try:
                lines = path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
            except Exception as exc:
                findings.append(
                    {"file": relative, "line": 0, "text": "", "error": str(exc), "exempt": False}
                )
                continue
            for line_number, line in enumerate(lines, 1):
                matches = sorted(
                    {match.group(0) for pattern in LEGACY_PATTERNS for match in pattern.finditer(line)}
                )
                if matches:
                    findings.append(
                        {
                            "file": relative,
                            "line": line_number,
                            "matches": matches,
                            "text": line.strip()[:500],
                            "exempt": relative in TRANSITION_TOOL_EXEMPTIONS,
                        }
                    )
    return {
        "scanned_file_count": scanned,
        "finding_count": len(findings),
        "non_exempt_finding_count": sum(not row["exempt"] for row in findings),
        "findings": findings,
    }


def content_stat_snapshot() -> dict[str, tuple[int, int]]:
    result: dict[str, tuple[int, int]] = {}
    for suffix in ("*.uasset", "*.umap", "*.uexp", "*.ubulk", "*.uptnl"):
        for path in CONTENT_ROOT.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size), int(stat.st_mtime_ns)
            )
    return result


validate_context()
mode = os.environ.get("CODEX_CALYSTO_V6_LEGACY_AUDIT_MODE", "TRANSITIONAL").strip().upper()
if mode not in {"TRANSITIONAL", "FINAL"}:
    fail(f"Unsupported audit mode: {mode}")
receipt_value = os.environ.get("CODEX_CALYSTO_V6_LEGACY_AUDIT_EVIDENCE", "").strip()
receipt = Path(receipt_value).resolve() if receipt_value else (
    SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" / "LegacyReferenceAudit.json"
)
try:
    receipt.relative_to(SAVED_ROOT / "Migration")
except ValueError:
    fail(f"Audit evidence must remain under Saved/Migration: {receipt}")

before = content_stat_snapshot()
result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "mode": mode,
    "status": "FAIL",
    "content_mutations": [],
}

try:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    dirty = dirty_packages()
    result["dirty_packages"] = sorted(dirty)
    result["legacy_assets"] = {
        package: inspect_asset(registry, package, spec, dirty)
        for package, spec in LEGACY_ASSETS.items()
    }
    result["v6_policy"] = validate_v6()
    result["unversioned_blueprints"] = {
        package: blueprint_status(package, parent)
        for package, parent in UNVERSIONED_BLUEPRINTS.items()
    }
    result["active_text_scan"] = scan_active_text()

    failures: list[str] = []
    if mode == "FINAL":
        remaining = [
            package for package, row in result["legacy_assets"].items() if row["exists"]
        ]
        if remaining:
            failures.append(f"Legacy assets remain: {remaining}")
        if result["v6_policy"].get("status") != "PASS":
            failures.append(f"V6 policy is not valid: {result['v6_policy']}")
        bad_blueprints = [
            package
            for package, row in result["unversioned_blueprints"].items()
            if not row.get("exists")
            or not row.get("parent_matches")
            or not row.get("compile_up_to_date")
        ]
        if bad_blueprints:
            failures.append(f"Unversioned runtime Blueprints are invalid: {bad_blueprints}")
        if result["active_text_scan"]["non_exempt_finding_count"]:
            failures.append(
                "Active source/config/tool/skill legacy references remain: "
                f"{result['active_text_scan']['non_exempt_finding_count']}"
            )
    result["failure_reasons"] = failures
    result["status"] = (
        "PASS_FINAL" if mode == "FINAL" and not failures
        else "PASS_TRANSITIONAL" if mode == "TRANSITIONAL"
        else "FAIL_FINAL"
    )
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
finally:
    after = content_stat_snapshot()
    result["content_mutations"] = sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )
    if result["content_mutations"]:
        result["status"] = "FAIL_AUDIT_MUTATED_CONTENT"
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V6_LEGACY_AUDIT_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"].startswith("FAIL"):
    raise RuntimeError(result.get("error") or "; ".join(result.get("failure_reasons", [])))
