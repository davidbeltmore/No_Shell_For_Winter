# Procedural chest controls on placed actor Details

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.3.
Implementation commit: `388feff3`. Source project remains read-only and untouched.

Select `BP_EquipmentDeposit` or `BP_EquipmentLockedChest` in the level Outliner.
The expanded **Procedural Chest** section exposes **Procedural Mode**, Chest
Rarity, Profile, Override Content, Content Override and Seed. Stable Identity
is advanced. Validate Configuration checks the existing component's settings.
Blueprint component editing remains available as before.

`EFProjectSystemsEditor/ProjectChestDetails.cpp` registers the customization for
`BP_EquipmentDeposit_C`; its descendants inherit it. Native external property
handles edit the actual `UProjectProceduralChestComponent` on each selected actor.
There are no runtime changes, duplicate variables, new inventories, Blueprint
graph edits, or changes to Marketplace/Engine code. The section is ordered at
the top of the category list for quick access. Undo/reset/multi-edit use Unreal's
property editor machinery; direct pointer-input testing is not claimed because
the Computer Use native pipe was unavailable.

The initial MCP read confirmed HUB, UE 5.8.3, stopped PIE and an unsaved HUB.
The original disk map and the user's saved map were backed up under
`Saved/EquipmentQA/ChestActorDetails/Before`. The saved user map SHA-256 is
`1F7DF72F77516F8F96B197AF1CD660106A7D145DAD6E6D047DCDEEB018FFC5C3`.
The HUB change remains outside the implementation commit. Temporary visual
checkbox changes were restored without saving. Existing untracked
`Content/_Game/Chest/` and `Docs/Migration/ChestMoveRollback.md` were preserved.

## Validation

- Editor build: PASS, `Saved/EquipmentQA/ChestActorDetails/build-final.log`.
- Both owned chest Blueprint compiles and generator-free PIE lifecycle: PASS,
  `Saved/EquipmentQA/ChestActorDetailsPIE01/result.json`. The fixture opts into
  actor selection with `EF_CHEST_ACTOR_DETAILS_QA=1` and still runs the original
  three-profile generation/reopening/exhaustion/fresh-session checks.
- User saves: PASS, seven exact saves checked/restored by `user_save_after.json`.
- Final visual placement: PASS, native MCP `final-deposit.png` and
  `final-locked.png` show actor Self selected and the section first in Details.
  `final-overrides.png` shows enabled mode, enabled overrides and the conditional
  Content Override row on the existing Armament fixture; reflected values were
  checked, restored and the test map was unloaded without saving.
- Protected hashes: PASS, 7,642 files unchanged,
  `Saved/EquipmentQA/ChestActorDetails/protected-final.json`.
- Cook/package: PASS, `Saved/EquipmentQA/ChestActorDetails/package.log`, UAT
  exit 0, cook 0 errors / 45 existing warnings. Development archive:
  `A:/NoShellForWinterChests/ActorDetails_01`.
- Packaged chest suites: PASS, all three exact `EF.Chests` suites, zero test
  failures/warnings, `Saved/EquipmentQA/Packaged_ChestActorDetails01/launch.json`
  and `Report/index.json`.

Initial native MCP screenshots confirmed both normal and locked actors expose
the section, plus the enabled checkbox on the normal actor. The final build
only moves the category higher in the panel; final screenshots are recorded
separately. No screenshot was synthesized or edited.

Durable copies, source locations and SHA-256 bindings are under
`Docs/Migration/Evidence/ChestActorDetails/manifest.json`. The editor was left
open on HUB with the normal chest actor selected and Procedural Mode off.
