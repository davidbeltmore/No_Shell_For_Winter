# Preserved HUB snapshot — 2026-09-23

`Content/_Game/Hub/HUB.umap` already contained chest replacements and prior
manual edits when this closure task began at `5084dcbd`.

The map is committed separately without resaving, replacing or reconstructing
it. Its SHA-256 before and after this task's PIE validations is:
`74f45bb4931e3f20db821a3cef9d61178d1d61b282bf692969a32b442d306402`.

The exact initial bytes are backed up under
`Saved/EquipmentQA/MasterClosure/Initial_20260923_042126/files/Content/_Game/Hub/HUB.umap`.
This is preservation of the user's whole existing map snapshot; it does not
attribute every unrelated manual map edit to the equipment implementation.

The real map passed inventory/save/load/death QA (`MasterInventory01`, 100
checks) and storage/lockpicking QA (`LockpickInput_Master02`, 64 checks), with
actual Slate screenshots inspected. See `EquipmentMasterClosure.md` for the
remaining release gates and their precise scope.
