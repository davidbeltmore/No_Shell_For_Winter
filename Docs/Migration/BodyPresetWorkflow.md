# Imported body presets

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8. Source project remains read-only.
Master skill: `A:/Skills Codex/ef-body-preset-workbench/SKILL.md`.
Measured results and outstanding gates: [implementation ledger](BodyPresetsImplementation.md).

## Existing catalog

`/Game/_Game/Data/CharacterCreation/DA_BodyPresets` contains Male.01, Male.02 and
Female.01. Display names are editable; persistent IDs must remain stable. The
character creator shows mutually exclusive preset checkboxes directly below Gender
on Info (also available under Body), filters this catalog by gender, remembers the last choice for
each gender, and saves the resolved mesh and preset ID. NPCs use the same catalog,
eligible weights and supported morph ranges. Their resolved appearance is saved
through ALS; loading does not roll a new appearance.

Five meshes per gender requires importing and certifying additional bodies, then
adding catalog rows. It does not require five character Blueprint subclasses.
This task supplies two male bodies and one female body, not ten new models.

## Onboard an imported body

1. The user exports/imports the body from DAZ with the intended morphs. Discover
   its exact mesh, local skeleton and local physics asset under `/Game/DazToUnreal`.
2. Invoke Unreal MCP read-only and the master skill. Inspect current assignments,
   references, required bones, materials, morph names, clothing and secondary physics.
3. Write `Saved/BodyPresetsQA/onboard-request.json` using the schema below. Run
   `Tools/BodyPresets/OnboardBody.py` in the target editor with `mode: inspect`.
4. Under the user's onboarding authorization, change the mode to `prepare` and
   run again. The report contains scoped backups. The G9 adapter uses an isolated
   new skeleton tree and replaces the imported local tree; it never saves the
   shared UEFN skeleton. Already-converted bodies are validated and bound instead.
5. Review the report and dirty packages on any failure before retrying. Do not
   blindly rerun a partially completed preparation. Unknown raw hierarchies need
   a separately reviewed conversion recipe.
6. Certify animation, creator selection/save, NPC persistence, clothes, morph
   extremes, protected hashes and packaged asset loading. A `PREPARED` result is
   deliberately distinct from a complete integration pass.

```json
{
  "mode": "inspect",
  "mesh": "/Game/DazToUnreal/Male3/Male3",
  "gender": "Male",
  "preset_id": "Male.03",
  "label": "Preset 3",
  "npc_weight": 1.0,
  "allow_npc": true,
  "author_secondary_physics": true,
  "compile_clothing": true,
  "morph_profile": [
    {"name": "Body Heavy", "minimum": -1, "maximum": 1,
     "npc_minimum": 0, "npc_maximum": 0.3}
  ]
}
```

Use actual imported morph names. Optional `skeleton`, `material_overrides`,
`material_slot_aliases` and `genital_bone_branches` must be grounded in inspection;
do not copy body-specific branch names into unrelated bodies. Missing materials
require an explicit candidate material mapping. Existing catalog IDs cannot be
silently reassigned to another mesh or gender.

The player has an animation-source `CharacterMesh0` and a visible retarget follower
`SkeletalMesh`. Route changes through `SelectBodyPreset`/the customization component;
changing only the follower's asset misses ACF state, morphs and appearance consumers.
Explicit per-mesh physics settings take precedence over the catalog profile.

## Supported test entry points

- `RunRuntimeQA.ps1`: actual HUB PIE selection, evaluated pose, movement, physics,
  and actual preset checkbox delegates, gender memory, Apply/Cancel and reopen. Guarded user saves.
- `RunVisualQA.ps1`: six garments across three bodies, coverage ownership, movement
  and extreme morphs; compile seven affected Blueprints and capture images.
- `RunNPCSaveQA.ps1`: twelve real NPCs, explicit and generated identities, diversity,
  player exclusion, disk ALS save/load equality. Check process exit separately.
- `FreshImportConversionQA.py`: isolated fixture from the existing Male2 FBX;
  exact morph-delta preservation and repeated-conversion rejection.
- `ValidateOnboardingWorkflow.py`: cold reload and two idempotent Male2 preparation
  runs. Its referencer-audited cleanup is limited to its own three QA assets.
- `Package-BodyPresets58.ps1` then `Validate-Packaged58.ps1`: fresh HUB development
  cook/package and isolated packaged runtime evidence. This is not an all-map
  shipping release or a network/crowd performance certification.
- `CaptureBaseline.py before|after`: protected asset/plugin hashes; keep the
  original before receipt immutable. Never replace a baseline to conceal drift.

Native catalog contracts use `NoShellForWinter.BodyPresets.CatalogAndState`.
Build and launch through the project migration wrappers so DAZ plugin receipts
remain enabled. Generated reports live under `Saved/BodyPresetsQA`.

Warm body changes currently load/apply synchronously. Measured calls and visual
sampling limits are in the ledger; no seamless crowd-streaming claim is made.
Non-G9 conversion, hair fitting and network replication require their own work.
