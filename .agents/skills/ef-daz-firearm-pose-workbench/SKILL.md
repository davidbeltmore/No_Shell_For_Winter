---
name: ef-daz-firearm-pose-workbench
description: Diagnose and repair NoShellForWinter DAZ firearm and bow neck/upper-body deformation through owned ACF overlay and aim assets, with bounded directional player/NPC pose validation.
---

# DAZ weapon poses

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
Bow is an exception to full torso weight: use absolute Spine at 0.2 for carry
and 0.8 for aim, retaining Head BASE_POSE at 1. The former Spine=1 in both
stances passed static checks but roughly doubled head/chest divergence while
turning (42 degrees versus 21 unarmed). The corrected carry measured 25 degrees.
Preserve the absolute pose type; restoring native additive branches is not the
same as blending the absolute torso pose with locomotion.

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

## Bow directional aim regression

Always inspect neutral aim and control pitch +/-60 degrees. Neutral-only checks
missed the bow head collapse. Identify BOTH the legacy `AimOffset` asset and the
modern `Aim_Center/Up/Down/Left/Right` fields; native bow mixes a UE4-skeleton AO
with modern rifle/bow samples. A Head BASE_POSE profile does not necessarily mask
the later additive aim branch. Never assume a firearm fix covers bow.

For mesh-space additive aim, removing local neck tracks does not remove inherited
component rotation from animated spine parents. The editor-only
`StabilizeOwnedBowAimComponentRotations` helper stabilizes spine_01..spine_05 and
neck_01/neck_02/head component rotations against an owned center reference. It
transfers the original component aiming rotations into clavicle_l/r local tracks
so the arm/weapon direction survives the spine mask. It preserves translations,
scales and every other track. Its path guards exclude native assets.
Validate the resulting runtime pose before treating authoring as a repair.
Neck correction alone still moved the head with the folding spine. Masking spine
without compensating arm roots made the weapon direction nearly static. Preserve
native locomotion and the weighted absolute torso overlay; restrict corrections to owned
directional aim samples. Explicitly rebind all nine AO SampleData animations after retargeting;
the batch retarget returned an AO with empty sample references in this repair.
Do not set AimOffsetAlpha=0 as a fix: require directional hand position/orientation
changes, otherwise aiming can silently freeze while the neck looks correct.

Use `AuthorDAZBowOverlay.py` for complete owned bow authoring, bounded to 90 seconds
internally and 120 seconds externally. It uses a short owned non-additive center
reference instead of repeatedly recompressing the large legacy base sequence.
Treat that reference as immutable once consumed. To change its policy, create a
new owned reference path; editing a loaded shared reference stalled compression.
The persistent author reuses a complete owned set. If the set is partial, restore
missing assets from Git or author a reviewed versioned set; do not bulk overwrite
loaded samples and references. Native reload selects arrows when a player's old
ammo slot still contains bullets; certify shots through inventory count decrease.
Keep failed receipts; a host timeout does not cancel a blocked editor operation.
Use `RunBowPosePIE.py` and `ValidateGunPosePackaged.ps1 -Bow` for the four gender /
player-NPC cases, including directional aim and native arrow consumption. Require
head elevation >70% of the unarmed baseline and neck ratio <1.35. At +/-60 degrees
require hand orientation delta >=0.15 radians from neutral; incidental breathing
translation must not count as directional aiming. Explicitly spawn a native
controller for disposable NPC fixtures when auto-possession is absent, and fail
if it remains missing. Record visual evidence; metrics alone cannot certify pose.

For unnatural carry motion, compare a short walking/turning trace with unarmed
locomotion before changing aim samples. Inspect `NewLayering`, `Spine`, `Head`
and the actual sequence at runtime. Absolute torso and base-pose head can each
look correct alone but combine poorly during turns. The bow QA now adds an
8-second moving sequence for each actual player body: carry, aim, release;
requires >400cm travel, >=50 samples, carry head/chest angle <=32 degrees and
aim <=45 degrees, plus the existing proportion gates. Review its temporal
screenshots (`Case_##_motion_##.png`); these scoped limits are regression guards,
not universal anatomical limits or proof of all animations. A static PASS does
not certify walking, transition quality, yaw sweeps or visual naturalness.
Keep the route on walkable floor and assert grounding: the original +X route
from the static fixture walked off HUB's edge. Its falling animation lowered
world-vertical head elevation without shortening the neck. Inspect movement mode
and rendered frames before attributing such a failure to the animation policy.
Use `Docs/Migration/DAZBowMotionRepair.md` for the temporal regression evidence.

## Authoring and bounded validation

- Run `Tools/Animation/AuthorDAZWeaponOverlays.py` through the project's localhost
  editor Python runner (90-second internal ceiling). It compiles/saves only the
  selected owned overlays/profiles (default two firearm overlays and four profiles).
  Run the complete bow wrapper above when adding or rebuilding bow aim assets.
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
Bow direction and reference-space evidence belongs in `Docs/Migration/DAZBowAimPoseRepair.md`.
