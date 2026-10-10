# DAZ firearm pose repair

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.3, installed ACF Ultimate 4.4.2.
Source game `D:/Projects UE5/LustAsDeadlySin` remained read-only.
Implementation snapshot: `a78a8298396caa752b18c6d1aa1e0601678db733`.
Starting snapshot: `eca786852c50611bfc808a7967e2bad0d9eb9b74`.

## Diagnosis and resulting behavior

The actual TattooShop-derived player and owned DAZ gun NPCs used the MM/GASP
additive weapon overlays. Native Manny gun NPCs used the legacy overlay and did
not show the same elongated neck. A transient A/B test removed the weapon overlay
and eliminated neck elongation; absolute upper-body overlay profiles then retained
weapon hold/aim poses with normal neck proportions on both DAZ genders.

The confirmed causal branch is the MM/GASP dynamic/secondary additive layering
on these DAZ bodies. The exact underlying retarget/reference-pose interaction is
not independently certified. There is no evidence establishing that an ACFU
update caused this regression. The MetaHuman guideline warnings are unrelated to
this finding. The observed sequence-evaluator dynamic-frame warning is recorded,
but was not established as the cause and vendor graphs were not patched.

Owned pistol/rifle overlay copies keep the existing main AnimBlueprint. Spine and
arms take the weapon overlay pose at weight 1. Head, pelvis, legs and secondary
additive branches use the base pose. Rifle Head actively restores base pose at
weight 1; zero weight left head deformation inherited from the spine branch.
A separate missing-socket defect placed sheathed pistols on the ground: the
`PistolEquip` socket is absent on the current DAZ meshes. The actor-owned
`UEFDAZWeaponSocketAdapter` mounts only configured missing sheath sockets using
the canonical Manny socket bone/local transform after native equipment events
and body changes. It does not edit shared skeletons or weapon definitions. An exact source-class mapping in
`UEFDAZWeaponPoseSettings` applies only to configured DAZ mesh roots through the
existing appearance-change bridge. It also refreshes an already active overlay
and survives skeleton-changing body swaps through the existing ACF cache/table
restoration. Other weapons and native bodies retain their original mappings.

Authoring: `Tools/Animation/AuthorDAZWeaponOverlays.py` (90-second ceiling), two
owned AnimBlueprints and four layering profiles under
`/Game/_Game/Characters/Animation/Weapons`. No protected Player/body/Marketplace
package was intentionally saved.

## Gates

| Gate | Result | Evidence / ceiling |
|---|---|---|
| Controlled visual isolation and candidate | PASS | Saved/GunPose diagnostics; normal neck in held/aimed pistol candidate |
| Owned Blueprint authoring/compile | PASS | Saved/GunPose/author.json; 90s |
| Editor cold build | PASS | Saved/NPCDAZ/gun-editor-build-process.json; 18.785s / 240s |
| Game cold build | PASS | Saved/NPCDAZ/gun-game-build-process.json; 38.551s / 240s |
| Integrated PIE, 8 cases | PASS | pie-runtime.txt: 65.088s; host: 69.015s; native 110s / host 140s |
| Integrated visual review | PASS | Player/NPC male/female pistol/rifle hold/aim screenshots |
| Representative NPC appearance regression | PASS | 12/12, 11.201s native / 13.391s host; ceilings 90/120s |
| Protected invariants | PASS | Saved/GunPose/Baseline/before.json: 7,643 files |
| Cook / package | PASS | Package_20261010_144113; exit 0; 289.072s / 900s |
| Packaged 8-case pose matrix | PASS | 8/8; native 64.772s; process 76.663s / 180s; exit 0, feature errors 0 |
| User save restoration | PASS | Two original .sav hashes checked; one restored after PIE |
| Git publication | Terminal verification | Saved/GunPose/git-publish.json is recorded after this evidence commit |
| Final editor | PASS | HUB, PIE false, no dirty maps/content; final-editor.json |

The native pose fixture checks active owned overlay class, movement AnimInstance
cache identity, finite upright pose, head scale and vertical elevation, neck length relative to unarmed
baseline, aim transition, native shot/ammo consumption, exact stowed weapon socket/bone mounting and player inventory/body
restoration. It stops fixture NPC brains and does not certify enemy pursuit. Native shots consume existing inventory ammunition; the fixture refunds only the
original changed counts (including ambient gameplay during PIE) with the native FastArray identity-preserving change API,
refreshes weight and asserts all original GUIDs/counts afterward. Direct
native shooting does not certify every GAS action, fire montage, multiplayer,
first-person overlay, reload or every imported body. Inspect images in addition to
numeric receipts. Failed compiler/fixture iterations are implementation work and
are not reported as successful product gates.

Reusable skill: `.agents/skills/ef-daz-firearm-pose-workbench/SKILL.md`.

The actual player can mount native weapons on its configured compatibility mesh.
The stow gate requires a skeletal parent owned by that player and alignment to
the visible mesh socket/bone, rather than assuming its parent is CharacterMesh0.

Versioned evidence: `Docs/Migration/Evidence/DAZFirearmPoses`. The retained latest
validated package is `Saved/BodyPresetsQA/Package_20261010_144113`. Dry-run and
scoped retention removed 4,987,580,589 bytes of prior Cooked/Staged output, keeping
prior receipts/logs and the new validated package. Full protected hash manifests
remain in Saved/GunPose/Baseline; their hashes and per-body invariants are versioned
in protected-summary.json. All 7,643 protected files remained unchanged after cook.
