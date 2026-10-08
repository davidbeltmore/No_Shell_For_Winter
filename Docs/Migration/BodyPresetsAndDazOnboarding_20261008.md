# Body presets and DAZ onboarding audit — 2026-10-08

> Historical initial audit. Implementation and subsequent corrections are tracked in
> [BodyPresetsImplementation.md](BodyPresetsImplementation.md). Statements below about
> missing functionality describe the initial baseline, not the current working tree.

## Scope and result

Analysis, live read-only inspection and reusable skill creation. No conversion,
asset save, runtime implementation, build or PIE was performed. Male2 is not yet
certified or registered as player preset 2. A scalable extension is feasible using
existing project-owned systems, but is not already a complete import-and-publish pipeline.

Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8).
Source: `D:/Projects UE5/LustAsDeadlySin`, untouched; no source-tree reads needed.
Base commit: `6a0fc12f4c88045cba8449de5d53ef8ec5c2e76c`.
Pre-existing changes: Config/DefaultGame.ini, Content/_Game/Hub/HUB.umap,
untracked Male2 and Chest content, ChestMoveRollback.md, ProjectSizeCleanup_20260924.md
and Invoke-SafeGeneratedCleanup.ps1. Preserved.

Evidence snapshot: [live-inspection.json](Evidence/BodyPresets_20261008/live-inspection.json),
[conversion-excerpt.log](Evidence/BodyPresets_20261008/conversion-excerpt.log),
[user-validation.txt](Evidence/BodyPresets_20261008/user-validation.txt).
MCP endpoint: `http://127.0.0.1:8000/mcp`; target UnrealEditor process observed.
Read-only native calls: list_toolsets, describe_toolset, SkeletalMeshTools.get_skeleton,
get_morph_target_names, get_physics_asset; ObjectTools.get_properties on settings;
AssetTools.get_referencers. No mutating editor tool calls.

## Actual support

| Feature | Audited behavior | Remaining work |
|---|---|---|
| Body replacement in runtime | EFCharacterCustomizationComponent::ApplyBodySkeletalMesh updates body components, broadcasts pre-swap hook, resets materials and rebuilds caches/morph entries | Certify each new body and animation/physics path |
| Saved body and morph state | FCharacterCustomizationState saves SelectedBodyMeshAsset and morph state; FCharacterPresetData stores it | Stable catalog ID/version and legacy mapping for scalable body definitions |
| Player body selector | BuildBodyMeshOptions builds a global skeleton-compatible list; UI Body section exposes previous/next when more than one option exists | Filter by gender and add labeled Male preset 1/2 |
| Gender | ResolveGenderMesh returns first matching configured entry; InferGenderForMesh matches registered paths | Multi-variant selection/default policy; avoid NotApplicable for a new body |
| Live configuration | GenderMeshOptions has Male and Female; BodyMeshOptions has only Female | Male2 is absent from both |
| NPC variation | ProjectEnemyVisualVariationSubsystem handles configured enemy classes, existing and spawned actors; standard path selects one allowed morph plus skin brightness, with extra optional groups | Weighted body pool per gender, coordinated multi-morph profiles and persistent NPC appearance |
| Clothing | Existing Director registers multiple bodies and generates garment variants | Register and visually certify Male2's fits/coverage |

Source locations:

- `Plugins/EFCharacterCreation/Source/EFCharacterCreationRuntime/Public/EFCharacterCreationTypes.h`
- `.../Private/EFCharacterCustomizationComponent.cpp`: RandomizeAll (223), BuildBodyMeshOptions (956), ResolveGenderMesh (1457), InferGenderForMesh (1476), ApplyBodySkeletalMesh (1542 vicinity).
- `.../Private/UI/EFCharacterCreationRootWidget.cpp`: body selector around 1284; gender and mesh callbacks around 1901–1971.
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters/ProjectEnemyVisualVariationSubsystem.cpp`: TryApplyVariation, standard morph roll and brightness around 450–522.
- `ProjectEnemyVisualVariationSelection.cpp` supports random-stream overloads, but the observed subsystem standard call does not pass a persistent per-NPC seed; brightness uses FMath::FRandRange. Do not infer stable reload appearance.

## Male2 and the validation error

Candidate: `/Game/DazToUnreal/Male2/Male2.Male2`.
Local imported/replaced skeleton: `/Game/DazToUnreal/Male2/Male2_Skeleton.Male2_Skeleton`.
Actual live assigned skeleton:
`/Game/FullSample/GASP/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin`.
Physics asset: `/Game/DazToUnreal/Male2/Male2_PhysicsAsset.Male2_PhysicsAsset`.
The local skeleton returned zero registry referencers during this audit; registry
references are not a complete proof about every transient editor object.

Live morph names: Male 415, Male2 416, Female 446. Male2 includes all Male names
plus `Mouth Realism`. This proves the name inventory, not identical deltas, geometry,
LOD behavior or visual correctness.

The pasted validation list contains one explicit incompatibility error. The other
`Validando recurso` lines are progress messages, not failures of all those materials.
Current session log corroborates this at line 3966, timestamp 20:05:34 UTC.

Installed converter source, read-only:
`D:/Unreal Engine 5/Library/UE_5.8/Engine/Plugins/DazToUnreal/Source/DazToUnreal/Private/DazToUnrealBlueprintUtils.cpp`:

1. `ConvertToEpicSkeleton` edits bone names, parents and reference transforms.
2. Line 527 calls `CommitSkeletonToSkeletalMesh`, where the merge/save interaction occurs.
3. Lines 533–535 obtain the selected reference mesh's skeleton, call
   `MergeAllBonesToBoneTree` without checking the result, and assign it to the body.
4. Subsequent code changes retarget modes on that target skeleton and rebuilds physics.

This explains why selecting New and replacing Male2_Skeleton does not guarantee
Male2 keeps that local skeleton assigned. The live result matches the final assignment.
It also means blindly replaying this menu can affect the shared skeleton.

The earlier log records `pelvis : Hierarchy does not match hip - root` at
20:03:32 UTC, then acceptance of New/local replacement and successful skeleton save.
That warning occurs before the final commit, so it must not be presented as a full
post-conversion bone comparison. The later validation failure establishes that the
final mesh/skeleton pair is incompatible. UE's `USkeleton::IsCompatibleMesh`
(`Engine/Source/Runtime/Engine/Private/Animation/Skeleton.cpp:648`) checks matching
bones and parent chains; `SkeletalMesh.cpp:1769` emits the reported error on failure.
The exact complete set of post-conversion mismatched bones remains PENDING until
mesh-local reference skeletons are compared. MCP's shared-skeleton bone listing
must not be substituted for that comparison.

There is also a MikkTSpace zero-length-normal message during Male2 rebuild at
20:04:53 UTC. Treat it as a separate geometry/tangent investigation (degenerate
geometry/UVs are possibilities, not an established cause here). It does not explain
the skeleton hierarchy validation failure. Other startup warnings/errors in the
session log precede conversion and are not proven to come from Male2.

Epic's [Skeleton documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletons-in-unreal-engine)
also distinguishes bone hierarchy compatibility from merely sharing an asset name.
The installed source and live target observations above are the diagnosis basis.

## Recommended architecture — proposal, not implemented

Use one project-owned body definition/catalog consumed by EFCharacterCreation and
the existing EFProjectSystems NPC variation service. Avoid duplicating a character
Blueprint for every body or hard-coding five entries per gender.

Each definition should have a stable ID, gender, UI name/order, soft mesh reference,
tested morph profile/defaults/ranges, NPC eligibility/weight, and necessary appearance
overrides. Clothing remains authored in the existing clothing Director; the body
definition must not become a second clothing database.

Player flow: gender -> eligible body preset -> morph customization -> existing
appearance save. Male preset 1 retains current Male; preset 2 refers to Male2 after
certification. Reselecting Male should preserve its last chosen variant. Map old
mesh-path saves to the stable body ID, with a known valid fallback.

NPC flow: persistent identity -> weighted eligible body -> supported morphological
variation -> clothing/appearance application. Save resolved body/morph state plus
seed/generator version so returning NPCs remain themselves. Use a small, art-directed
set of compatible facial/body dimensions; do not randomize all expressions and
correctives. Five bodies per gender is reasonable as catalog data, subject to actual
memory, texture, LOD, animation and clothing profiling at the target NPC count.

Reuse the current body-change hooks and avoid changing only one skeletal component.
Respect optional morph content-policy gates already present. Handle delayed loading
and cancelled swaps, missing morphs, updated materials, deformer overrides, skin/hair,
tattoo consumers, physics and equipment. A common skeleton alone does not certify these.

Implementation order: certify Male2 conversion/local skeleton and animation contract;
add catalog plus legacy-save mapping; implement Male preset 1/2 UI; extend existing
NPC variation to select bodies and persist appearance; certify clothing and packaged
behavior. A project-owned converter adapter is preferable to editing DazToUnreal.

## Reusable workflow and validation status

Created `A:/Skills Codex/ef-body-preset-workbench/SKILL.md` with an onboarding reference
and automatic-discovery metadata. `C:/Users/bigin/.codex/skills` resolves to
`A:/Skills Codex` via the existing junction. AGENTS.md routes future body tasks to
this skill. This is persistent workflow documentation, not a claim of model memory
or a newly implemented conversion executable.

The convention records UEFN selection, Merge Type New, replacement of the imported
local skeleton, final-assignment verification, scoped snapshots, reuse of existing
appearance systems and exact validation requirements. The historical Male2 replacement
skill targets UE 5.7/source paths and is explicitly excluded from this target workflow.

| Gate | Status | Evidence/limit |
|---|---|---|
| Live target connection and settings/morph inventory | PASS | live-inspection.json; inspection only |
| Diagnosis of shared-skeleton final assignment | PASS | converter source + live get_skeleton + log excerpt |
| Male2 existing asset validation | FAIL | supplied/current log incompatibility; no repair attempted |
| Exact post-conversion mesh-local hierarchy diagnosis | PENDING | requires mesh-local inspection, not shared skeleton names |
| Conversion adapter, catalog, player preset 2, NPC body pool | PENDING | design only |
| Skill structure | PASS | quick_validate.py returned `Skill is valid!`; see validation.txt |
| Build / Blueprint compile / PIE / visual QA / cook / packaged run | PENDING | no runtime implementation or asset mutation in this analysis task |
| Protected migration before/after hash comparison | PENDING | no migration phase performed; do not claim certified asset invariance |

No runtime success is declared. The next integration must capture a fresh protected
baseline, including the shared UEFN skeleton, and complete the full project gate set.
