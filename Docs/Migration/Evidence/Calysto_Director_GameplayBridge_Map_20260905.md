# Calysto Director gameplay bridge map ? 2026-09-05

Status: **AUDITED SOURCE; implementation and gameplay gates PENDING.**
Target: `D:/Projects UE5/NoShellForWinter`; engine: UE 5.8.
Read-only source project was not accessed. HEAD:
`973981fd96b604e3560341447858159e32439c0c` plus the existing dirty implementation tree.
This bounded pass inspected only EFProcedural, EFProjectSystems and EFLevelFlow;
it changed this document only. It did not run C++, Blueprint, PIE or assets.

Path keys below: **P** = `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay`,
**R** = `Plugins/EFProcedural/Source/EFProceduralRuntime`,
**A** = `Plugins/EFProcedural/Source/EFProceduralACFURuntime`,
**L** = `Plugins/EFLevelFlow/Source/EFLevelFlowRuntime`.
Line numbers identify the audited source revision; hashes follow below.

## Existing V7 boundary

`R/Public/Calysto/EFCalystoDirectorSubsystem.h` currently exposes run requests,
read-only snapshot/diagnostics, BeforeTravel, FloorReady and RequestFailed.
`R/Public/Calysto/EFCalystoDirectorRuntime.h` supplies native attempt
preflight/begin/observe/release and OnCommitted. Native verification reaches
AwaitingPlayerRelease; EFLevelFlow confirms the actual pawn/view before commit.
`bGameplayVerified` deliberately remains false for the native parity fixture.

These APIs do **not** yet provide a typed population manifest, companion-level
plan, roster/outcome submission, gameplay preparation veto, staged inventory
restore, ecology update or atomic gameplay commit bridge. A consumer must not
manufacture a V6 intent from V7 or call a V6 fallback to fill these gaps.

## Consumer-to-bridge map

| Consumer and exact entrypoints | Reusable behavior | Required V7 migration / correction |
| --- | --- | --- |
| `P/Calysto/ProjectCalystoPopulationBridge.h:9`; `.cpp:142` HandlesCategory, `:307` PrepareDeferredActor, `:403` FinalizeSpawnedActor, `:535` RollbackSpawnedActor, `:584` CommitPopulationPlan. Registered by `P/EFProjectSystemsGameplayModule.cpp:82`. | Deferred actor preparation, exact resident-class checks, gameplay postcondition checks and specialized enemy/NPC/chest rollback. | Replace `IEFCalystoPopulationBridgeV6` and all V6 plan/decision parameters with a native unversioned bridge taking the immutable V7 selection/reservation. Dispatch on typed role, not `Enemy`/`NPC`/`Chest` names. Key staging by request + attempt + element ID, not only world/hash. Rollback must observe destroyed actors/components and release all pending maps before reseeding. |
| `P/Characters/ProjectEnemyLevelSubsystem.cpp:229` PrepareDeferredDirectorEnemy, `:648` RollbackDirectorEnemy; public synchronous initialization/verification APIs in `.h:59`. Old bridge `.cpp:80` ResolveLogicalLevel, `:94` StampDeferredDirectorLevelIdentity. | Pre-BeginPlay ARS/ACF level preparation, replacement of queued random initialization, post-spawn scaling verification and rollback. | Reuse the explicit-level APIs with a level frozen from V7 depth + authored offsets, including the supported physical ACF cap. Remove string tag protocol `EF.Calysto.V6.DirectorAssignedLevel` / `LogicalLevel.*` from both the bridge and enemy subsystem (`.cpp:23`). Preserve unrelated generic world-tier initialization through `IProjectEnemyLevelContextProvider`; it must never reroll a Director-owned pawn. Old bridge currently ignores authored offsets by using floor number. |
| `P/Calysto/ProjectCalystoPopulationBridge.cpp:268` BuildRandomNPCDefinition, `:466` recruitment hook; `P/Companions/ProjectCompanionRuntimeAdapter.h:30` PrepareDeferredCompanion, `:40` FinalizeDeferredCompanion, `:57` RollbackSpawnedCompanion. | Project companion definition, statistics repair contract/preload, typed ACF character preparation, group/social/death integration. | Construct definition from explicit stable V7 element ID, content ID, Archetype, Gender, Lifecycle, rarity and frozen level. Delete `.cpp:51` ResolveEntryIdentity dot-token parsing and `.cpp:42` first-32-character decision-ID GUID extraction. `ToProjectGrade` at `:29` currently maps Winter through default Common although project `EProjectCompanionDifficultyGrade` already has Winter (`ProjectRunCompanionTypes.h:18`); add exhaustive five-tier conversion. |
| `P/Companions/ProjectRecruitableCompanionComponent.cpp:15` InitializeRecruitmentHook, `:66` SynchronizeFromACFGroup; `ProjectRunCompanionSubsystem.cpp:253` RegisterRecruitedCompanion, `:330` SetCompanionActivePartyMembership. | Detect actual ACF group membership before registering a recruit; canonical roster, party membership, death proxy and social changes. | Hooks may prepare during realization but cannot commit a new recruit before attempt acceptance. Preserve exact GUID identity under labels/reorder and retain failed-attempt uncommitted recruitment rollback (`:1340`). Pre-reserve active-party projections rather than using the adapter's post-selection safe-spawn search as a substitute for frozen placement. |
| `P/Companions/ProjectRunCompanionSubsystem.cpp:198` Initialize, `:216` BindDirectorEvents, `:793` HandleBeforeDirectorTravel, `:917` HandleDirectorWorldAccepted, `:1035` HandleFloorReady, `:1068` HandleFloorTravelFailed. | Pre-travel/current/floor-start roster snapshots; replay/reroll restore floor-start roster; Advance changes PendingDead to confirmed Dead; exact roster readiness and level matching. | Replace mandatory V6 subsystem dependency and five old events with typed V7 request-preparation/attempt-staging/commit/rollback. Current V7 BeforeTravel carries only floor number: an explicit travel kind and immutable source/destination identities are needed. Automatic spatial retries reuse the original prepared roster and inventory capsule, without another user-travel capture. Commit death-state/roster events only after successful whole-floor acceptance. New-run roster clearing must also wait for commit. |
| `P/Companions/ProjectRunCompanionSubsystem.cpp:419` BuildDirectorSnapshot, `:548` ApplyResolvedCompanionLevels, `:596` ResolveActiveIntentCompanionLevel, `:638` ResolveSameFloorRecruitedRevivalLevel, `:1182` MaterializeActivePartyProjectionsForFloor, `:1364` FinalizeDirectorRosterReadiness. | One frozen level per roster GUID, exact grade matching, same-floor newly recruited revival bound to one original population decision; group projection adoption/release. | Define unversioned roster and resolved-level inputs with logical floor identity and stable content/element IDs. Replace GenerationSerial/V6 intent/hash coupling. Preserve floor-local versus recruitable lifecycle and prove every selected projection's placement/group/stats before commit. Do not replace same-floor revival's exact original decision with a fresh level roll. |
| `P/Companions/ProjectRunCompanionSubsystem.cpp:1551` ComputeInventoryHash, `:1605` SerializeEquipmentCapsule, `:1639` RestoreTypedEquipmentCapsule, `:1758` RestoreRevivalInventoryCapsule, `:1790` CaptureInventoryForTravel, `:1838` RestoreAndVerifyInventoryAfterTravel, `:1881` VerifyRestoredEquipment. | Typed ACF equipment serialization restores concrete item objects, subclasses, GUIDs, counts, fragments, equipment/accessories and exact inventory hash. Read-only `AuditTypedInventoryForAutomation` exists at `:1537`. | Retain these typed serializers/verifiers. Replace V6 canonical hash dependency/domain and GenerationSerial transport identity. Keep the pre-request bytes/hash alive across every spatial attempt and through failure UI; `RestoreAndVerifyInventoryAfterTravel` currently resets the capsule after restore (`:1877`) before final floor commit, and failure handler resets it (`:1078`). Stage destination inventory changes and provide exact rollback after late realization/release failure. Do not recapture mutated destination inventory as the next attempt's baseline. |
| `P/Companions/ProjectRunCompanionSubsystem.cpp:2042` PurgeWintersRecallFromInventory, `:2087` ReportCompanionDeath, `:2148` HasConfirmedDeadCompanion, `:2160` GetRevivalCandidates, `:2223` CanBeginRevival, `:2414` ConfirmPendingRevival. | PendingDead and confirmed Dead distinction, exact ACF group removal, combat/active-run restriction, exact Recall object/count, one-item consumption only after verified revival, typed inventory rollback if consumption postcondition fails. `ProjectCompanionRevivalConsumable`, menu and death proxy are reusable unversioned classes. | Route active run/pending state/frozen revival level through V7. Preserve pending same-floor revival and confirmed-dead graveyard eligibility separately. New-run Recall purge currently occurs at world acceptance (`:980` vicinity); it must remain rollback-capable until commit. Explicit retry or automatic reseed must not purge, consume, recruit, confirm deaths or alter inventory twice. |
| `P/Lockpicking/ProjectCalystoChest.cpp:36` ConfigureResolvedLoot, `:115` VerifyFrozenLootStorage, `:141` FinalizeAndVerifyResolvedLoot; base `ProjectLockedWorldItem`. | Project-owned skeletal chest appearance, authoritative empty ACF storage, exact selected item classes/count verification, existing lockpicking/interactions. No random content roll in the chest. | Feed frozen V7 container-content decisions after class preloading. Existing chest/bridge explicitly cap at three entries and Quantity==1 (`:47`, `:78`); either compile V7 amounts to this documented supported contract or implement the advertised larger quantity behavior. Never silently truncate. Preserve chest variants/appearance references and base lock state. Configure/finalize failure rejects the entire attempt. |
| `P/Calysto/ProjectCalystoFloorOutcomeSubsystem.cpp:33` Initialize, `:73` HandleBeforeFloorAdvance, `:132` HandleFloorReady, `:219` BuildOutcome. | Combat fraction, health survival, hunger/thirst resources, bounded pace score, death/failure tally and baseline at accepted Ready. | Replace V6 intent/manifest/submission with immutable V7 completed-floor result and token-scoped staged outcome. Count only owned realized enemy IDs; current alive scan at `:262` includes all world actors with `EF.Calysto.Enemy`. Make missing telemetry explicit, not `.cpp:115` neutral continuation after submission rejection. Preserve retry counters separately from committed ecological outcomes. |
| `R/Private/Calysto/EFCalystoDungeonSubsystem.cpp:176` BuildInitialEcologyV6, `:202` CommitOutcomeToEcologyV6, `:606` SubmitFloorOutcome, `:625` SubmitCompanionRunSnapshot; planner options `:1325`. | Canonical run ecology, queued outcome/roster and pending destination state are migration source behavior. | Implement unversioned immutable run-state ecology with stable entry IDs, cooldowns/repetition/drought/quotas and bounded authored modifiers. Freeze pre-floor state for all four attempts; commit accepted result once. V6 population options currently set `bGraveyardEligible=false` at `:1325`, so that path is not preserved functional eligibility. V7 must compute explicit eligibility from canonical confirmed-dead roster/Recall state before probability. Adaptation disabled must yield exactly zero adaptive influence. |
| `P/Debug/ProjectGameplayDebugCommandExecutor.cpp:52` FindCalystoDungeonSubsystem, `:527` GetDungeonHarnessStatusLabel, `:580` GetDungeonHarnessStatusDescription, `:709` floor/style choices, `:838` onward travel commands. | Existing `L > Dungeon Harness` host/menu and Development-only restrictions. | Bind existing advance/replay/reroll/new-run controls to actual V7 APIs; add Retry/Return HUB/Cancel and read-only GetDiagnosticsJson inspector. Current next-floor bias/style intent controls and DevelopmentJump have no equivalent implemented V7 mutation API: implement bounded explicit intent/validation first, or visibly disable unsupported controls. No string-derived style identity, success callback buttons or V6 status/fallback. Probability simulator must exercise the production library and separate requested/feasible/accepted distributions. |
| `P/DayCycle/ProjectDayCycleSubsystem.cpp:115`; `P/Defeat/ProjectDefeatTravelSubsystem.cpp:104`; `A/Private/Calysto/EFCalystoPackagedSmokeSubsystem.cpp:161`; `P/Companions/ProjectCalystoInventoryTravelFixtureSubsystem.*`. | Floor readout; defeated same-seed replay; legacy smoke and exact equipment-corruption fixtures. | DayCycle reads V7 last committed floor for gameplay. Defeat routes current run through V7 replay; candidate must not fall through to raw OpenLevel when old subsystem is absent. Migrate fixture ownership/receipt schema and exact inventory faults to V7, retaining bounded tests. Old V6 smoke never counts as V7 packaged proof. |

The existing project roster types are unversioned but still call
`UEFCalystoDungeonSubsystem::ComputeCanonicalHash`
(`P/Companions/ProjectRunCompanionTypes.cpp:143`) and encode GenerationSerial.
Their names alone do not make them V7-independent. Extract the pure canonical
hash helper into an unversioned project utility and define migration-safe
logical identities; do not involve routing GUIDs in random seeds.

## Staging interface required before population is enabled

The following is a **proposed contract, not an existing callable API**:

1. Request preparation receives explicit travel kind, source accepted floor,
   destination floor/reroll, run epoch and request routing identity. A gameplay
   bridge captures immutable roster/equipment/outcome state and can reject
   configuration/resource failure before travel. Prepare is idempotent for this
   request, not per spatial retry.
2. Attempt preparation receives only frozen typed decisions and reserved
   transforms. It preloads additional required dependencies, prepares deferred
   actors and isolated roster projections, restores/verifies destination
   equipment while retaining the original rollback capsule, and returns an
   owned realization receipt. It never rolls replacements or commits roster,
   inventory handoff, Recall consumption or ecology.
3. Verification identifies every reserved element and companion, exact container
   contents and equipment hash; missing or conflicting evidence rejects the
   entire attempt. Pending infrastructure remains Pending under the shared
   deadline. A hash-only `PreFloorGameplayHash` is not an inventory snapshot.
4. Commit must be prepared to succeed as one owned game-thread operation before
   FloorReady/player release. The existing void OnCommitted callback cannot
   host fallible inventory/roster work after transaction acceptance. Define a
   staged gameplay commit/rollback protocol, validate its receipt first, then
   publish roster/outcome events once. Re-check ownership after callbacks.
5. Attempt rollback removes actors/components/group membership/projections and
   all attempt leases while retaining request-level baseline data. On exhaustion
   preserve protected player and Retry/Return HUB. Cancellation and explicit HUB
   return settle request ownership without committing a generated floor.

## Immediate candidate risks and bounded verification

Static evidence: `ProjectRunCompanionSubsystem::Initialize` still forces the old
subsystem dependency; BindDirectorEvents logs Error when candidate guards stop
V6 creation. Outcome initialization similarly reports missing old authority.
This is an actual unmigrated consumer, not permission to suppress failed full
playability evidence. Native parity may expose these logs; assess them explicitly.

Keep the native three-floor gate first. Then use a small selected-content fixture
covering one enemy, one recruitable NPC, one chest with known contents and a
frozen active companion. Test preparation/finalization/late-release failure once
each with retained baseline hashes; each request stays within 30 seconds/four
attempts. Verify recruit -> pending death -> same-floor Recall and confirmed
Dead -> graveyard eligibility -> Recall using exact inventory object/count
receipts. Separately test new-run purge, replay/reroll restoration, cancellation,
exhaustion and explicit HUB return. Reuse the meaningful existing companion
canonical-hash/same-floor-revival and typed equipment corruption tests after
porting them; never call their V6 results V7 proof. The 100,000 in-memory
probability suite remains a separate short gate, not 100,000 generated floors.

Full gameplay, visual QA, Blueprint compilation, cook/packages and final V7-only
retirement remain PENDING. No protected assets were saved by this audit.

## Source fingerprints

SHA-256 for central inspected files at document creation:

- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Calysto/ProjectCalystoPopulationBridge.cpp`: `8ab02b73789ef6289f962b68ecf30c00234055cbdc53f7f16bba4726144fbb5e`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Calysto/ProjectCalystoFloorOutcomeSubsystem.cpp`: `ac8381d9b82ec7b7231661ddf64005b86bc32d84c255691df2adcbf8560e0430`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Companions/ProjectRunCompanionSubsystem.cpp`: `593a34f78adc8568c07bffd64f853fd6e116d4746d45bdb8c48d4c3ea43d0916`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Companions/ProjectRunCompanionTypes.cpp`: `d0a1efd62bcdfa0ab97fc170edcd74a4cd7985042b1e228e87146140dbd4dfc8`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters/ProjectEnemyLevelSubsystem.cpp`: `ed30682eb08639e474f6c35af0134455925fc4d5454df82e9817f6d01a8445b8`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Lockpicking/ProjectCalystoChest.cpp`: `cd95632ffb6a5ee833ea1010d5a1db565ff272005459071ab60c83c31ea5f541`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Debug/ProjectGameplayDebugCommandExecutor.cpp`: `76fe28a4d0092588779d03372bf4d6a3cdb46bcb83162f1d355f9eea7058aded`
- `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDirectorSubsystem.h`: `14fb49ad3006b0d6746910d70b28467979edb88d9b35aa6ddfe0d7fd56685bf1`
- `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDirectorRuntime.h`: `a7a1d020985ffd31b4dcc7eeeffa1b3dfd1fef927890bd7c52ecab755d86bb3c`
