"""Repair the missing montage on the UE 5.8 Mage area-damage ability.

This guarded, idempotent commandlet edits exactly one instanced ability inside
AS_ACF_Mage.  It does not modify the ACF Marketplace plugin, the Mage
AnimBlueprint, character meshes, AI, ticks, or spell gameplay configuration.

Required process environment:
  CODEX_MAGE_MONTAGE58_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


ABILITY_SET_PATH = (
    "/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_Mage"
)
EXPECTED_ACTION_OBJECT_PATH = (
    ABILITY_SET_PATH
    + ".AS_ACF_Mage:ACFAreaDamageSpellBP_C_0"
)
EXPECTED_ACTION_CLASS_PATH = (
    "/AscentCombatFramework/Blueprints/Actions/"
    "ACFAreaDamageSpellBP.ACFAreaDamageSpellBP_C"
)
MONTAGE_PATH = (
    "/Game/FullSample/Animations/MageAnims/AreaBoost_AM"
)
EVIDENCE_VALUE = os.environ.get(
    "CODEX_MAGE_MONTAGE58_EVIDENCE", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_MAGE_MONTAGE58_FAIL: " + message)
    raise RuntimeError(message)


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def snapshot(path):
    stat = os.stat(path)
    return {
        "file": os.path.realpath(path),
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")

evidence_path = os.path.realpath(EVIDENCE_VALUE)
saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
try:
    evidence_is_scoped = (
        os.path.commonpath([evidence_path, saved_migration_root]).lower()
        == saved_migration_root.lower()
    )
except ValueError:
    evidence_is_scoped = False
if not EVIDENCE_VALUE or not evidence_is_scoped:
    fail(
        "CODEX_MAGE_MONTAGE58_EVIDENCE must be below target Saved/Migration"
    )

asset_file = os.path.realpath(
    os.path.join(
        unreal.Paths.project_content_dir(),
        "FullSample",
        "Blueprints",
        "Actions",
        "AbilitySets",
        "AS_ACF_Mage.uasset",
    )
)
if not os.path.isfile(asset_file):
    fail("Target AS_ACF_Mage.uasset is absent")

ability_set = unreal.EditorAssetLibrary.load_asset(ABILITY_SET_PATH)
montage = unreal.EditorAssetLibrary.load_asset(MONTAGE_PATH)
if ability_set is None:
    fail("AS_ACF_Mage could not load")
if object_path(ability_set.get_class()) != "/Script/ActionsSystem.ACFAbilitySet":
    fail("AS_ACF_Mage has an unexpected native class")
if montage is None:
    fail("AreaBoost_AM could not load")
if object_path(montage.get_class()) != "/Script/Engine.AnimMontage":
    fail("AreaBoost_AM is not an AnimMontage")

matching_actions = []
for descriptor in ability_set.get_editor_property("action_abilities"):
    action = descriptor.get_editor_property("action")
    if action is None:
        continue
    if object_path(action.get_class()) == EXPECTED_ACTION_CLASS_PATH:
        matching_actions.append(action)
if len(matching_actions) != 1:
    fail(
        "Expected exactly one ACFAreaDamageSpellBP action, found "
        + str(len(matching_actions))
    )

action = matching_actions[0]
if object_path(action) != EXPECTED_ACTION_OBJECT_PATH:
    fail("Unexpected area-damage action object: " + object_path(action))

before = snapshot(asset_file)
before_montage = object_path(action.get_editor_property("anim_montage"))
if before_montage not in ("", MONTAGE_PATH + ".AreaBoost_AM"):
    fail("Refusing to overwrite unexpected montage: " + before_montage)

changed = before_montage == ""
if changed:
    action.modify(True)
    ability_set.modify(True)
    action.set_editor_property("anim_montage", montage)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        ability_set, only_if_is_dirty=False
    ):
        fail("EditorAssetLibrary failed to save AS_ACF_Mage")

after_montage = object_path(action.get_editor_property("anim_montage"))
if after_montage != MONTAGE_PATH + ".AreaBoost_AM":
    fail("Post-save montage verification failed: " + after_montage)

after = snapshot(asset_file)
if changed and before["sha256"] == after["sha256"]:
    fail("The requested repair did not change the asset file")

payload = {
    "schema_version": 1,
    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc)
    .replace(microsecond=0)
    .isoformat()
    .replace("+00:00", "Z"),
    "project": project_file,
    "engine_version": engine_version,
    "status": "PASS",
    "changed": changed,
    "scope": {
        "ability_set": ABILITY_SET_PATH,
        "action_object": EXPECTED_ACTION_OBJECT_PATH,
        "action_class": EXPECTED_ACTION_CLASS_PATH,
        "property": "AnimMontage",
        "before": before_montage or None,
        "after": after_montage,
    },
    "asset_before": before,
    "asset_after": after,
    "safety": {
        "marketplace_plugin_modified": False,
        "anim_blueprint_modified": False,
        "character_blueprints_modified": False,
        "ai_or_tick_behavior_modified": False,
        "spell_config_modified": False,
    },
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, ensure_ascii=False)

unreal.log(
    "CODEX_MAGE_MONTAGE58_PASS changed={} action={} montage={} evidence={}".format(
        changed,
        EXPECTED_ACTION_OBJECT_PATH,
        after_montage,
        evidence_path,
    )
)
