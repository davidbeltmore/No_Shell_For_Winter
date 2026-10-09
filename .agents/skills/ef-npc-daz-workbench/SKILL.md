---
name: ef-npc-daz-workbench
description: Author, audit and repair NoShellForWinter owned DAZ NPCs, inherited ACF gameplay defaults, interaction menus, navigation, spawn morphology and animation initialization. Use for NPC roster additions and gameplay regressions; imported-body conversion belongs to ef-body-preset-workbench.
---

# DAZ NPC workbench

Start with `noshellforwinter-unreal-mcp`. Inspect live target context read-only before
authoring. Use `ef-body-preset-workbench` when changing the body catalog. Source game,
installed ACFU, DAZ plugins and protected player/body assets remain read-only.

## Ownership and extension points

- `Tools/NPC/Roster.json` maps source archetypes to owned variants. It is authoring
  provenance, not a runtime source-class registry. Add an archetype there, then run
  the bounded author/audit path. Preserve established Male/Female public paths.
- `/Game/_Game/Characters/Framework`: owned copies of character Blueprint ancestors.
  Native ACF C++ and shared component/animation libraries remain dependencies; this
  is not a fork of the entire marketplace plugin.
- `/Game/_Game/Characters/Male` and `Female`: spawnable variants, with
  `EF.BodyVariation` authored before spawn processing. Do not put explicit
  `EF.AppearanceId.*` identities on class defaults.
- `/Game/_Game/Characters/Data`: owned initializer DataAssets and `DA_NPCRoster`.
  Keep `MeshComponents` consistent with the Blueprint mesh and AnimClass: otherwise
  ACF's InitializeComponent restores Manny or the wrong animation before BeginPlay.
- `/Game/_Game/Characters/Animation`: owned humanoid/zombie AnimBlueprints.
  Preserve role-specific abilities, equipment, AI and dialogue fragments.
- One shared `DA_BodyPresets` feeds player and NPC consumers. Extend its rows and
  per-body morph profiles, not gender-specific C++ switch statements.

The loaded catalog is retained by `UEFBodyPresetCatalogCache` (EngineSubsystem).
Do not put a hard runtime asset cache on a settings CDO: packaged builds can put
that CDO in the disregard-for-GC set and crash on the first collection. A short
PIE or appearance pass does not expose this. Also avoid static strong UObject
pointers whose destructors run after UObject shutdown. Validate normal packaged
collection and clean process exit when changing catalog ownership.

## Appearance contracts

Spawn variation uses a stable identity seed, weighted same-gender body pool,
explicitly enabled morph dimensions and skin variation. Saved resolved appearances
win over new rolls. `EF.BodyPresetFixed` keeps the authored body but still varies
morphs/skin. Placed identities are normalized actor paths; dynamic IDs are ALS-saved
tags. Never copy an instance's identity tag onto another NPC.

Male rest is a body-profile default, independent of optional active presentation.
The current bodies use `DK_Flacid 03=1`, `DK_Erection=0`, with `bNPCNeutral=true` and
randomization disabled for both. `MigrateNeutralDefaults` upgrades generator-v1 NPC
records once, preserving other morphs, skin and body ID. Generator-v2 saved custom
states are not repeatedly overwritten. Do not reset all saved morphs to repair rest.

`Tools/BodyPresets/OnboardBody.py` recognizes the current DK pair or requires an
explicit `npc_neutral_morphs` mapping for a new male body. Verify actual targets and
visual rest; never infer support from a mesh name.

## Animation traps

An AnimInstance pointer alone proves nothing. SkillGuy's original simple template
has empty moveset/overlay tables and no authored initializer. Use the owned
initializer and a complete animation template. Skeleton-changing body swaps also
recreate AnimInstances: the project ACF bridge restores tables and the movement
component cache. Check actual bone motion and cache identity after a swap.
Use named Python Rotator arguments (`pitch=0, yaw=-90, roll=0`): positional
construction produced pitch -90 and horizontal bodies even while bone motion passed.
Require upright head/foot separation and a living, non-ragdoll pawn before accepting
the pose-delta assertion. Inspect the image; a numeric pass is not visual evidence.

The MetaHuman-derived DAZ variant uses a complete DAZ body. Its MetaHuman face,
clothing and groom payloads must not render over that body. Do not claim DAZ facial
lip-sync from retaining a MetaHuman-specific component.

## Durable authoring

Reparenting a copied Blueprint changes the owning SCS node keys. Existing inherited
component overrides can silently disappear even when compilation passes. Names,
dialogue arrays, marker data and other gameplay defaults then fall back to base
values. After each reparent, call `SyncOwnedNPCLogicDefaults` against the source
archetype and compile. It rebuilds override keys against the owned ancestors,
preserves DAZ mesh/initializer overrides and rejects unhandled instanced properties.
Combined-animation condition arrays are duplicated into the owned component; deep
comparison checks their values instead of requiring shared object pointers. For the
native reflection API, use `Identical_InContainer(From, To, 0, PortFlags)`; the third
argument is an array index. Run a read-only all-variant audit before applying a
repair so unsupported property families are reported together.
Do not edit a raw inherited template that still belongs to a vendor package.

Run `Tools/NPC/AuditLogic.py` for all 40 variants after authoring. Zero effective
component differences and zero errors are required. `RepairLogic.py` is the bounded
repair entry point for an existing roster. Actor names alone do not prove dialogs.
Do not rewrite shared ACF dialogue components to compensate for lost defaults.

Use `acfu-installation-audit` when version claims conflict. The measured installed
release on 2026-10-09 was 4.4.2; re-audit instead of hardcoding that fact forever.

`UProjectNPCNavigationSubsystem` adapts owned NPC ancestry on possession when the
pawn-selected NavData differs from Detour CrowdManager's NavData. It uses base path
following on that mismatch, retaining native controllers and behavior trees.
Do not issue repeated MoveTo commands to hide an orphaned active AITask: Detour can
abort a mismatched path synchronously before the task binds its completion delegate.
Log both nav-data names and check actual displacement. The existing Calysto-only
compatibility path does not cover ordinary HUB NPCs. Volume-only navigation also
requires `bGenerateNavigationOnlyAroundNavigationInvokers=False`; rebuild and verify
projection on the actual supported agents without altering the user's floor/bounds.

Use `set_editor_property` when assigning struct arrays, then explicitly save the
scoped owned package (`only_if_is_dirty=False`). Python can update an array in
memory without marking its package dirty; a live PASS must be confirmed by cold
load or the cooked runtime. Never force-save all loaded packages.
Blueprint compilation reinstantiates descendants and CDOs: rebuild reference maps
after each compile. Compare actor GUIDs with `Guid.to_string()`, not Python object
representations, which include wrapper addresses. Keep one-off recovery scripts
in Saved, not the reusable authoring path.

## Bounded workflow and evidence

Read `Docs/Migration/NPCDAZ.md` for measured gates and exact scope. Receipts are in
`Saved/NPCDAZ`; versioned evidence belongs in `Docs/Migration/Evidence/NPCDAZ`.

1. `Inspect.py`/`InspectData.py` are read-only. `Protection.py before` creates an
   immutable task baseline; reuse `after` for subsequent phase comparisons.
2. Cold builds use the project's protected Build/Launch wrappers. Never launch
   during a Game wrapper's temporary UBT descriptor exclusion.
3. `Author.py` has a 180-second internal ceiling; run through the localhost editor
   Python channel with `Bounded.py author 210 -- ...`. It preserves scoped backups,
   copies/reparents character ancestors and remaps only owned packages. It does not
   consolidate assets, rewrite vendor packages, or overwrite existing framework logic.
4. `Audit.py`: 120-second structural gate, all variants compile and agree with
   their initializer, gender, animation and unique-identity contracts.
   `ReplaceHub.py` migrates source NPC instances in HUB through the native editor
   replacement API. It preserves object path/GUID, transform and common instance
   properties, repairs level references, then restores the owned DAZ initializer.
5. `RunPIE.py`: native `Project.NPCDAZ.QA`, 12 representative NPCs, 90-second native
   and 120-second host ceilings. Require actual pose deltas, neutral morph values,
   appearance restoration, and visual review. Do not describe this as exhaustive AI,
   combat, dialogue or network certification.
   For logic changes also use `RunLogicPIE.py` / `Project.NPCLogic.QA`: actual E
   input, detected actor/name, dialogue state and natural enemy perception/path/
   movement, with 150-second native and 180-second host ceilings. Never stop the
   tested brains or force targets/MoveTo and then claim native pursuit passed.
   Python navigation helpers exposed as class calls can target a CDO and ensure;
   use the actual world's NavigationSystem instance (or the native QA command).
   An outer Python process returning does not mean an asynchronous PIE test passed;
   require a fresh runtime receipt and the host cleanup result.
   ACF's Blueprint dialogue event can omit the native parent flag update. Measure
   `IsDialogueStarted` on the runtime graph and the active visible dialogue widget,
   not only the participant's `IsInDialogue` flag. Select the native service response
   and require its exact menu class; HUD/Navbar reactivation is not service success.
   Close only that menu during fixture cleanup so subsequent E input remains valid.
   Provision missing dynamically spawned controllers as ACFGroupAIComponent does.
   Run pursuit in a clear corridor inside the existing nav volume: other hostile
   actors can win threat selection. Record the actual target, active BT task,
   blackboard and path-following status on failure. Do not reset threat to make a
   player-target assertion pass. A native comparison must include the source
   initializer when the bare source Blueprint has none.
   Derive movement expectations from the source role. The installed ranged behavior
   attacks above 100cm and probabilistically retreats below 350cm; a stationary
   ranged attack at 1200cm is not a chase failure. Measure melee approach and ranged
   spacing separately, using player approach as stimulus, never forced AI states.
   Start appearance settling deadlines after synchronous asset loading; cold class
   loads can consume the entire interval if it starts before spawning.
6. Use SaveGuard for real ALS disk QA. Run cook and the same native smoke in a
   packaged executable with isolated UserDir and an outer process deadline.
   `ValidatePackaged.ps1 -StagedRoot <package>/Staged -Mode Logic` has a 210-second
   ceiling; `-Mode Appearance` has 150 seconds. Require both process exit zero and
   the fresh native PASS receipt, then inspect the saved menu/roster images.
   Preserve one successful package, receipts and failed diagnostics.

Every retry needs an observed failure and a concrete correction. Stop expanding
test coverage once the scoped gates pass. Distinguish authoring, static compile,
runtime, visual, persistence, cook and packaged gates; unperformed gates are PENDING.
