---
name: ef-animation-movement-calibration
description: Diagnose and calibrate NoShellForWinter hit reactions and short actions whose visible pose conflicts with locomotion, navigation or root motion. Measure movement during real actions and verify recovery through bounded gameplay fixtures.
---

# Animation and movement calibration

Start with `noshellforwinter-unreal-mcp` and a read-only live context pass. For NPC
ancestry or initialization, use `ef-npc-daz-workbench`. Keep installed ACFU, the
source project, protected bodies and Player read-only. Use project-owned adapters
or owned action assets for a repair.

For future requests to make another damage source react only above the hips,
follow [upper-body damage policy](references/upper-body-damage-policy.md).
Reuse the existing light action/torso layer; separate damage classification from
animation selection and preserve the configured native damage calculation.

## Diagnose the disagreement

Identify the actual pawn, visible mesh, AnimInstance, active ability, montage,
slot and controller. Compare three windows: moving before activation, the visible
reaction, and movement after its blend-out. Record elapsed time, world displacement,
velocity, action/section and path-following status. A pose-only screenshot cannot
prove that movement stopped or recovered.

Separate intended movement from sliding. A full-body planted reaction can need a
temporary navigation pause; an upper-body flinch can legitimately retain walking;
a root-motion recoil can intentionally displace the capsule. Do not globally
freeze every montage, suppress root motion or alter all hit reactions from one
example. Check the animation and native action contracts before choosing a policy.

In ACFU, pausing the behavior tree and pausing an active navigation request are
different operations. Inspect both. Prefer preserving an existing request over
aborting it and issuing artificial repeated MoveTo commands. Do not use permanent
MOVE_None or replace the controller/AnimBP to hide the mismatch.

## Reproduction and acceptance

Use the real damage/action pipeline. Low damage may not exceed native stagger
resistance, so absence of a hit montage is not evidence of successful calibration.
Wait until the world has begun play and match the native spawn contract, including
SpawnDefaultController when needed. A pawn without a controller cannot certify AI.
Project each fixture target onto the effective navigation data and reject large
projection offsets. Repeated relative retreats can leave the NavMesh and falsely
look like a movement-recovery regression. Keep pose bones refreshed in disposable
fixtures when the camera is offscreen; stale transforms cannot certify layering.

Define thresholds from the intended animation: sufficient pre-hit movement,
maximum unwanted displacement during the reaction, and resumed pursuit afterward.
Test a repeated reaction and one interruption or death to detect stuck locks.
For death, measure displacement and velocity. Native ACF can retain a Moving
path status after disabling pawn movement; report that status separately rather
than interpreting it alone as resumed translation.
Use finite native and host deadlines, disposable fixtures and restored player/save
state. Preserve failed receipts; never accept an empty reaction sample set.

Bind lifecycle events instead of scanning pawns or polling animations every frame
in shipped code. Restore only state/request IDs the adapter actually owns. Do not
resume a route after death or overwrite a newer navigation request. Calibration
parameters must express the authored action, rather than an arbitrary global delay.

For a walking flinch, retain a cached full pose (locomotion plus normal action slot)
and layer a separate existing torso slot from a verified spine bone. Keep pelvis
and legs outside that branch, and validate the cached pose is evaluated once.
Check an existing skeleton slot before creating nodes; do not save a protected
skeleton to add metadata. Disable root motion only on an owned light clip when
its authored displacement is unwanted; retain strong/native action root motion.

ACF action end can signal blend-out start. Inspect the montage tail before
releasing a planted movement hold. Clear both current velocity and pending native
requested velocity (`StopActiveMovement`) if the latter causes a first-frame slip.
For acceleration-based navigation, also consume already queued pawn movement input
at activation. Verify the next frame: clearing velocity/acceleration alone can
still allow the pending input to accelerate the planted reaction.
Keep weak lifecycle references and avoid replacing ability-task montage delegates;
the AnimInstance multicast end event can observe completion without stealing them.

Preserve the effective native damage calculator class. A native C++ superclass
can lack GAS attribute mappings authored on its Blueprint child; replacing that
child with a bare C++ subclass caused Attribute.Get() to assert during critical
damage calculation. Prefer composition around the captured configured calculator
and delegate critical/final damage plus defense to it. A successful build does not
validate these authored attributes; exercise actual damage before packaging.

For projectile-specific policy, inspect the native collision-to-damage call.
ACF forwards the shooter as DamageDealer, and penetration damage is shared by
arrows, grenades and cannonballs. Do not infer arrow identity from that class or
the shooter's currently equipped weapon. `UProjectArrowImpactContext` records the
native arrow's pre-damage collision and consumes it once for the same receiver,
dealer, frame, bone and impact position. No native damage type/FX mapping changes.
Match ACF's reconstructed hit position: DamageHandler uses the incoming
FHitResult.Location (sphere center), not ImpactPoint (surface). Comparing the
surface point rejects valid projectile identity after a sphere sweep.
Use `RunArrowHitPIE.py` or packaged validation `-Arrow` to exercise deferred native
arrow spawning and physical flight above the light threshold, plus strong-hit
regression. The fixture changes only its disposable arrow's base damage.
On a pursuit timeout, inspect its recorded target, AI/action tags, path status,
movement hold and native pause flag before changing gameplay. A successful
recovery followed by an unsuccessful target relocation does not by itself prove
that the reaction gate remains locked. Preserve failed receipts; an intermittent
failure without state evidence has an unverified cause, even if a later run passes.

Project entry points: `Tools/Animation/AuthorZombieHitLayers.py`,
`RunZombieHitPIE.py`, `ValidateZombieHitPackaged.ps1`, and console
`Project.ZombieHit.QA`. Read
`Docs/Migration/ZombieHitMovementCalibration.md` for the policy, measured gates and
current evidence status before reusing this specific calibration.

Build, compile touched owned Blueprints, inspect one real visual reaction and run
representative PIE and packaged gates. Record exact paths, measured before/after
results and limitations in Docs/Migration. Add verified project-specific entry
points here when the repair has passed; do not label unfinished evidence PASS.
