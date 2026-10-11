# Arrow reactions on owned DAZ zombies

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8 /ACFU 4.4.2.
Baseline: `ae660e1eb2c1e20dd96f27cd712ee16e93b6da60`.

Arrows should play the existing upper-body light flinch while zombie legs keep
walking, even when calculated damage exceeds the generic 25-point light threshold.
Damage magnitude, penetration, critical calculation, native defense responses and
death remain unchanged. Other strong impacts retain the planted reaction.

## Identification

Read-only live MCP verified `/Game/_Game/Hub/HUB`. The native arrow item
`/Game/FullSample/Blueprints/Items/Ammo/ACF_Arrow_BP` uses
`/Game/FullSample/Blueprints/ItemActors/ACF_ArrowActor_BP`; its collision trace uses
`/AscentCombatFramework/Blueprints/DamageTypes/ACFPenetrationDamageType`.
That damage class is also referenced by cannonball/grenade actors, so class-only
classification would change unrelated attacks. Native collision damage forwards
the shooter as DamageDealer; the current equipped weapon cannot reliably identify
a previously fired arrow.

`UProjectArrowImpactContext` observes only the native arrow actor ancestry through
world spawn notifications and a component without Tick. Native trace collision
broadcast precedes synchronous ApplyDamage. The observer records receiver, dealer,
frame, bone and impact position; the owned zombie calculator consumes that context
once for that matching damage event. Skipped entries expire on the next mark;
ACF reconstructs the event's ImpactPoint from the incoming FHitResult.Location;
the observer matches that sweep position, not the original surface ImpactPoint.
The first physical-arrow test rejected the surface-point comparison and retained
the strong native hit; its failed receipt is preserved in Saved/ArrowHit.
World teardown clears the map. No native Blueprint, projectile trace configuration,
damage type, FX lookup or shared body asset is changed in production.

The existing light policy still checks positive damage, life, immortality and
ability availability; native block/parry/special responses take precedence. Only
the generic light damage ceiling is bypassed for an identified arrow.

## Bounded gates

`Tools/Animation/RunArrowHitPIE.py` / `Project.ZombieHit.QA arrow` reuse the existing
two-zombie fixture. It creates the actual arrow through native deferred spawn,
InitItemActor, FinishSpawning, ActivateDamage and physical flight. Its disposable
trace base damage is 60 to prove identity-based light selection above 25.
Native damage type, damage calculation and collision pipeline remain active.
Require actual final damage >25, Actions.HitLight, no movement hold, >30 cm walking,
leg/torso pose deltas and a visible flinch. The same fixture then checks strong
non-arrow reactions, interruption, pursuit recovery and death translation.
Native ceiling 90s /PIE host 120s. Packaged entry:
`ValidateZombieHitPackaged.ps1 -StagedRoot <package>/Staged -Arrow`, 180s ceiling.
This certifies projectile impact and reaction, not bow input, aim or networking.

Implementation: `f5a55d13`, based on the baseline above. Versioned receipts:
`Docs/Migration/Evidence/ArrowHit_20261010`.

| Gate | Result | Evidence /metrics |
|---|---|---|
| Editor /Game build | PASS | Final diagnostic build: `editor-build.json` 43.624s; `game-build.json` 118.945s, each within 600s |
| Owned Blueprint compile | PASS | `blueprint-compile.json`, existing zombie AnimBP and Male/Female zombie, 0.641s |
| Physical arrow PIE | PASS | `pie-host.json`, 26.750s; `pie-runtime.txt`, 2/2 zombies in 21.289s |
| Arrow Male /Female | PASS | Native penetration damage 112.982 /108.201; HitLight; walking 130.752 /121.500 cm; thigh 1.6365 /1.5137 rad; torso 0.9853 /0.9129 rad; no movement hold |
| Strong non-arrow regression | PASS | 4 normal/interrupted reactions, each 0 cm slide /0 cm/s; pursuit recovery >100 cm in 0.451–0.590s |
| Death regression | PASS | Both dead, 0 cm displacement /0 cm/s; one retained native Moving status without translation |
| Protected files | PASS | `protected.json`, 7,643 /0 changes |
| User saves | PASS | `save-restore.json`, original saves verified/restored |
| Fresh cook | PASS | `cook-package.json`, exit 0, 261.870s; process 262.474s /900s ceiling |
| Final UAT re-stage | PASS | `restage.json`, current executable hash verified; 59.683s /180s ceiling; unchanged successful cooked content |
| Final packaged runtime | PASS | `packaged-process.json`, 26.375s /180s ceiling, exit 0 /0 feature errors; `packaged-runtime.txt`, 2/2 cases in 16.940s |
| Packaged arrows | PASS | Male /Female final damage 107.436 /103.730; walking 168.210 /172.577 cm; thigh 1.0296 /1.0710 rad; torso 0.8497 /0.8413 rad |
| Packaged strong/death regression | PASS | 4 strong/interrupted reactions, each 0 cm slide /0 cm/s; >100 cm pursuit recovery in 0.423–0.700s; both deaths 0 cm /0 cm/s |
| Rendered visual | PASS | `light_torso_0_0.png`, `light_torso_1_0.png`: walking legs with torso flinch and physical arrow attached; no head/neck collapse |
| Final editor | PASS | `final-editor.json`, HUB reopened with Daz launch guard; live MCP confirms HUB, no PIE /0 dirty packages |

The PIE screenshot requests captured the editor viewport rather than the gameplay
camera; those images are excluded from visual evidence. Rendered visual QA will
use the packaged fixture, which has a single gameplay viewport. Pose/action/
movement measurements above are from the actual PIE pawn, not those captures.

The first packaged run passed both arrows and both normal strong reactions, but
timed out waiting for female pursuit after moving the player to the other end of
the corridor. Its failed process/runtime receipts are retained as
`rejected-packaged-*`. That run had already measured >100 cm recovery after the
strong reaction; it does not establish a stuck reaction hold. Cause: PENDING.
The fixture now records target, AI/action tags, path status, movement hold and
native pause on pursuit timeout. A diagnostic run with unchanged gameplay policy
passed 2/2 cases in 17.857s. No target forcing, deadline extension or weaker
acceptance thresholds were introduced. The final staging uses UAT and verifies
the executable hash against the current Game build. Reusing the already successful
cook is limited to this QA-only code change; production code/assets are unchanged.

Reusable future source policy instructions:
`.agents/skills/ef-animation-movement-calibration/references/upper-body-damage-policy.md`.
They separate source classification, native damage calculation, torso layering and
bounded acceptance. A general editable damage-policy asset is not yet implemented.

Retained validated distribution: `Saved/BodyPresetsQA/Package_20261010_191904`.
Final packaged receipt: `Saved/ArrowHit/Packaged_20261010_193608`, isolated UserDir.
After dry-run, safety classification and quarantine, old duplicate Cooked/Staged
output was removed: 2 targets /4,988,514,308 bytes. Logs, failed receipts, saves,
rollback copies and productive assets were retained.
