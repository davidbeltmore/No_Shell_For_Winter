# Calysto V7 typed trait bindings - 2026-09-08

Status: **source implemented; focused native checks PASS; live integration PENDING**. This bounded G1 change preserves all 25 source trait values and implements explicit native content modifiers. Parent owns builds, Editor, assets and G3 architecture. No native adapter/subsystem, runtime snapshot provider, asset or acceptance-ledger edits were made here.

`FEFCalystoTraits` retains normalized Mystery, Danger, Safe, Abundance and ClothingInfluence on each Style and Theme. `FEFCalystoTraitBinding` explicitly names input source (selected Style, current Theme or supplied snapshot), trait, typed gameplay role and target control. Chance targets a role; EntryWeight also requires a persisted exact entry ID. No name-to-role associations, hidden default bindings or invented trait effects exist.

Each binding specifies multiplicative percentage effects at input 0 and 1. `ResolveTraitMultiplier` linearly interpolates those endpoints, adds matching effects in canonical input/target order, and clamps their sum once to the authored `MaximumEffectPercent`. It returns `1 + boundedEffect/100`. An absent Theme contributes no Theme term; a referenced absent/invalid snapshot fails. Adaptation disabled, maximum effect zero, or no matching bindings returns exactly 1 without reading unused context. Controls never change Theme presence, Amount, capacities, rarity weights or random identities.

`UEFCalystoDungeonDirectorAsset::Compile` rejects nonfinite/out-of-[0,1] trait fields, invalid source/trait/role/control enums, invalid endpoint effects outside [-100,100], duplicate input/control/target bindings, over 64 bindings and nonexistent authored role/entry targets. Exact field paths identify errors. Immutable compiled copies retain both traits and bindings; neither labels nor array position determine binding identity.

`FEFCalystoContentReservationRequest::TraitSnapshot` is an optional immutable typed input from the caller. The former generic `NormalizedAdaptationInput` had only planner-test callers and implicitly modified every category; it is replaced by explicit bindings and the existing test now authors that association. The bounded scalar `AdaptationMultiplier` primitive remains and implements the final clamped effect.

`FEFCalystoContentReservationPlanner` resolves/caches Chance and entry weights once per distinct scope/target. Zero effective entry weights are excluded before compatibility/Amount feasibility, so a later choice cannot invalidate an already accepted count. Conditional entry selection uses those same effective weights; frozen payloads retain authored selection data. Reports contain requested/effective Chance, multiplier and per-entry effective weights. Enabled evaluation charges `bindingCount * (bindingCount + 8)` work units before validation/sorting/evaluation. Disabled/empty bindings have the constant neutral path. A future optimization may validate the policy once per build; current work exhaustion rejects rather than exceeding the finite proof budget.

`Mapper.traits` maps every exact original leaf, checks finite [0,1] values without clamping/defaulting and retains source numerics. Initial migration explicitly sets `Adaptation.bEnabled=false`, `Bindings=[]`; enabling adaptation with no authored association has no effect. This preserves the prior ineffective trait inputs while supplying an actual explicitly authored consumer. Current full-floor capability preflight still rejects enabled adaptation until real snapshot capture and world integration are verified.

## Focused checks

- `python Tools/Migration/Test-CalystoDirectorImporter58.py -v`: **15 tests PASS, .357 seconds, process exit 0**. Added pure checks verify all 25 archived values, exactly 25 unique correction rows, exact parser retention, source immutability and each malformed/missing field. Existing archive comparison explicitly accounts for parent's G3 chance/cap additions while comparing all remaining values unchanged. Earlier two fixture-comparison failures were corrected (Raw numeric wrapper and nullable synthetic record), not suppressed.
- New prepared native `Director.Content.TypedTraitBindings`: each of the five traits through all three explicit input sources, exact 0/.5/1 Chance and Weight behavior, no unbound-entry effect, zero-weight feasibility, missing-context atomic failure, disabled nonfinite-input neutrality, empty bindings, global sum bound, binding order and typed-role isolation. These are pure planner decisions, no worlds.
- New prepared native `Director.Authoring.TraitBindings`: every Style/Theme trait's exact bounds/errors and frozen values; invalid enums/targets/endpoints, duplicates, count bound and immutable binding copies. Existing `Director.Content.CapacityPreservingWeightedChoices` now exercises an explicit Snapshot binding.
- Subsequent parent-owned `Native_TraitsArchitectureCopy_20260908/StrictSummary.json`: **6 expected/actual tests PASS**, no failures, 47.760798 seconds, launcher exit **0**. This includes both new G1 tests, the amended existing capacity test, `CompleteFieldExport` and parent's two architecture tests. Strict SHA-256 `b84b67a6f1af39a60277ddcaf22086c1267315d6dda408d9eb5d0f2d7e35f7bd`; report SHA-256 `ba975919bde4ee8f629176c86126484566ca02026257f0174ee9a7f66da73b25`. The prior `Native_TraitsArchitecture_20260908` remains FAIL from an actual architecture-test self-container assertion; parent corrected that fixture without changing G1 source. These receipts execute the prepared native assertions above, and do not promote the frozen 31 original acceptance criteria or prove live integration.

## Source fingerprints at build handoff

Relative paths under `D:/Projects UE5/NoShellForWinter`; shared types/compiler/importer include preserved parent G3 changes at this capture. Snapshot: `Saved/Migration/CalystoDungeonDirectorV7/Resume_20260908/SourceBefore.zip`.

| Path | SHA-256 |
| --- | --- |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDirectorTypes.h` | `01adcce8173e104e95ce5ed608e29ea790cb6862848f05dc6a1be02a715c6c1c` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDirectorProbability.h` | `431b03b988c99ae91999bc86443a27a6ec55ce344d869bf6af991d32899d4dd2` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDirectorProbability.cpp` | `f05da5eb03466cb8b2d3a0a2ad01fd97c282f454b6fb3bb9582a983b15536f5a` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoContentReservationPlanner.h` | `d37c4a527cdb23bf4ca0dc25378c3d48befae17594e311d73e82d937c02eada6` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoContentReservationPlanner.cpp` | `19c3c933ef9488d9d703f0e918411191ec63719d73ecbbc6cd11e4873baa9577` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDungeonDirectorAsset.cpp` | `784edffc12ef1b1504f19216ccc30eb118775eb0d4b7f88295e21596958a2119` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Tests/EFCalystoContentReservationPlannerTests.cpp` | `f399f5de8bee160c95a7f1cae6f980b2346fee91b12fcddbafb503bd1999707b` |
| `Tools/Migration/Import-CalystoDungeonDirector58.py` | `c77d772c8c3921d0a074c1f1350047b77b9d81853d8c0673efb605e35d03effd` |
| `Tools/Migration/Test-CalystoDirectorImporter58.py` | `3bf52119dbdef39fbc020fbf7471e499c86947c0f62a560c4c89c8aca411b9f9` |

Next: current-schema staging; native entry-name picker for binding targets (keep internal GUID identity, do not require authors to enter it); actual immutable gameplay/clothing/ecology snapshot source and enabled integration tests before removing capability guards. The current raw EntryId authoring property is a known native Details gap, not a completed authoring experience. No initial binding associations should be invented during import.
