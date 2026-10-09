# Onboarding and scalable appearance

## Audit before conversion

Record the exact object paths for the candidate, imported skeleton, UEFN reference
mesh/skeleton, physics asset, materials and deformers. Snapshot dirty-package state
separately from disk hashes; do not save unrelated user edits. Capture candidate
and protected baselines before mutation. Inspect skeleton referencers before accepting
replacement of the imported skeleton; report any dependencies outside the intended body.

Obtain mesh-local reference bones, parents, reference pose and skin influences.
The native MCP `SkeletalMeshTools.get_bone_names` implementation may enumerate the
assigned shared skeleton; it is not proof that those bones exist in the mesh.
Inspect its installed implementation before choosing a hierarchy diagnostic.
Use a discovered read-only editor API or project-owned helper for mesh-local evidence.

Compare morph names and counts before/after, actual deltas/LOD behavior when applying
the conversion, material slots, physics constraints, bounds, sockets and branch weights.
Same bone names do not prove equal parents or transforms. Same morph names do not
prove equal shape, acceptable limits or fitting. Use `ue5-skeletal-branch-audit` for
extra deform branches and `ef-clothing-unisex-workbench` for clothes/coverage.

## Conversion execution

Apply the user's New/local-replacement convention in SKILL.md only to an imported,
unconverted candidate. Discover the exact UEFN asset path instead of selecting the
first mesh containing UEFN. Current reference mesh:
`/Game/FullSample/GASP/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin`.

Inspect the installed Daz converter before automating. As audited on 2026-10-08,
`DazToUnrealBlueprintUtils.cpp:527-558` calls CommitSkeletonToSkeletalMesh, ignores
the result of MergeAllBonesToBoneTree, assigns TargetSkeleton regardless, adjusts
retarget modes on that skeleton and invokes CreateFromSkeletalMesh on the physics asset.
This can affect a shared skeleton. The project-owned `ConvertGenesis9BodyReport`
adapter isolates the target, preserves the local skeleton destination and rejects
failed commit/merge or changed morph deltas, LODs or materials. It also rebuilds
only the candidate's imported physics asset. Its G9 recipe is generated from the
installed source, SHA256-pinned in `EFGenesis9ConversionRecipe.inl`; review/regenerate
after a plugin update with `Tools/BodyPresets/GenerateConversionRecipe.py`.

Use an exposed editor API when available; if UI is required, use the computer-use
skill and only supported surfaces. If native UI is unavailable and no callable API
exists, state the automation limitation; do not claim the menu was clicked.

After conversion, verify the candidate's actual skeleton pointer, parent chains,
branch deformation, morph inventory, physics and saved state after reload. A local
skeleton can be retained, but compatibility with UEFN animations, AnimBP, IK, slots,
sockets, retarget settings and pose followers must be validated explicitly. Bone
compatibility metadata alone is not validation. Avoid mutating the shared skeleton
to accommodate an unverified candidate.

## Add a player preset

Inspect these owners under `Plugins/EFCharacterCreation/Source/EFCharacterCreationRuntime`:

- `EFCharacterCreationTypes.h`: FCharacterSkeletalMeshOption,
  FCharacterGenderMeshOption, FCharacterCustomizationState and saved presets.
- `EFCharacterCustomizationComponent.cpp`: BuildBodyMeshOptions, ResolveGenderMesh,
  InferGenderForMesh, ApplyBodySkeletalMesh, BuildMorphEntries, ApplyState and CaptureState.
- `UI/EFCharacterCreationRootWidget.cpp`: body option selector and gender buttons.

The shared `EFBodyPresetCatalog` now supplies stable BodyPresetId, label/order,
gender, soft mesh reference, optional MorphPhysicsAsset, morph defaults/ranges/profile,
NPC eligibility and selection weight. `/Game/_Game/Data/CharacterCreation/DA_BodyPresets`
contains Male.01, Male.02 and Female.01. Both UI and NPC selection consume it.
Keep imported body folders separate and preserve stable IDs when updating labels.

Filter choices by gender; preserve the chosen variant when reselecting that gender.
Migrate old saves by matching SelectedBodyMeshAsset to the new stable ID. Keep a
validated default when an asset is unavailable. Store versioned resolved morph values
and preserve existing preset files. Distinguish a base-body preset from a user-saved
appearance preset containing body + morphs + colors/hair.

Use the existing appearance swap path and its GetOnBodyMeshesWillChange event;
do not set just one Player mesh component. Preserve animation ownership, clear stale
material/morph caches, rebuild morph entries for the new body, and restore clothing
coverage before reacquiring it. Clamp transferable values to the new body's profile;
reset absent morphs. Confirm the current Player component roles live before editing.

Register the body once in
`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`, compile its generated
fits and check anatomy coverage and DQS. A registered body is not automatically a
certified fit. Test equipment, hair, tattoos and appearance consumers when relevant.

## NPC variety

Reuse `ProjectEnemyVisualVariationSubsystem`, `Settings` and `Selection` under
`Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters`.
The subsystem now chooses a gender-filtered body and supported morph profile, preserving
the existing level/content policies. `ProjectNPCAppearanceState` is an ALS-saved actor;
it stores resolved appearance and seed by identity. Explicit EF.AppearanceId.* tags
win; placed actors use a normalized path; dynamically spawned actors receive a GUID tag
that ALS serializes. Generic NPCs can opt in with EF.BodyVariation. Player and client-only
worlds are excluded. NPC saved state wins over new random generation.

Select a weighted eligible body first, await its load/application, then select only
supported morphs from that body's profile. Keep NPC identity independent of mesh path.
Use persistent NPC identity + saved seed + generator version, and save the resolved
BodyPresetId/morph state for returning characters; do not reroll on streaming or load.
Let existing saved appearance win over spawn randomization. Keep the player's choices
out of the NPC generator. Preserve existing content-policy checks.

For natural variety, use a few compatible body/face dimensions with tested correlated
ranges. Do not uniformly randomize every morph, expressions and correctives together.
Five meshes per gender is a configurable pool size, not a hard-coded architecture.
Use soft references, loading/cancellation handling, appropriate LODs and measured NPC
memory/frame costs. Do not load all high-resolution DAZ bodies for every NPC.
If networking is required, replicate the resolved appearance/seed authoritatively;
do not assume the existing random stream makes peers agree.

## Reusable command

Create a request at `Saved/BodyPresetsQA/onboard-request.json`, or point
`EF_BODY_ONBOARD_REQUEST` at another JSON file before starting the editor script.
`Tools/BodyPresets/OnboardBody.py` defaults to read-only inspect. Example fields:

```json
{
  "mode": "inspect",
  "mesh": "/Game/DazToUnreal/Male3/Male3",
  "gender": "Male",
  "preset_id": "Male.03",
  "label": "Preset 3",
  "npc_weight": 1.0,
  "allow_npc": true,
  "npc_neutral_morphs": {"DK_Flacid 03": 1.0, "DK_Erection": 0.0},
  "author_secondary_physics": true,
  "compile_clothing": true,
  "genital_bone_branches": ["shaft_01", "scrotum"],
  "material_slot_aliases": {"Genesis9_GP_Torso": "None"},
  "morph_profile": [
    {"name": "Body Heavy", "minimum": -1, "maximum": 1,
     "npc_minimum": 0, "npc_maximum": 0.3}
  ]
}
```

Use discovered names/branches; the example is not proof that another mesh has them.
The default skeleton path is `<mesh>_Skeleton` in the same folder; specify `skeleton`
when the import uses another local name. After reviewing inspect evidence, use
`mode: prepare` under the user's onboarding authorization. The script snapshots the
candidate, catalog and Director; converts raw G9 or binds already-converted input;
registers a single catalog row and Director target; optionally authors secondary
physics; compiles stale clothing fits and synchronizes V5 runtime data.
It does not certify visual correctness merely because preparation succeeded.

Unknown required morphs, changed stable-ID ownership, a shared skeleton/physics asset
or an unsupported raw hierarchy are errors. Review the generated report and dirty
packages before retrying a partial authoring failure. Python must use
`set_editor_property` for editor-only/BlueprintReadOnly authoring fields such as
Director bodies; do not assume direct assignment is supported.

Fresh conversion is G9-only. Already-converted candidates use hierarchy validation
instead of reapplying the orientation recipe. Preserve the source DAZ export.
Run `ValidateOnboardingWorkflow.py` only as the scoped Male2 regression fixture;
its cleanup list is restricted to assets created by FreshImportConversionQA.

## Validation and evidence

For integration, verify Male1 -> Male2 -> Male1 and both gender directions; confirm,
cancel, reopen, save/load and legacy save migration. Exercise missing optional morphs,
animation transitions, locomotion, combat, IK, ragdoll, morph extremes, clothes and
visual seams. NPC tests cover same body with different morphs, stable reload identity,
invalid/missing preset fallback and multiple simultaneous NPCs.

Select checks relevant to the change; the preceding list is a coverage menu, not
an instruction to run every scenario on every task. Set an outer deadline before
launching automation and record counts, elapsed time and assertions. The NPC skill
provides current bounded defaults. Do not expand passing coverage without a concrete
failure or an uncovered requirement. Neutral targets use explicit non-random defaults;
different male body families require their own supported mapping, not invented DK names.

Follow target build/launch wrappers. Keep every gate distinct: build, Blueprint compile,
asset validation, PIE, visual evidence, cook and packaged run. Re-hash protected plugins,
Player, Female, Male, Multiple and Frederick after each actual migration phase.
Capture UEFN skeleton too because conversion targets it. Preserve existing worktree edits.

For a diagnosis-only task, record inspected source/live state and pending runtime
gates without triggering conversion, PIE or a full rebuild just to certify prose.
