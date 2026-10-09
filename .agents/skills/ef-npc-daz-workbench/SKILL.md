---
name: ef-npc-daz-workbench
description: Author, audit and validate NoShellForWinter project-owned DAZ NPC archetypes, spawn morph variation, anatomical rest defaults and ACF animation initialization. Use for NPC roster additions and repairs; imported-body conversion belongs to ef-body-preset-workbench.
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
6. Use SaveGuard for real ALS disk QA. Run cook and the same native smoke in a
   packaged executable with isolated UserDir and an outer process deadline.
   Preserve one successful package, receipts and failed diagnostics.

Every retry needs an observed failure and a concrete correction. Stop expanding
test coverage once the scoped gates pass. Distinguish authoring, static compile,
runtime, visual, persistence, cook and packaged gates; unperformed gates are PENDING.
