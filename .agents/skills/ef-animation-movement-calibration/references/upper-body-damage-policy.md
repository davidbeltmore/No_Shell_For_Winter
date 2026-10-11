# Reusable upper-body reaction policy

Use when the user wants a specified attack or damage category to flinch above the
hips while a moving character's legs and navigation continue normally. This
recipe covers policy selection and validation; it does not automatically enable
the policy on every character or turn every large hit into a light reaction.

## Find the effective source

Record the receiving pawn/tag, configured calculator Blueprint, actual
FACFDamageEvent, response action, damage class/tags and attack origin. Determine
whether the request means every event of one damage class, a gameplay tag, or
only one weapon/projectile. Damage classes can be shared by unrelated weapons.
ACF projectiles report the shooter as DamageDealer, not the projectile actor.
The current equipped weapon is unreliable for an already launched projectile.

When identity is needed, prefer explicit project-owned attack metadata. For native
arrows, reuse ProjectArrowImpactContext: a pre-damage collision observer supplies
one-shot receiver/dealer/frame/bone/position context. Preserve its exact native
Location-to-ImpactPoint contract. Add another verified source matcher only after
inspecting that source's collision ordering; do not assume every projectile uses
the same pipeline. Do not change native damage classes to make identification easy.

## Select the response without changing damage

Keep actual damage, defenses, critical calculation and hit/FX pipeline delegated
to the captured configured native calculator. ProjectZombieDamageCalculator is
the current owned composition point; its generic light ceiling is 25 and arrows
are an explicit exception. Extend only the requested source policy. Avoid a bare
native calculator subclass that drops Blueprint-authored GAS attribute mappings.
Honor block/parry/special responses before selecting Actions.HitLight; retain
alive, positive damage, immortality and action availability checks. A lethal hit
must still die. Keep strong sources on their existing full-body response.

For several future policies, put measured source-to-response rules in an owned
profile rather than adding scattered weapon-name checks. Expose only the needed
classification and response choice; animation classification must not rewrite
damage magnitude. The present arrow implementation is deliberately narrow and
does not yet provide a general editable damage-policy asset.

## Reuse the torso layer

Owned assets: /Game/_Game/Characters/Animation/ABP_DAZZombie and
/Game/_Game/Characters/Animation/Hits/AS_ZombieReactions.
Inspect actual paths and graph before modifying them. Existing Actions.HitLight
uses the existing UpperBody slot above spine_01 (branch depth 3, mesh-space rotation) over the
cached native full pose; pelvis and legs remain locomotion. The owned light clip
has root motion disabled and its action does not stop the behavior tree. Verify
bone ancestry on a new mesh. Do not move the layer root to pelvis: that includes
the legs. Do not copy this AnimBP onto another character without verifying its
native locomotion/action contract and required slots.

## Bounded acceptance

Use the real requested attack. If it should bypass the light ceiling, test damage
above 25, not a low-damage substitute. Current two-zombie entries are
Tools/Animation/RunArrowHitPIE.py and ValidateZombieHitPackaged.ps1 -Arrow.
For another source, extend the disposable fixture's source injection; keep the
shared assertions and cleanup. Never classify a synthetic test as that source
by manually forcing a montage or action tag.

Measure actual positive final damage and selected action, >=4 reaction samples,
>30 cm walking, thigh delta >0.05 rad, torso delta >0.02 rad and no navigation
hold. Retain the unmodified strong-hit comparison (<=3 cm slide, <=5 cm/s),
interruption, >100 cm pursuit recovery, and lethal-hit translation checks.
Current ceilings: native 90s, PIE host 120s, packaged process 180s. Verify a real
rendered pose; screenshots alone cannot prove maintained locomotion. Extend
scope/thresholds only when the new authored action requires it and record why.

Record source asset/symbol, policy match, damage/action/movement metrics, failed
receipts and known limits in Docs/Migration. Build, compile owned Blueprints,
representative PIE/visual/cook/packaged gates and protected hashes remain required.
Do not repeatedly run broad suites after these gates pass without a new failure
or change. Keep source identity and upper-body layering independently reusable.
