"""Restore an audited UE 5.7 package cohort into the UE 5.8 target.

This script runs only inside the detached UE 5.7 harness prepared by
Invoke-NativeWorldRestore.ps1.  The legacy AssetTools migration backend is
intentional: it preserves the original package bytes and references instead
of loading the assets in the dependency-minimal harness.
"""

import hashlib
import json
import os
import shutil
import traceback

import unreal


def real(path):
    return os.path.realpath(os.path.abspath(path))


def is_under(path, root):
    path = os.path.normcase(real(path))
    root = os.path.normcase(real(root))
    return path == root or path.startswith(root + os.sep)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def file_record(path, root):
    path = real(path)
    return {
        "relative": os.path.relpath(path, root).replace(os.sep, "/"),
        "length": os.path.getsize(path),
        "sha256": sha256(path),
    }


def fail(message, details=None):
    payload = {"status": "FAIL", "error": message}
    if details is not None:
        payload["details"] = details
    try:
        if RECEIPT:
            os.makedirs(os.path.dirname(RECEIPT), exist_ok=True)
            with open(RECEIPT, "w", encoding="utf-8") as handle:
                json.dump(payload, handle, indent=2, sort_keys=True)
    finally:
        raise RuntimeError(message)


MANIFEST = real(os.environ.get("CODEX_NATIVE_WORLD_MANIFEST", ""))
TARGET_CONTENT = real(os.environ.get("CODEX_NATIVE_WORLD_TARGET_CONTENT", ""))
BACKUP_DIR = real(os.environ.get("CODEX_NATIVE_WORLD_BACKUP_DIR", ""))
RECEIPT = real(os.environ.get("CODEX_NATIVE_WORLD_RECEIPT", ""))
HARNESS_CONTENT = real(unreal.Paths.project_content_dir())


try:
    if not os.path.isfile(MANIFEST):
        fail("Package manifest is absent: " + MANIFEST)
    if not os.path.isdir(TARGET_CONTENT):
        fail("Target Content directory is absent: " + TARGET_CONTENT)
    if is_under(TARGET_CONTENT, HARNESS_CONTENT) or is_under(HARNESS_CONTENT, TARGET_CONTENT):
        fail("Harness and target Content directories overlap")

    with open(MANIFEST, "r", encoding="utf-8-sig") as handle:
        manifest = json.load(handle)
    rows = manifest.get("packages", [])
    packages = [row["package_name"] for row in rows]
    if not packages or len(packages) != len(set(packages)):
        fail("Package manifest is empty or contains duplicate package names")

    forbidden_prefixes = (
        "/Game/FullSample/",
        "/Game/DazToUnreal/",
    )
    forbidden_exact = {
        "/Game/FullSample/Player",
    }
    violations = [
        package
        for package in packages
        if package in forbidden_exact or package.startswith(forbidden_prefixes)
    ]
    if violations:
        fail("Manifest touches protected target packages", violations)

    source_records = []
    target_before = []
    os.makedirs(BACKUP_DIR, exist_ok=False)
    for row in rows:
        for relative in row["relative_files"]:
            source = real(os.path.join(HARNESS_CONTENT, relative))
            target = real(os.path.join(TARGET_CONTENT, relative))
            if not is_under(source, HARNESS_CONTENT) or not is_under(target, TARGET_CONTENT):
                fail("Package file escaped an audited Content root: " + relative)
            if not os.path.isfile(source):
                fail("Staged package file is absent: " + source)
            source_record = file_record(source, HARNESS_CONTENT)
            expected = row["source_files"].get(relative)
            if expected is None:
                fail("Manifest source record is absent: " + relative)
            if (
                source_record["length"] != expected["length"]
                or source_record["sha256"] != expected["sha256"]
            ):
                fail("Staged file differs from the frozen source manifest: " + relative)
            source_records.append(source_record)

            if os.path.isfile(target):
                before = file_record(target, TARGET_CONTENT)
                target_before.append(before)
                backup = real(os.path.join(BACKUP_DIR, relative))
                if not is_under(backup, BACKUP_DIR):
                    fail("Backup path escaped backup root: " + relative)
                os.makedirs(os.path.dirname(backup), exist_ok=True)
                shutil.copy2(target, backup)
                if sha256(backup) != before["sha256"]:
                    fail("Target backup hash differs: " + relative)

    if not hasattr(unreal.AssetMigrationConflict, "OVERWRITE"):
        fail("UE 5.7 Python has no AssetMigrationConflict.OVERWRITE")

    options = unreal.MigrationOptions()
    options.set_editor_property("prompt", False)
    options.set_editor_property("ignore_dependencies", True)
    options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.OVERWRITE)
    options.set_editor_property("orphan_folder", "")

    unreal.SystemLibrary.execute_console_command(None, "AssetTools.UseNewPackageMigration 0")
    unreal.log(
        "CODEX_NATIVE_WORLD_RESTORE57_BEGIN: packages={}".format(len(packages))
    )
    unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
        packages, TARGET_CONTENT, options
    )

    target_after = []
    mismatches = []
    for row in rows:
        for relative in row["relative_files"]:
            source = real(os.path.join(HARNESS_CONTENT, relative))
            target = real(os.path.join(TARGET_CONTENT, relative))
            if not os.path.isfile(target):
                mismatches.append({"relative": relative, "reason": "target_absent"})
                continue
            source_record = file_record(source, HARNESS_CONTENT)
            after = file_record(target, TARGET_CONTENT)
            target_after.append(after)
            if (
                after["length"] != source_record["length"]
                or after["sha256"] != source_record["sha256"]
            ):
                mismatches.append(
                    {
                        "relative": relative,
                        "reason": "target_bytes_differ",
                        "source": source_record,
                        "target": after,
                    }
                )
    if mismatches:
        fail("AssetTools restore did not preserve the audited source bytes", mismatches)

    receipt = {
        "status": "PASS",
        "policy": "UE57_LEGACY_ASSETTOOLS_EXPLICIT_OVERWRITE",
        "package_count": len(packages),
        "package_names": packages,
        "source_files": source_records,
        "target_files_before": target_before,
        "target_files_after": target_after,
        "backup_dir": BACKUP_DIR,
        "source_project_unchanged_by_design": True,
    }
    os.makedirs(os.path.dirname(RECEIPT), exist_ok=True)
    with open(RECEIPT, "w", encoding="utf-8") as handle:
        json.dump(receipt, handle, indent=2, sort_keys=True)
    unreal.log("CODEX_NATIVE_WORLD_RESTORE57_PASS")
except Exception as exc:
    unreal.log_error("CODEX_NATIVE_WORLD_RESTORE57_FAIL: {}".format(exc))
    unreal.log_error(traceback.format_exc())
    if RECEIPT and not os.path.isfile(RECEIPT):
        os.makedirs(os.path.dirname(RECEIPT), exist_ok=True)
        with open(RECEIPT, "w", encoding="utf-8") as handle:
            json.dump(
                {"status": "FAIL", "error": str(exc), "traceback": traceback.format_exc()},
                handle,
                indent=2,
                sort_keys=True,
            )
    raise
