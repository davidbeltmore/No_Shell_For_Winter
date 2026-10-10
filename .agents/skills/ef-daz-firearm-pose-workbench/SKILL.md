---
name: ef-daz-firearm-pose-workbench
description: Diagnose and repair NoShellForWinter DAZ firearm neck/upper-body deformation through owned ACF overlay profiles, and run bounded player/NPC pose validation.
---

# DAZ firearm poses

Start with `noshellforwinter-unreal-mcp`, then identify the actual PIE pawn, body,
AnimInstance, active overlay tag/class and equipped weapon. Template assignments
alone missed the actual TattooShop-derived player in the October 2026 repair.

## Proven compatibility path

DAZ bodies using MM/GASP pistol and rifle additive overlays exhibited elongated
necks. Removing that overlay isolated the causal branch; disabling unrelated IK
or changing MetaHuman requirements did not establish a fix. The owned policy in
`/Game/_Game/Characters/Animation/Weapons` uses absolute overlay Spine/ArmL/ArmR
(weight 1), base pose for Legs/Pelvis and secondary additive branches (weight 0).
Rifle Head must actively restore BASE_POSE at weight 1: weight 0 left the head
inherited from the spine override and collapsed it into the chest. Pistol Head
retains its measured base/zero policy.
Do not assume this policy suits another weapon, skeleton or ACF version.

`UEFDAZWeaponPoseSettings` maps exact source overlay classes to owned replacements
only for configured DAZ mesh roots. The existing appearance-change bridge restores
ACF tables/cache and applies replacements. Preserve the main AnimBP, locomotion,
other overlays, Marketplace code and protected body assets. Inspect active classes
after initial spawn and skeleton-changing preset swaps; authoring receipts alone
cannot prove initialization ordering or packaged soft-reference availability.

## Missing sheath sockets

`PistolEquip` is absent on the three current DAZ bodies and even the shared UEFN
mesh; native Manny has it on `spine_03`. `UEFDAZWeaponSocketAdapter` copies the
reference socket's local mounting transform onto the actor's stowed weapon root,
attached to the existing bone. It runs after native equipment events/body changes,
uses no polling tick, and never adds sockets to shared protected meshes. Settings
limit the fallback to configured names and DAZ roots. Check draw, sheath and body
swaps: an equipped weapon appearing on the ground is not an animation-layer fix.

## Authoring and bounded validation

- Run `Tools/Animation/AuthorDAZWeaponOverlays.py` through the project's localhost
  editor Python runner (90-second internal ceiling). It compiles/saves only the
  two owned overlays and four owned layering profiles.
- Layer profile fields are EditDefaultsOnly. Edit owned asset/CDO copies. For
  reflected overlay struct arrays, construct fresh `unreal.OverlayLayer` rows and
  assign the full array; mutating retrieved wrappers silently failed to persist.
- Cold builds and launches use `Tools/Migration/*58.ps1` protected wrappers.
- `RunGunPosePIE.py` issues `Project.GunPose.QA`: eight cases, 110-second native /
  140-second host deadlines. Require fresh `Saved/GunPose/Runtime/runtime.txt`,
  host cleanup receipt and visual inspection. It checks neck proportion versus
  unarmed baseline, overlay/cache identity, aim transition, native shooting, stowed socket mounting and
  player inventory/appearance restoration. Native shooting is not certification
  of every GAS action or firing montage. NPC brains are stopped for this pose
  fixture; it proves nothing about pursuit.
- `ValidateGunPosePackaged.ps1 -StagedRoot ...` runs the same matrix with an
  isolated UserDir and 180-second process ceiling after cooking the final assets.
- Preserve failed receipts and inspect the failing stage before changing an
  assertion. Keep user save backups outside fixture mutation and restore after
  safely closing the editor.

Measured diagnosis and gate evidence belong in `Docs/Migration/DAZFirearmPoseRepair.md`.
