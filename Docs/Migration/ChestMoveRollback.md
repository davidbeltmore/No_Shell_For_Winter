# Chest move rollback — 2026-09-23

User requested reversal of accidental Content Browser moves, explicitly authorizing restoration of the original ACF asset. Snapshot: HEAD `1114c8435e8a767e6c193c3eaccfb22de61eada3`; per-file backups and reports: `Saved/EquipmentQA/ChestFolderRepair/`.

Native Unreal AssetTools restored:

- `/Game/_Game/Chest/ACF_Deposit_BP` → `/AscentCombatFramework/Integrations/Actors/ACF_Deposit_BP`.
- `/Game/_Game/Chest/BP_EquipmentDeposit` → `/Game/_Game/Items/Chests/BP_EquipmentDeposit`.
- `BP_EquipmentLockedChest` remains at `/Game/_Game/Items/Chests/BP_EquipmentLockedChest`.

Scoped checks: PASS. All three Blueprints compile up to date with their original parent hierarchy; native data validation checked three assets, all valid, zero warnings/errors, including AssetReferenceRestrictions. Evidence: `verify-result.json`, `verify-final.log`. All 32 HUB actor names, labels and transforms match the preserved user state. User changes were saved; no Git reset or binary replacement was used.

Cleanup limitation: native `delete_loaded_asset` returned true for the two unreferenced redirectors, but fresh-editor and disk inspection show both redirectors remain under `/Game/_Game/Chest`. The report's `removed_unreferenced_redirectors` records native API acknowledgments, not persisted deletion. They are retained as compatibility redirects; no force or filesystem deletion was performed. The real Blueprints are at the original paths.

Protected hashes: 7,642 checked before/after; only the explicitly authorized original `ACF_Deposit_BP.uasset` changed. Evidence: `protected-comparison.json`, `Saved/ClothingUnisexQA/protected_chest_move_before.json`, `protected_chest_move_after.json`. Source project untouched. Daz receipt guard PASS.

Live editor reopened in NoShellForWinter UE 5.8.3, HUB, no PIE and no dirty packages: `context-after.json`. Visual editor check: `restored-editor.png`; chest arrangement preserved. Build, PIE gameplay, cook and packaged validation for this rollback: PENDING (not run). This is scoped asset restoration evidence, not a new full migration/release acceptance.
