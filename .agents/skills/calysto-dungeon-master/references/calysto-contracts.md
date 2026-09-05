# Calysto authoring and runtime contracts

The complete V7 contract is the
[master plan](../../../../Docs/Migration/Calysto_Dungeon_Director_V7.md).
Read it in full for implementation. It describes required behavior, not a
completion receipt. Use [operator-control.md](operator-control.md) for actual
current controls and [validation-gates.md](validation-gates.md) for timed proof.

## Authority

V7: `UEFCalystoDungeonDirectorAsset`, internal schema 7, asset
`/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector`.
Separate authored data, immutable compiled configuration, floor request, attempt,
room/surface context, reserved manifest and realized result/diagnostics.
Public runtime interfaces are stable and unversioned.

The 2026-09-05 preflight found the current V6 input:
`/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset` and
`/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy`.
Check actual Config/loaded class again. Preserve it read-only for migration;
never compile V7 through V6 or provide a runtime fallback. Existing native test
success does not establish gameplay.

EFProceduralRuntime owns policy, decisions, run/floor transactions and leases;
EFProceduralPCGRuntime owns the native adapter and structural/navigation/
reservation/materialization checks. EFProceduralACFURuntime owns progression
door identity. EFLevelFlow consumes the verified entry transform.
EFProjectSystems supplies thin gameplay, outcome, companion and Harness bridges.

## Authoring and probabilities

Clean native Details: Dungeon, Dungeon Styles, Room Themes, Advanced. Style and
Theme settings are inline. Keep named arrays, percentages, conditional fields,
Undo/Redo, asset pickers and exact-field validation. Put technical IDs, hashes,
schema and diagnostics in the on-demand inspector. Every exposed functional
control requires a runtime behavioral test.

One Style per floor. For N eligible rooms, independently rank one guaranteed
room and roll Bernoulli(0.25) on each remaining room:
`1 + Binomial(N - 1, 0.25)`. N=0 rejects before content commitment; N=1 has
exactly one Theme. Start/End/Critical/Progression rooms are excluded.
Conditional Theme weights do not change presence.

Shared decision library: finite eligibility -> feasible placements/budgets for
supported amounts -> Chance -> conditional Amount -> optional populated-tier
rarity -> entry Weight -> frozen reservations -> strict realization.
Limits add no hidden probability. Only intentional native decoration may author
Empty. Fixed/uniform/triangular distributions use their actual named mathematics;
inline depth curves have exact endpoints and default linear interpolation.
Filter depth, roles, cooldowns, surfaces, footprints, spacing and remaining
capacity before commitment. Do not sample impossible amounts then truncate.
Include Winter in populated-tier normalization without implicit Nothing.

Stable identities persist independently of labels/order and are regenerated
appropriately for new entries/duplicates without changing unrelated entries.
Typed roles/budget membership/gender/archetype/lifecycle never derive from names.
Theme modes: Inherit uses Style; Extend edits entries while preserving inherited
probabilities/limits; Replace owns locally; Block disables locally. Separate
explicit probability/count overrides. Floor-wide limits always apply.
Adaptation off means exactly zero influence from every adaptive input.

## Native construction, entry and placement

Protect BP_MassiveDungeon, PCG_MassiveDungeonMaster and native DataAssets/graphs
under /Game/Calysto. Localize vendor classes/properties/pins in one adapter,
validate capabilities, strongly retain transient schemas and issue one root
GenerateLocal per attempt. Preserve UE 5.8 SoftObjectPath metadata. Required
structural pieces never disappear via random Empty.

Inspect actual mesh instances/transforms, collision profile/body/navigation
relevance, structural bounds and room ownership. Verify unique Start/End, floor
contact under entry, capsule clearance, nav bounds covering settled geometry,
relevant usable nav data/tiles and complete Start-to-End approach route. Idle
build queue is insufficient. Delayed geometry/nav remains Pending under bounded
observation; no repeated full rebuilds or expanding projection search. EFLevelFlow
uses the Director's one entry transform without searching another room.

Use bounded native room/surface candidates with footprint/spacing, doorway
protection, clearance and appropriate nav constraints; cover valid floor space.
All world entries expose Floor, Wall Bottom/Middle/Top, Corner Bottom/Middle/Top
or Roof placement. Pickups/chests default Floor; torches Wall Middle with 2 cm
variation. Avoid array-order favoritism. Custom PCG declares input/output pins,
footprint, surfaces, dependencies and deterministic seed; reject unsupported
contracts, untracked spawning, topology/progression edits or regeneration.

## Materials, decals and loading

Theme override > selected Style separately on Floor/Wall/Roof. Empty explicit
override fails; no silent native fallback or per-room MID. Verify actual
generated slots, including shared boundaries. Initial general grey, Forge
instancing/Nanite-compatible orange and Shrine display blue.

One blood decal owner/pool, capacity 24, active 8, maximum one/room, initial
Floor 3 / Wall 4 / Roof 1 caps. Style 10%, Forge replacement 25%, Shrine blocked.
Protect Start/End/progression, doors and main route. Keep intended RealisticBlood
T_Splat_04/T_Splat_N_04 dependency closure; confirm exact paths with Asset Registry.
No Demo/Blueprint/Niagara creep; preserve native Calysto torch effects.
Real distance culling is distinct from honestly named screen-size fading.
Unselected absence is legal; selected/reserved failure rejects the attempt.

Async phases: shared session dependencies, reachable structural/Theme visuals,
frozen gameplay payloads, selected decal/extensions. Deduplicate across phases,
make equal requests idempotent, reject stale callbacks, retain proper leases.
Prewarm supported material PSOs/native torches; measure geometry, navigation,
shader preparation and gameplay spawning separately.

## Transaction and recovery

Preflight -> Native Generation -> Structural Verification ->
Navigation/Reservations -> Realization Verification -> Commit -> Player Release.
Independent preparation may overlap but every selection must verify before commit.

At most four total attempts share one 30-second request deadline. Deterministic
seeds derive from immutable run/floor/attempt identity, never callback request
IDs. Only one attempt active. Pending waits without consuming another attempt.
Settled recoverable spatial failure rejects, cleans up and reseeds.
Configuration/resources/infrastructure failures do not retry another seed.
Cancellation releases ownership without outcomes or recovery.

Before next attempt remove actors/instances/decals/reservations/nav registrations,
callbacks and transient references. Keep selected Style, run/floor/rules/budgets,
pre-floor companions/outcomes unchanged. Never silently drop or substitute,
advance progression or commit inventory to make recovery succeed. Exhaustion
protects player with concise English error and Retry / Return to HUB.
Report requested, feasible, rejected and accepted distributions separately.

Retire only after candidate acceptance and exact registry/config/code/tool/package
audit; editor-only exact asset deletion, then cold rebuild/fresh package and
final-tree traversal. Remove old classes/wrappers/redirects after consumers
migrate. Archive evidence outside cook, preserving useful unversioned payloads.
