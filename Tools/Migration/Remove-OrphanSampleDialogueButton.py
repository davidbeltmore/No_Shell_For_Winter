"""Remove one verified zero-referencer FullSample orphan through Unreal AssetTools.

The operation is intentionally narrow and refuses to run unless its expected
hash, ownership contract, and zero-referencer preconditions all still hold.
"""

import datetime
import hashlib
import json
import os

import unreal


ASSET_PATH = "/Game/FullSample/Integrations/ATSIntegrations/Dialogue/SampleDialogueButton_WBP"
QUARANTINE_PATH = "/Game/_MigrationQuarantine/Phase1_20260713/SampleDialogueButton_WBP"
OLD_OWNER_PATH = "/Game/FullSample/Integrations/ATSIntegrations/Dialogue/ACF_Dialogue_WB"
CURRENT_PLAYER_PATH = "/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerBP"
CURRENT_DIALOGUE_PATH = "/AscentCombatFramework/Integrations/Widgets/ACF_MinimalDialogue_WB"
EXPECTED_SHA256 = "9A94F4E897501F9266029CF87EF27DCFC93EB2B257532BFB15D2A65C3150CF6F"
EXPECTED_QUARANTINE_SHA256 = "C1D70A9F13E2BE3AB62B676041C7F4AB808357108A5F35DBEA6876DC581F593C"

APPLY = os.environ.get("CODEX_APPLY_ORPHAN_WBP_CLEANUP", "") == "1"
OUTPUT_PATH = os.environ.get("CODEX_ORPHAN_WBP_EVIDENCE", "").strip()


def fail(message):
    unreal.log_error("CODEX_ORPHAN_WBP_CLEANUP_FAIL: " + message)
    raise RuntimeError(message)


def package_file(asset_path):
    relative = asset_path.removeprefix("/Game/").replace("/", os.sep) + ".uasset"
    return os.path.join(unreal.Paths.project_content_dir(), relative)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()

options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    try:
        options.set_editor_property(property_name, True)
    except Exception:
        pass


def referencers(package_name):
    try:
        values = registry.get_referencers(package_name, options)
    except TypeError:
        values = registry.get_referencers(package_name)
    return sorted({str(value) for value in (values or [])})


if not APPLY:
    fail("Apply guard is not set; define CODEX_APPLY_ORPHAN_WBP_CLEANUP=1")
if not OUTPUT_PATH:
    fail("CODEX_ORPHAN_WBP_EVIDENCE is required")
original_exists = unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH)
quarantine_exists = unreal.EditorAssetLibrary.does_asset_exist(QUARANTINE_PATH)
if original_exists and quarantine_exists:
    fail("Both original and quarantine assets exist; refusing ambiguous cleanup")
if not original_exists and not quarantine_exists:
    fail("Neither original nor quarantine asset exists; cleanup state is unverifiable")
if unreal.EditorAssetLibrary.does_asset_exist(OLD_OWNER_PATH):
    fail("Old dialogue owner unexpectedly exists: " + OLD_OWNER_PATH)
if not unreal.EditorAssetLibrary.does_asset_exist(CURRENT_PLAYER_PATH):
    fail("Current target player asset is missing: " + CURRENT_PLAYER_PATH)
if not unreal.EditorAssetLibrary.does_asset_exist(CURRENT_DIALOGUE_PATH):
    fail("Current ACFU dialogue asset is missing: " + CURRENT_DIALOGUE_PATH)
active_path = ASSET_PATH if original_exists else QUARANTINE_PATH
disk_path = package_file(active_path)
actual_hash = sha256(disk_path)
expected_active_hash = EXPECTED_SHA256 if original_exists else EXPECTED_QUARANTINE_SHA256
if actual_hash != expected_active_hash:
    fail("Asset hash changed: expected {}, got {}".format(expected_active_hash, actual_hash))

refs_before = referencers(active_path)
if refs_before:
    fail("Asset is no longer orphaned; referencers={}".format(refs_before))

resumed_from_quarantine = quarantine_exists
if original_exists:
    if not unreal.EditorAssetLibrary.make_directory(os.path.dirname(QUARANTINE_PATH)):
        if not unreal.EditorAssetLibrary.does_directory_exist(os.path.dirname(QUARANTINE_PATH)):
            fail("Could not create quarantine directory")

    if not unreal.EditorAssetLibrary.rename_asset(ASSET_PATH, QUARANTINE_PATH):
        fail("AssetTools quarantine rename failed")

refs_quarantined = referencers(QUARANTINE_PATH)
if refs_quarantined:
    fail("Quarantined asset unexpectedly acquired referencers: {}".format(refs_quarantined))

if not unreal.EditorAssetLibrary.delete_asset(QUARANTINE_PATH):
    fail("AssetTools deletion failed after quarantine")
if unreal.EditorAssetLibrary.does_asset_exist(QUARANTINE_PATH):
    fail("Asset still exists after AssetTools deletion")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "operation": "QUARANTINE_THEN_DELETE_VIA_UNREAL_EDITOR_ASSETTOOLS",
    "asset_path": ASSET_PATH,
    "quarantine_path": QUARANTINE_PATH,
    "original_sha256": EXPECTED_SHA256,
    "quarantine_sha256": actual_hash if resumed_from_quarantine else None,
    "referencers_before": refs_before,
    "referencers_in_quarantine": refs_quarantined,
    "resumed_from_quarantine": resumed_from_quarantine,
    "old_owner_present": False,
    "current_player_present": True,
    "current_acfu_dialogue_present": True,
    "result": "PASS",
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_ORPHAN_WBP_CLEANUP_PASS: " + json.dumps(payload, sort_keys=True))
