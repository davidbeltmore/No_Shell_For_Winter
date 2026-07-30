# Plugin compatibility matrix

Status: `PHASE4_DOOR_TO_LEVEL_STATIC_CONTENT_AND_FRESH_LOAD_PASS_RUNTIME_PACKAGE_PENDING`

The Phase 2 rows preserve a sanitized inventory of the legacy private build. Every required and auxiliary project-owned plugin listed below now compiles in its applicable UE 5.8 Editor/Game targets. Phase 4 content statuses are promoted only for exact approved batches. The focused DirtyPawn runtime wrapper/binding contract and the static DoorToLevel rebuild are recorded below; neither promotes unexecuted PIE, cook, package, packaged-runtime, or user-owned visual QA. Run `FASTIMPORT_20260713_1948` historically quarantined a one-file ACFU PDB delta; the final protected-invariant gate now reports 5,043/5,043 ACFU files matching the baseline, including the PDB at its expected SHA-256.

User-directed scope decision (2026-07-13): SaveGame migration, legacy-slot compatibility, and ALS persistence validation are `OUT_OF_SCOPE_BY_USER`. All remaining visual QA is `USER_OWNED_OUT_OF_SCOPE` and will be performed by the user. Neither disposition is a `PASS`, and all historical evidence remains unchanged.

## Authoritative external plugins

| Plugin | Descriptor | Location | Version | Runtime/Editor | Classification | Load | Build | Cook/package | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| AscentCombatFramework | `AscentCombatFramework.uplugin` | Engine Marketplace | Source 4.2.3 / Target 4.3.5 | Runtime + Editor | KEEP_TARGET | PASS prior load probes | PASS builds / one known protected PDB hash delta quarantined for the exact Door batch | PENDING | `FASTIMPORT_20260713_1948/Gates/POST_RESAVE58_SafetyGate.json`; `PASS_EXPECTED_DELTA_KNOWN_ACFU_PDB_QUARANTINE`, external repair still pending |
| DazToUnreal | `DazToUnreal.uplugin` | Engine | Source 5.7.0.480 / Target 5.8.0.491 | Editor | KEEP_TARGET | PASS baseline | PASS | PENDING | target descriptor, baseline logs, protected hash manifest |
| BpGeneratorUltimate | `BpGeneratorUltimate.uplugin` | Engine Marketplace | Source 1.5.5 / Target 1.7.0 | Editor tooling | KEEP_TARGET | PASS_WITH_WARNINGS | PASS_WITH_WARNINGS | PENDING | UBT/commandlet logs; UECP tool calls blocked by vendor license |
| PCGExtendedToolkit | project/Engine descriptors | Engine Marketplace | Source 0.75.8 / Target 0.76.2 | Runtime + Editor | KEEP_TARGET | PASS baseline | PASS | PENDING | descriptors + build logs |
| SkinnedDecalComponent | `SkinnedDecalComponent.uplugin` | Engine Marketplace | Source 3.0 / Target 3.0.1 | Runtime | KEEP_TARGET | PASS baseline | PASS | PENDING | descriptors + build logs |

## UE 5.8 native plugin parity

The 2026-07-19 descriptor audit represents all 22 source-enabled plugins in the
target and explicitly pins `PCG` instead of relying on its UE 5.8
`EnabledByDefault` value. The same-name installed UE 5.8 equivalents added to
the target are `MLDeformerFramework`, `NiagaraFluids`,
`PCGExternalDataInterop`, `PCGFastGeoInterop`,
`PCGGeometryScriptInterop`, `PCGInstancedActorsInterop`,
`PCGPythonInterop`, `Volumetrics`, and `WaterAdvanced`. All ten parity plugins,
including explicit `PCG`, mount in a fresh live editor; Editor and Game builds
pass, and 74/74 Calysto/Procedural Blueprints compile without saves.

This closes descriptor/load/build parity, not Calysto runtime acceptance. The
focused DungeonGeneration PIE run reaches the dungeon actor and two PCG
components but fails `PathfindingElement_2` and generates zero StartPoints.
Cook processes 7,017 packages but finishes with the pre-existing global
`GameFeatureData` Asset Manager errors. Evidence:
[UE58_Plugin_Parity_20260719.md](Evidence/UE58_Plugin_Parity_20260719.md).

## Project-owned plugin inventory

Every reference descriptor below came from the read-only legacy private build. Its filesystem and repository identity are intentionally omitted. All listed project-owned plugins have since been migrated or rebuilt under target ownership and pass their applicable UE 5.8 compile gates.

| Plugin | Version | Modules, type, loading phase | Descriptor plugin edges | Source files | Public surface | Content/config | Classification | Phase status |
|---|---:|---|---|---:|---|---|---|---|
| EFCharacterCreation | 1.0.0 | `EFCharacterCreationRuntime` Runtime/Default; `EFCharacterCreationEditor` Editor/Default | none | 25 | 12 headers; 8 UCLASS, 9 USTRUCT, 1 UENUM | 2 WBP + recomposed project `DefaultGame.ini` section | MIGRATE_SOURCE_WITH_CONFIG_MERGE | PHASE3_EDITOR_GAME_ASSETS_CONFIG_PASS / RUNTIME_QA_PENDING |
| EFCharacterCreationDazBridge | 1.0.0 | `EFCharacterCreationDazBridgeEditor` Editor/Default | EFCharacterCreation, DazToUnreal, DeformerGraph enabled in target | 3 | 1 module header | none | REBUILD_AGAINST_TARGET | PHASE3_EDITOR_BUILD_READ_ONLY_PROBE_PASS / MUTATING_COMMANDS_BLOCKED |
| EFClothingMorph | 1.0.0 | `EFClothingMorphRuntime` Runtime/Default | EFCharacterCreation | 4 | 1 header; 1 UCLASS, 8 USTRUCT, 2 UENUM | none | MIGRATE_SOURCE | PHASE3_EDITOR_GAME_BUILD_PASS / PLAYER_INTEGRATION_QA_PENDING |
| EFProcedural | 1.0.0 | `EFProceduralRuntime`, `EFProceduralACFURuntime`, `EFProceduralPCGRuntime` Runtime/Default; `EFProceduralEditor` Editor/Default | PCG | 26 | 13 headers; 4 UCLASS, 3 UINTERFACE | exact 20-package contract batch, exact `DungeonGeneration` World, and exact DoorToLevel visual closure present; dungeon actor deferred | MIGRATE_SOURCE | PHASE4_EDITOR_GAME_EXACT_CONTRACTS_MAP_AND_DOOR_STATIC_PASS / PIE_COOK_PACKAGE_DUNGEON_RUNTIME_PENDING / VISUAL_QA_USER_OWNED_OUT_OF_SCOPE |
| EFLevelFlow | 1.0.0 | `EFLevelFlowRuntime` Runtime/Default | AscentCombatFramework, EFProcedural, EFCharacterCreation | 9 | 4 headers; 3 UCLASS | native `AProjectLevelDoor` adapter plus rebuilt `/Game/Procedural/DoorToLevel`; four visual dependencies imported and UE 5.8-resaved | REBUILD_AGAINST_TARGET | NATIVE_EDITOR_GAME_BUILD_PASS / DOOR_BLUEPRINT_COMPILE_RESAVE_RELOAD_FRESH_LOAD_PASS / PIE_COOK_PACKAGE_PENDING / VISUAL_QA_USER_OWNED_OUT_OF_SCOPE / SAVEGAME_OUT_OF_SCOPE_BY_USER |
| EFCharacterCreationACFUBridge | 1.0.0 | `EFCharacterCreationACFURuntime` Runtime/Default | EFCharacterCreation, GameplayAbilities | 5 | 2 headers | none | REBUILD_AGAINST_TARGET | PHASE3_EDITOR_GAME_BUILD_PASS / RUNTIME_QA_PENDING |
| EFProjectSystems | 0.1.0 | `EFProjectSystemsCore`, `EFProjectSystemsGameplay`, `EFProjectSystemsUI` Runtime/Default; `EFProjectSystemsEditor` Editor/Default | GameplayAbilities, EnhancedInput, AscentCombatFramework, ACFTrainingSystem, EFCharacterCreation, EFLevelFlow, EFProcedural, CodeWidgetDesignerBridge, DirtyPawnRuntime, SkinnedDecalComponent | 288 | 144 headers; 217 UCLASS, 91 USTRUCT, 31 UENUM, 4 UINTERFACE | plugin Content empty; 5 Core Redirects, 58 `Project.*` tags, and 6 settings sections applied selectively; `/Game` dependencies pending | MIGRATE_SOURCE_AFTER_DEPENDENCIES | PHASE3_EDITOR_GAME_CONFIG_STRUCTURAL_NATIVE_AUTOMATION_PASS / CONTENT_BLUEPRINT_PIE_COOK_PACKAGE_PENDING / VISUAL_QA_USER_OWNED_OUT_OF_SCOPE |
| EFBlink | 0.1.0 | `EFBlinkRuntime` Runtime/Default | none | 11 | 5 headers; 3 UCLASS, 1 USTRUCT | none | MIGRATE_SOURCE | PHASE3_EDITOR_GAME_BUILD_PASS / CONFIG_PENDING / VISUAL_QA_USER_OWNED_OUT_OF_SCOPE |
| DirtyPawnRuntime | 1.0.0 | `DirtyPawnRuntime` Runtime/Default; `DirtyPawnRuntimeEditor` Editor/Default | none | 12 | 5 headers; 16 UCLASS, 5 USTRUCT, 3 UENUM | exact 15-package material/texture closure migrated and resaved | MIGRATE_SOURCE | PHASE4_EDITOR_GAME_ASSET_COMPILE_RESAVE_AND_RUNTIME_BINDING_PASS / ALPHA_API_TATTOO_COOK_PACKAGE_PENDING / VISUAL_QA_USER_OWNED_OUT_OF_SCOPE |
| ACFTrainingSystem | 0.1.0 | `ACFTrainingSystem` Runtime/Default | GameplayAbilities, AscentCombatFramework | 11 | 5 headers; 3 UCLASS, 5 USTRUCT, 1 UENUM | 1 project AnimSequence migrated through AssetTools 5.7 and resaved 5.8 | REBUILD_AGAINST_TARGET | PHASE3_EDITOR_GAME_ASSET_PASS / RUNTIME_COOK_QA_PENDING |
| CodeWidgetDesignerBridge | 0.1.0 | `CodeWidgetDesignerBridge` Runtime/Default; `CodeWidgetDesignerBridgeEditor` Editor/Default | none | 22 | 2 public headers; 1 UCLASS, 5 USTRUCT, 2 UENUM, 1 UINTERFACE | none | REBUILD_AGAINST_UE58_EDITOR | PHASE3_EDITOR_GAME_BUILD_PASS / QA_PENDING |

`CanContainContent=true` is present on EFCharacterCreation, EFProjectSystems, DirtyPawnRuntime, and ACFTrainingSystem, but only EFCharacterCreation currently contains plugin assets. Project DataAssets, DataTables, Widget Blueprints, maps, input assets, and other `/Game` dependencies must therefore be handled by the content manifest; porting plugin source alone is not a subsystem-complete migration.

EFProjectSystems was imported through a 289-file descriptor-plus-Source allowlist totaling 3,491,166 bytes. Ten project-owned files differ intentionally from that receipt: the ACFU signature adaptation, UE 5.8 delegate and runtime/editor separation fixes, and remapping of absent legacy enemy classes to the three target-authoritative ACF enemy assets.

Its Phase 3 read-only UE 5.8 probe passed 46/46 checks across ten representative native classes/CDOs, effective input/survival/release settings, five redirect lines, and 58 project tags. At that point it found the three authoritative enemy packages and eight missing soft packages, plus one pending PNG outside AssetRegistry. It did not load maps or content objects, compile Blueprints, save assets, or run PIE. A separate strict native Automation gate passed 71/71 tests with no warnings; the later exact Phase 4 content batches have their own evidence and do not promote the deferred PIE gates.

The approved EFProcedural contract increment contains exactly 20 packages: 19 Calysto data contracts plus the canonical `/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint`. UE 5.7 AssetTools migration, UE 5.8 load/compile/resave, Editor/Game builds, source read-only verification, and protected-target re-hash all pass. Evidence: [Phase4_ProceduralContracts_ContentBuild.json](Evidence/Phase4_ProceduralContracts_ContentBuild.json).

The separate exact-map increment adds only `/Game/Procedural/Maps/DungeonGeneration` as a `World`. Its UE 5.7 read-only load and AssetTools migration, UE 5.8 load/save/reload, Editor/Game builds, source read-only verification, and protected-target re-hash pass. This is static compatibility evidence, not a procedural-runtime PASS: `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon`, map/door PIE, cook, package, and packaged runtime remain pending. `/Game/Procedural/DoorToLevel` is now present through the later exact Door batch. Visible navigation/procedural QA is `USER_OWNED_OUT_OF_SCOPE`. Evidence: [Phase4_DungeonGeneration_ContentBuild.json](Evidence/Phase4_DungeonGeneration_ContentBuild.json).

The DirtyPawn increment migrates exactly 15 packages: 1 DAZ wrapper material, 8 material functions, and 6 textures. The isolated UE 5.7 load and exact AssetTools migration pass; UE 5.8 loads, compiles the material and all functions, resaves, reloads, and reports zero material/shader errors. Editor/Game builds and both post-migration source/protected gates pass. A focused six-test Defeat-flow PIE run finishes with `SucceededWithWarnings` for all six tests, six `Result={Success}` completions, six `Ready ... bindings=6` records, no missing wrapper, and no PIE lifecycle error. This is a runtime resolution/binding contract only. The remaining morph-physics constraint warnings belong to the Phase 7 Player/Daz audit. Wet/mud/blood/smear/snow/sand presentation and rendered-alpha confirmation are `USER_OWNED_OUT_OF_SCOPE`; tattoo-path compatibility, blood-alpha API confirmation, cook, package, and packaged runtime remain `PENDING`. Evidence: [Phase4_DirtyPawnAssets_ContentRuntime.json](Evidence/Phase4_DirtyPawnAssets_ContentRuntime.json).

The ProjectLevelDoor increment now includes its exact static content batch. Its project-owned native adapter passes UHT plus UE 5.8 Editor and Game builds. Run `FASTIMPORT_20260713_1948` validated and migrated exactly four visual packages through isolated UE 5.7 AssetTools, then UE 5.8 loaded, compiled/resaved and reloaded them and rebuilt `/Game/Procedural/DoorToLevel` as a thin child of `/Script/EFLevelFlowRuntime.ProjectLevelDoor`. The Blueprint was `UP_TO_DATE` both before and after save, retains the quest-target and map-marker components, and has no duplicate travel EventGraph. `DoorToLevel58Rebuild.json` reports `UE58_DOOR_TO_LEVEL_REBUILD_RESAVE_RELOAD_PASS`; its historical `POST_RESAVE58_SafetyGate.json` quarantined the then-observed ACFU PDB delta. The final `ProtectedInvariantVerification.json` subsequently reports the entire protected ACFU set matching baseline. The later separate-process `DoorToLevel58FreshLoad.json` reports `UE58_DOOR_TO_LEVEL_FRESH_READ_ONLY_LOAD_PASS`; `Logs/UE58_DoorFreshLoad_Retry1.log` exited 0 with zero errors and the Blueprint still `UP_TO_DATE`. PIE interaction/travel/quest/map-marker, cook, package, and packaged runtime remain `PENDING`. SaveGame/legacy slots and ALS persistence are `OUT_OF_SCOPE_BY_USER`; Door/dungeon visual QA is `USER_OWNED_OUT_OF_SCOPE`.

## Dependency DAG

The project-owned dependency graph is acyclic:

```text
EFCharacterCreation
|- EFCharacterCreationDazBridge
|- EFClothingMorph
|- EFCharacterCreationACFUBridge
`- EFLevelFlow

EFProcedural
`- EFLevelFlow

ACFTrainingSystem ---------.
CodeWidgetDesignerBridge --+
DirtyPawnRuntime ----------+
EFCharacterCreation -------+
EFProcedural --------------+
EFLevelFlow ---------------`- EFProjectSystems

EFBlink (independent leaf)
```

Module-internal edges add no cycles: each Editor module depends on its Runtime module; `EFProceduralPCGRuntime` depends on `EFProceduralRuntime` and `EFProceduralACFURuntime`; `EFProjectSystemsEditor` depends on Core, Gameplay, EFLevelFlowRuntime, and EFProceduralEditor.

## Required port order and current state

Steps 1 through 7 are complete at the source-port and applicable Editor/Game compile level. Their subsystem runtime, cook, package, and packaged-runtime gates remain independent; remaining visual QA is `USER_OWNED_OUT_OF_SCOPE`.

1. Port and compile independent leaves: CodeWidgetDesignerBridge, DirtyPawnRuntime, EFBlink.
2. Port EFCharacterCreation; migrate its two WBP assets through Unreal and merge, rather than replace, `Config/DefaultGame.ini`.
3. Port EFCharacterCreationDazBridge, EFClothingMorph, and EFCharacterCreationACFUBridge.
4. Port ACFTrainingSystem and validate its ARS/GAS contract against ACFU 4.3.5.
5. Port EFProcedural in module order: Runtime, ACFURuntime, PCGRuntime, Editor.
6. Port EFLevelFlow after EFCharacterCreation and EFProcedural are green.
7. EFProjectSystems descriptor/source import, Editor/Game build, selective structural config probe, and strict native Automation gate are complete. Exact core, Modern UI, procedural-contract, `DungeonGeneration` map, DirtyPawn material-closure, and DoorToLevel static-content batches are validated to their documented scopes. DirtyPawn wrapper/binding PIE and DoorToLevel read-only fresh-load pass; remaining subsystem PIE, cook, package, and packaged runtime remain pending. Remaining visual QA is `USER_OWNED_OUT_OF_SCOPE`.

Passing the source port does not make a subsystem complete: each wave still needs its applicable load, Blueprint, PIE, cook, package, and protected-hash evidence. No `Binaries`, `Intermediate`, or `Saved` subtree is a port input.

## Confirmed ACFU 4.3.5 signature change

This is a confirmed compile blocker, not a speculative hotspot:

- Project call: project-owned `InnerDoctrine/ProjectInnerDoctrineComponent.cpp`
- ACFU 4.2.3 declaration: private reference installation, `CollisionsManager/Public/ACMEffectsDispatcherComponent.h:20`.
- ACFU 4.3.5 declaration: active UE 5.8 installation, `CollisionsManager/Public/ACMEffectsDispatcherComponent.h:20`.

`UACMEffectsDispatcherComponent::PlayReplicatedActionEffect` changed from two arguments to three and now requires `const FComponentFX& outComps`. The target-owned compatibility patch constructs an `FComponentFX` and passes it as the third argument. UE 5.8 Editor and Game builds pass; runtime hit-feedback validation remains pending. ACFU 4.3.5 was not modified.

## UE 5.8 / ACFU compatibility hotspots

| Plugin | Initial hotspot/gate |
|---|---|
| EFCharacterCreation | Merge settings; reject old `AmalaforGenesis9` body option and `bAutoEnterTestingMap=True` unless explicitly retained; validate runtime WidgetTree/input/camera with authoritative Female Player. |
| EFCharacterCreationDazBridge | High-risk editor APIs around Control Rig, AnimBlueprint graph generation, post-process assignment, and `CreateAutoJCMControlRig.py`; audit-only before any repair command. |
| EFClothingMorph | Runtime reflection of InventorySystem class/property/delegate names plus morph-map and leader-pose behavior; equip/unequip probe required. |
| EFProcedural | PCG delegates, `GenerateLocal`, navigation rebuild/invokers, and spawned-pawn sanitation; dungeon PIE required. |
| EFLevelFlow | Direct AIFramework threat APIs plus input/camera/loading state restoration. |
| EFCharacterCreationACFUBridge | Reflected `ACFCharacterMovementComponent.SetCanMove` and GAS `CancelAllAbilities`; both need runtime contract probes. |
| EFBlink | Low compile risk; authoritative Female morph existence remains a functional gate; visible blink QA is `USER_OWNED_OUT_OF_SCOPE`. |
| DirtyPawnRuntime | Exact material closure, UE 5.8 compile/resave, and focused wrapper/binding runtime contract pass. Tattoo-path and alpha API compatibility, cook, and packaged validation remain gates; dynamic material/height-mask presentation QA is `USER_OWNED_OUT_OF_SCOPE`. |
| ACFTrainingSystem | ARS modifier handles, ACF GAS attribute tags, GameplayEffect fallback, replication, progress, and minigame delegates. SaveGame/legacy-slot validation is `OUT_OF_SCOPE_BY_USER`. |
| CodeWidgetDesignerBridge | High-risk UMGEditor/KismetCompiler/WidgetBlueprint factories and commandlets; compile commandlets before using them to mutate any WBP. |
| EFProjectSystems | Highest risk: broad ACF module surface, direct inheritance/interfaces, reflection, 144 effectively public headers, and the confirmed `FComponentFX` signature change. `bUseUnity=false` should remain for the first strict build. |

Detailed evidence and exact descriptor hashes are recorded in `Docs/Migration/Evidence/Phase2_Plugin_Dependency_Audit.md`.
