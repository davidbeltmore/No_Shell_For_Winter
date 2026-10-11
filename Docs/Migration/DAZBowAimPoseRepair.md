# DAZ bow aiming pose repair

Target: D:/Projects UE5/NoShellForWinter, UE 5.8 /ACFU 4.4.2.
Baseline: c32f13412df75b768d16b27e9bc3f1d2f938481a.
Implementation snapshot: 4005d387, branch feature/efclothing-v4-multicloth.
Tracked metric receipt: [Evidence/DAZBowAim20261010.json](Evidence/DAZBowAim20261010.json).
Read-only source: D:/Projects UE5/LustAsDeadlySin; this task writes only target-owned
assets, code, config, tooling and evidence.

## Reproduction and scope

The real /Game/TattooShop/Blueprints/TSCharacter/BP_TSChar player, using Female,
equips native ACFBowBP and runs ACF_BowAdditiveOverlay. Neutral aim alone missed
the problem. At -45 degrees, head elevation above the clavicle midpoint fell
from 17.809 to 9.950 cm. Removing the overlay restored 17.840 cm. The native bow
overlay was outside the previous exact pistol/rifle compatibility mappings.

The native overlay has two aiming paths: legacy BowAim_AO on the UE4 skeleton,
plus Aim_Center/Up/Down/Left/Right fields combining rifle poses and UEFN_Bow_D.
A project-owned absolute torso profile with explicit Head base restoration did
not fix directional aim: elevation was 8.574 cm downwards. Retargeting the legacy
AO and pointing the modern fields to owned bow samples also remained insufficient.
AimOffsetAlpha=0 prevented the head collapse but froze hand orientation, so this
candidate is rejected. Disabling aiming is not the intended repair.

Removing local neck tracks and stabilizing neck component rotation alone still
left the head following the folding spine. Masking spine rotation alone improved
head elevation to 15.69 cm but nearly froze the arms, so that intermediate is also
rejected. Incidental breathing translation must not satisfy directional aiming.

The owned editor helper stabilizes the five spine and three neck/head component
rotations against a short center reference, then transfers the original sample
component rotations to clavicle_l/r. It writes those ten local rotation tracks,
preserving translations/scales and all other tracks. Native main AnimBP, damage,
locomotion and weapon actions remain in place. AimOffsetAlpha stays 1.
The owned AO's nine SampleData references must be explicitly rebound: retargeting
initially produced nine positions with empty animation references.

Reference recompression stalled twice when editing an already-consumed reference;
failed receipts/logs are retained in Saved/BowPose. The final short center reference
is immutable; policy revisions require a fresh owned path. No claim that an ACFU
update caused the defect is established.
No source project or Marketplace code is modified.

## Required gates

PASS: final pose policy, build, compile, actual player/NPC directional PIE,
rendered visual review, fresh cook and packaged validation.
The bow fixture tests hold, neutral aim, +/-60-degree directional aim, native shot
and player inventory/body restoration on both DAZ genders. Firearm regression is
kept separate. Native/host/process ceilings remain 110/140/180 seconds.
Require head elevation >70% of unarmed baseline, finite neck ratio <1.35 and
hand orientation change >=0.15 radians during directional aim. A neutral ready-to-aim bow stance can
retain its center hand pose; directional tests must not pass with frozen hands.
The disposable NPC fixture explicitly spawns its native controller if automatic
possession is absent; missing control previously caused a legitimate frozen-aim
failure. Controller tick is disabled only inside this pose fixture so its
native control rotation can hold the requested pitch; this is not an AI test.
The fixture uses native Reload(true) to select arrows when a restored player's
ammo slot still contains bullets. All four native shots consumed one arrow, and
original inventory identities/counts, equipment and appearance were restored.

| Gate | Result | Evidence under target Saved/ | Bound |
|---|---|---|---|
| Editor build | PASS, 17.914 s | NPCDAZ/bow-editor-ammo-process.json | 600 s |
| Game build | PASS, 35.893 s | NPCDAZ/bow-game-ammo-process.json | 600 s |
| Idempotent author | PASS, 4.863 s host | NPCDAZ/bow-author-idempotent-process.json; BowPose/aim-author.json | 90 s native /120 s host |
| Owned Blueprint compile | PASS, 3/3 up to date | BowPose/compile.json | <1 s |
| Bow PIE | PASS, 4/4, 43.607 s native | BowPose/Runtime/runtime.txt; BowPose/pie-host.json | 110/140 s |
| Firearm regression PIE | PASS, 8/8, 65.003 s native | GunPose/Runtime/runtime.txt; GunPose/pie-host.json | 110/140 s |
| Visual review | PASS, head outside torso, directional arms retained | BowPose/Runtime/Case_01_aim_down.png, Case_02_aim_down.png, Case_03_aim_down.png | Representative frames |
| Fresh HUB cook/package | PASS, exit 0, 257.057 s | BodyPresetsQA/Package_20261010_204511/package-result.json; uat.log | 900 s host |
| Packaged bow | PASS, 4/4, 43.604 s native; 60.762 s process; 0 feature errors | BowPose/Packaged_20261010_204939/process.json; UserData/Saved/BowPose/Runtime/runtime.txt | 110 s native /180 s process |
| Packaged firearm regression | PASS, 8/8, 64.812 s native; 78.533 s process; 0 feature errors | GunPose/Packaged_20261010_205107/process.json; UserData/Saved/GunPose/Runtime/runtime.txt | 110 s native /180 s process |
| Packaged visual | PASS, actual Female player head outside torso when aiming down | BowPose/Packaged_20261010_204939/UserData/Saved/BowPose/Runtime/Case_03_aim_down.png | Representative frame |
| Protected files | PASS, 7,643 hashes, 0 changed | BowPose/Baseline/after.json | 60 s host |
| User saves | PASS, 4 checked/restored | BowPose/SaveGuard/user_save_after.json | Closed editor restoration |
| Final editor | PASS, HUB, no PIE, no dirty packages | BowPose/final-editor.json; NPCDAZ/bow-final-editor-ready-process.json | 120 s native readiness /150 s host |

At +/-60 degrees, hand orientation changed approximately 0.633/0.525 radians.
Female NPC downward head elevation was 14.566 cm against 17.751 cm baseline;
Male.02 actual player was 15.108 cm against 18.530 cm baseline. Neutral head/neck
proportions remain close to baseline. This certifies the reported aiming pose,
native shot primitive and representative bodies, not every bow charge/GAS montage
or extreme morph combination.

Four rejected owned assets were removed through the editor only after confirming
zero referencers; 2,311,230 bytes are backed up in BowPose/RejectedAssets.
No extra candidate copies remain in the shipped BowAim folder.

The persistent author reuses complete owned assets. A partial set fails explicitly
instead of force-overwriting a loaded reference set. The default firearm author
retains its two-weapon scope; the complete bow wrapper adds the bow-specific aim
authoring and compile assertion.

Cook retains baseline warnings for native ANS_MapMarker_WBP, AssetRegistry queries
and the missing native Quinn post-process dependency. The same messages exist in
Package_20261010_191904/uat.log; none is evidence of this bow repair failing.

Validated package retained: Saved/BodyPresetsQA/Package_20261010_204511.
After dry-run and successful packaged tests, the retention tool removed only the
previous package's Cooked/Staged outputs: 4,988,694,884 bytes across two paths.
Logs, receipts, saves and rollback backups remain. Evidence: NPCDAZ/bow-retention-
dry.log, bow-retention-apply.log and their process JSON receipts.

Reusable prevention: .agents/skills/ef-daz-firearm-pose-workbench/SKILL.md.
Final publication procedure verifies exact remote HEAD and a clean working tree;
its receipt is Saved/BowPose/publication.json.

Protected baseline: Saved/BowPose/Baseline/before.json, 7,643 files.
User-save baseline: Saved/BowPose/SaveGuard/user_save_before.json, four saves.

Follow-up: [DAZBowMotionRepair.md](DAZBowMotionRepair.md) addresses unnatural
head/chest motion missed by this static matrix. Its 0.2 carry / 0.8 aim absolute
Spine weights supersede the bow's original full-weight torso policy. The owned
directional aim sample correction described above remains in place.
