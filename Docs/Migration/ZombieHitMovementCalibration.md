# Zombie impact and movement calibration

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8, installed ACFU 4.4.2.
Source project and installed framework/plugins remain read-only.

## Problem and acceptance

The owned Male/Female zombies slid forward through their planted hit animation.
The initial native point-damage reproduction captured `Actions.Hit`,
`HitReact_Front_Montage`, 350 cm/s movement, and 479.579 cm displacement across
80 reaction samples / 1.375 seconds. Native `UACFActionAbility::PreActivate`
pauses the behavior tree, but an existing path-following request continues.
This establishes the defect without attributing it to an unverified update.

The user subsequently requested light torso reactions while legs continue moving,
with strong full-body reactions remaining planted.

Measured gates:

- Light: actual native damage chooses `Actions.HitLight`; walking continues at
  least 30 cm; thigh rotation changes >0.05 rad; torso rotation changes >0.02 rad;
  no navigation hold. Inspect the visible blend as well as these state metrics.
- Strong: natural pre-hit pursuit >100 cm/s; actual hit samples, unwanted
  translation <=3 cm / velocity <=5 cm/s; native pursuit resumes >100 cm within 6s.
- Repeat/interruption: a second hit interrupted through native Montage_Stop
  releases the hold and recovers pursuit. Death during another reaction must not
  resume translation. Record the native path status separately.
- Two representative owned zombies, 90s native /120s PIE host /180s packaged
  process limits. Engine point damage exercises the hitscan damage/action path;
  this fixture does not certify projectile aiming, gun input or network latency.

## Project-owned implementation

`UProjectActionMovementGateComponent` is installed on owned `EF.NPC.Zombie` pawns
through the existing controller-change subsystem. It uses native ability and
montage lifecycle delegates, pauses rather than aborts the current navigation
request, and resumes only the same request on the same controller while alive.
It leaves native root motion, movement mode and behavior-tree ownership intact.
ACF ability end can occur at blend-out start, so the visual tail is considered
before release. It has no polling tick.

`UProjectZombieDamageCalculator` retains native damage/defense and strong response
calculation by composing the actual configured native Blueprint calculator.
Its GAS attribute mappings are preserved. Positive calculated impacts <=25 use the granted light reaction when
native response is empty or normal hit; block/parry/heavy responses win. Immortality
and missing/blocked light ability retain the native result. Strong reactions still
follow native stagger resistance. This is an opt-in owned zombie policy.
The light threshold is configurable in DefaultGame.ini under
`/Script/EFProjectSystemsGameplay.ProjectZombieDamageCalculator`.
Identified arrow impacts subsequently bypass this ceiling and retain the walking
torso reaction. See `ArrowZombieHitCalibration.md` for the source-identification
adapter and measured physical-arrow gates; damage magnitude remains native.

Owned `ABP_DAZZombie` retains its existing locomotion/DefaultSlot branch and caches
that complete pose. An existing `UpperBody` slot is layered over that cache from
`spine_01` with depth 3, mesh-space rotation blend and root-based root-motion weight.
Pelvis/legs stay on the cache. When the light slot is inactive, the original full
pose remains. No skeleton slot registration or shared skeleton save is necessary.
The owned light sequence disables root motion, and its montage uses a 0.65s clip
at 1.15 rate, 0.08s blend-in and 0.12s blend-out.
The short flinch uses one authored Front section for all hit directions; it does
not claim four distinct directional gestures.

Authoring: `Tools/Animation/AuthorZombieHitLayers.py` (90s), backed by the guarded
editor-only `ConfigureOwnedZombieHitAssets`. Owned initializer assets use the
owned ability set; native assets and other NPC roles are not remapped.

## Evidence

Baseline: `Saved/ZombieHit/Baseline/before.json`, 7,643 protected files, HEAD
`ddda02170e11847c4a8aaed3609282be8129c54c`. Reproduction trace:
`Saved/ZombieHit/before.json`; live MCP HUB and graph inspection recorded locally.
The first movement-gate PIE had 1.705 cm residual movement /51.106 cm/s peak and
was rejected by the velocity gate. `StopActiveMovement` now also clears the
movement component's pending requested velocity. Final gates remain PENDING.

A rejected intermediate PIE exposed Attribute.Get() in native IsCriticalDamage:
a bare C++ subclass lacked attributes authored on ACFGASDamageCalculatorBP.
The final wrapper captures that configured BP class and delegates to a live native
calculator instance. Crash log retained at `Saved/ZombieHit/calculator-crash.log`.
An intermediate light pass observed real locomotion (74.186 cm), but pose samples
were stale while the test camera faced away. The disposable fixture now sets up
the camera before the reaction and refreshes bones independent of visibility.
Production animation tick policy remains unchanged.

A rejected recovery fixture repeatedly retreated relative to the zombie and left
the HUB NavMesh. Live projection rejected X=-1290/-1478 at Y=-3200 on both navigation
datasets. The final fixture alternates verified X=-400/800 targets and checks
projection error <=20 cm; it does not expand navigation or force AI targets.
Clearing requested velocity alone also left queued acceleration-based pawn input:
the intermediate strong reaction moved 1.768 cm with a 51.364 cm/s peak. Activation
now consumes that pending input as well. Acceptance thresholds were not relaxed.

The native death path disables CharacterMovement but can leave its path request
reported Moving. A bounded observer confirmed dead=1, speed=0 and unchanged
position while that status persisted. The death gate measures actual displacement
<=3 cm and speed <=5 cm/s over 0.6s, and reports path status without treating it
as proof of movement. The adapter itself never resumes a dead pawn's request.

Implementation commit: `e7590777bc4a92da72b9fae66e3910c5b1ca4dc9`, based on
`ddda0217`. Receipts and representative
images under `Docs/Migration/Evidence/ZombieHit_20261010`.

| Gate | Result | Evidence / metrics |
|---|---|---|
| Editor build | PASS | `editor-build.json`, 16.144s /600s ceiling |
| Game build | PASS | `game-build.json`, 33.096s /600s ceiling |
| Owned animation authoring and compile | PASS | `author.json`, 0.797s; owned AnimBP and both zombie Blueprints compiled |
| NPC inherited logic audit | PASS | `roster-parity.json`, 40 variants /45,546 properties /0 mismatches, 12.172s |
| PIE | PASS | `pie-host.json` 23.063s; `pie-runtime.txt`, 2/2 cases, native 18.821s |
| Light Male /Female | PASS | 93.814 /150.616 cm walking; thigh 1.6456 /1.0697 rad; torso 0.9009 /1.3309 rad |
| Strong and interrupted strong | PASS | 4 reactions, each 0 cm slide /0 cm/s peak; recovery >100 cm in 0.429–0.450s |
| Death during reaction | PASS | Both dead, 0 cm translation /0 cm/s |
| PIE visual | PASS | `pie-light_torso_*` walking legs with torso flinch; `pie-reaction_*` planted full-body reaction |
| Protected invariants | PASS | `protected.json`, 7,643 files /0 changes |
| User saves | PASS | `save-restore.json`, 2 checked; changed original restored |
| Cook /package | PASS | `cook-package.json`, fresh cook, exit 0, 290.120s /900s ceiling |
| Packaged QA | PASS | `packaged-process.json`, 32.511s /180s ceiling, exit 0, 0 feature errors; `packaged-runtime.txt`, 2/2 cases in 15.863s |
| Packaged movement | PASS | Light Male /Female: 154.274 /152.907 cm walking with thigh and torso pose deltas; 4 strong reactions each 0 cm /0 cm/s; recovery >100 cm in 0.399–0.413s; both deaths 0 cm /0 cm/s |
| Packaged visual | PASS | `packaged-light_torso_1_0.png` walking legs and torso flinch; `packaged-reaction_0_0.png` planted reaction |
| Final editor | PASS | `final-editor.json`, HUB reopened with protected Daz launch guard, no PIE and no dirty packages |

Retained validated distribution:
`Saved/BodyPresetsQA/Package_20261010_160141/Staged/Windows`.
Packaged receipt root: `Saved/ZombieHit/Packaged_20261010_160655`, isolated UserDir.
Old duplicate Cooked/Staged output was classified, quarantined and removed after
dry-run: 2 targets /4,988,101,889 bytes reclaimed. Receipts, saves, rollback copies,
productive assets and the newest validated distribution were retained.

Implementation and reusable skills are on `feature/efclothing-v4-multicloth`.
Remote exact-HEAD publication verification is recorded after the evidence commit
under `Saved/ZombieHit/publication.json`.
