"""Read-only post-migration validation in the isolated UE 5.7 harness."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGES = (
    "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses",
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry",
    "/Game/Data/CharacterBackground/DT_ProjectBackstories",
    "/Game/Data/CharacterBackground/DT_ProjectProfessions",
    "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget",
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget",
    "/Game/_Game/Icons/Bleeding",
    "/Game/_Game/Icons/Dirt",
    "/Game/_Game/Icons/Dizzy",
    "/Game/_Game/Icons/Exhausted",
    "/Game/_Game/Icons/extremepain_transparent",
    "/Game/_Game/Icons/Fear",
    "/Game/_Game/Icons/frenzy_transparent",
    "/Game/_Game/Icons/gracestep_transparent",
    "/Game/_Game/Icons/Hungry",
    "/Game/_Game/Icons/knockedout_transparent",
    "/Game/_Game/Icons/Orgasm",
    "/Game/_Game/Icons/SleepDeprived",
    "/Game/_Game/Icons/Thirst",
)

TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
HARNESS_CONTENT = os.environ.get("CODEX_EXPECTED_HARNESS_CONTENT", "").strip()
MIGRATION_EVIDENCE = os.environ.get(
    "CODEX_EFCORE_MIGRATION_EVIDENCE", ""
).strip()
OUTPUT_PATH = os.environ.get("CODEX_EFCORE_VALIDATION_OUTPUT", "").strip()


def fail(message):
    unreal.log_error("CODEX_EFCORE_CONTENT57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all((TARGET_ROOT, HARNESS_CONTENT, MIGRATION_EVIDENCE, OUTPUT_PATH)):
    fail("All validation environment variables are required")

target_root = os.path.realpath(TARGET_ROOT)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(HARNESS_CONTENT).lower():
    fail("The commandlet is not running inside the audited harness")
if not os.path.isfile(MIGRATION_EVIDENCE):
    fail("Migration evidence is absent: " + MIGRATION_EVIDENCE)

with open(MIGRATION_EVIDENCE, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_BATCH_MIGRATION_PASS":
    fail("Prior AssetTools migration did not report PASS")
if migration.get("created_package_count") != 19:
    fail("Prior AssetTools migration did not create exactly 19 packages")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()


def asset_class_path(asset_data):
    # UE 5.7 exposes AssetClassPath; consulting the deprecated AssetClass
    # property, even as an eagerly evaluated getattr fallback, emits a warning.
    value = getattr(asset_data, "asset_class_path", None)
    return str(value) if value is not None else ""


registry_rows = []
for package_name in EXPECTED_PACKAGES:
    assets = registry.get_assets_by_package_name(
        package_name, include_only_on_disk_assets=True
    )
    if not assets:
        fail("Staged package is not registered: " + package_name)
    registry_rows.append(
        {
            "package": package_name,
            "asset_count": len(assets),
            "classes": sorted(
                {
                    asset_class_path(item)
                    for item in assets
                }
            ),
        }
    )

target_rows = []
evidence_packages = {
    row["package"]: row for row in migration.get("packages", [])
}
if set(evidence_packages) != set(EXPECTED_PACKAGES):
    fail("Prior evidence package set differs from the exact allowlist")
for package_name in EXPECTED_PACKAGES:
    prior = evidence_packages[package_name]
    path = os.path.realpath(prior["file"])
    expected_content_root = os.path.realpath(
        os.path.join(target_root, "Content")
    )
    if not path.lower().startswith(expected_content_root.lower() + os.sep):
        fail("Migrated file escapes target Content: " + path)
    if not os.path.isfile(path):
        fail("Migrated target file is absent: " + path)
    current_hash = sha256(path)
    if current_hash != prior["sha256"]:
        fail("Migrated target hash changed before UE 5.8 resave: " + path)
    target_rows.append(
        {
            "package": package_name,
            "file": path,
            "length": os.path.getsize(path),
            "sha256": current_hash,
        }
    )

output_path = os.path.realpath(OUTPUT_PATH)
evidence_root = os.path.realpath(
    os.path.join(target_root, "Saved", "Migration")
)
if not output_path.lower().startswith(evidence_root.lower() + os.sep):
    fail("Validation output escapes target Saved/Migration")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(
        datetime.timezone.utc
    ).isoformat(),
    "status": "UE57_POST_MIGRATION_READ_ONLY_VALIDATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "registry_packages": registry_rows,
    "target_packages": target_rows,
    "package_count": len(target_rows),
    "target_package_loads": 0,
    "target_package_saves": 0,
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCORE_CONTENT57_VALIDATION_PASS: " + output_path)
