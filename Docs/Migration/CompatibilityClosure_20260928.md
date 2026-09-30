# Compatibility continuation - 2026-09-30

Current implementation: `81fb51cd729f100a90c3f3ac739601c23dc8d4fb`. Continuation baseline: `62c2c81d4cf0525ea0a3f791d367e36ef3fa4c1e`.
Target: `D:/Projects UE5/NoShellForWinter`; source remains read-only. Detailed receipts, artifact hashes, scope limits and residual ownership: [CompatibilityContinuation_20260930.json](Evidence/CompatibilityContinuation_20260930.json).

**The project-owned repairs below are validated in their stated scope. Calysto is excluded. This is not a claim that the entire project or all vendor logs are error-free.** This section supersedes the remaining-work status in the historical 2026-09-28 report retained below.

## RESUELTO

| Item | Change and evidence |
|---|---|
| MorphPhysics | Added project-owned `PA_Female_Morph` and `PA_Male_Morph`, each preserving its original bodies and adding the seven required branches. Exact-mesh overrides and a narrow cook directory retain both profiles without editing protected meshes or original physics assets. The driver resolves the actual `CharacterMesh0` physics owner, including collisionless visible meshes without a Leader Pose link after load. |
| Physics persistence and defeat | Constraint authoring now saves `DefaultProfile`; runtime setters synchronize solver and reflected limits. Defeat collision restoration reapplies the owned simulation and current morph limits. Runtime cache invalidation handles mesh/physics asset changes. Missing constraints are reported as unavailable rather than as successfully applied target values. |
| RealisticBlood demo | Replaced only the null additive base in `/Game/RealisticBlood/Demo/Characters/Mannequins/Animations/ABP_Manny` with its existing evaluated main pose via a cache. Preserved the Control Rig, upper-body slot and layer branch. Manny and Quinn compile and show normal idle poses. This does not certify every upper-body montage. |
| Historical TattooShop backup | Registry and text-reference checks found no consumers of the broken 20260811 backup. Its exact bytes are preserved in `Tools/Migration/Archive/TattooShop_20260811-231427.zip` with a hash manifest. The editor retired the obsolete Content asset; the active TattooShop widget remains intact and compiles. |
| Compass icon references | Two exact package redirects map the old Icon_Circle and Icon_Circle_Border paths to the existing installed ACFU Texture2D assets. Live loads of old/new paths returned the same objects. No invented textures or vendor edits. |

Implementation commits: `a9573c2e`, `cd8e5369`, `db0c76dc`, `69158346`, `81fb51cd`.

## VALIDADO

Most continuation artifacts are under `Saved/Analysis/CompatibilityClosure/Continuation_20260930`. Paths and SHA-256 values are indexed in the evidence JSON.

| Gate | Result |
|---|---|
| Editor build | PASS, `build_editor_10.log`; official wrapper, Daz receipt enabled |
| Game Development build | PASS, `build_game_02.log`; final native repairs included, descriptor and Daz receipt restored |
| Blueprint compile outside Calysto | **827/827 PASS**, zero failures or on-disk changes; 73 Calysto/project procedural assets explicitly excluded. `Saved/Artifacts/PendingValidation/BlueprintCompile/continuation_20260930/RunSummary.json` |
| Canonical automation | **27 PASS**, zero failed, not-run or warning results; `Automation_20260930_144616` |
| General PIE | **177 checks PASS**: Visual 46, Activities 81, Reload 23, HUB-only Respawn 27. Current native progression, effort, rest, pause, save/travel and defeat preservation remain covered |
| Morph-specific PIE | **29 checks PASS** after cold reload: Female/Male/Female, positive and negative limits, actual simulation, pause and movement; frontal captures reviewed |
| Ladder with new physics | PASS at x1/x2/x4; approximately 5.15/5.19/5.29 simulated seconds, walking restored and no residual effort |
| Cook/package | PASS, UAT exit 0. **6,221 packages accounted for: 6,209 cooked and 12 platform-skipped; 0 errors, 26 warnings**. `Package_20260930_151131` |
| Packaged progression | PASS: reading, x1/x2/x4, pause/resume, cue aliases and absence of premature quest errors. `PackagedQA_20260930_151552` |
| Packaged morph physics | PASS: Female/Male/Female profiles, **35 live body reports** across five samples, valid constraints, movement input and verified pause/resume. `MorphPackaged_20260930_151612` |
| Visual review | 12 recorded views: character genders, Manny/Quinn, ladder, reading, HUB respawn and both packaged suites. Packaged gender captures are rear/side views; the frontal comparison was performed in PIE |
| Protection and saves | **7,643 protected files, zero differences** at snapshot `81fb51cd`. Final suites restored SaveGuard; packaged runs used isolated UserDir. Original user HUB/Chest/doc/tool edits remain untouched and unstaged |

The 827-Blueprint gate and general suites preceded the final native respawn adapter. The final HUB respawn suite recompiled the affected Player/BP_TSChar, and both final builds and package include the adapter. No Blueprint graph changed after the full scoped audit. Prior bow, quest and mathematical balance evidence below remains historical; this continuation does not claim to have rerun every one of those scenarios.

The reviewed package is at `Saved/Analysis/PracticeProgression/Package_20260930_151131/Staged/Windows`.

## Log comparison and fixture corrections

The comparable Visual logs from 2026-09-28 and 2026-09-30 show missing morph-constraint warnings falling from **28 to 0**. Old ACF script-import and missing-blink counts remain zero. SetFontSize remains 1, localized Engine Condition failures 12, external configuration timeout 1, and dispatcher ownership warnings 969. The cook summary remains at 26 warnings. No log suppression, severity reduction, language change or functionality disabling was used to obtain these results.

Intermediate failures were retained: the initial candidate profiles lost their edited limits on reload until DefaultProfile serialization was corrected; the old setter updated the solver without its reflected profile. Respawn then exposed a different visible-mesh relationship on base Player, requiring an identical-primary-mesh fallback.

**Scope correction:** the inherited respawn harness initially used the production default `DungeonGeneration`. Its failed attempts, and a later numerically green attempt hidden behind a dungeon failure overlay, are not accepted as non-Calysto validation. The final harness changes only its process-local defeat settings to HUB, forces a real reload and asserts the destination. `Respawn_20260930_150824` passed all 27 checks with an unobscured gameplay capture. No Calysto repair or validation is claimed.

An earlier diagnostic editor crashed in Python teardown, and another was interrupted. Their original saves were restored. Final diagnostic sessions and validation processes exited cleanly. Test-regenerated `DT_ProjectAttributesSetByCallers` bytes were backed up and restored to the pre-test baseline before packaging.

## PENDIENTE and BLOQUEADO

| Status | Residual / next concrete requirement |
|---|---|
| BLOQUEADO | Protected ACFU SetFontSize graph and duplicate DefaultSlot nodes: original vendor graph repair is outside the allowed write scope. Runtime typography impact and exhaustive montage equivalence remain PENDIENTE; a warning alone does not establish broken behavior |
| BLOQUEADO | Engine English-literal assertions under Spanish locale, MetaHumanCoreTech cook registry query and UECP dispatcher ownership: retain the demonstrated source/paired-test causes below; fixes to Engine/Marketplace implementations are forbidden |
| BLOQUEADO | Protected Daz/animation metadata and legacy ACF ini/redirect contracts: no mass resave or removal solely to silence warnings |
| PENDIENTE | Authoritative compatible replacement for vendor Quinn's missing post-process rig; no guessed redirect was added |
| PENDIENTE | External free_tier_config availability and offline behavior beyond its observed cached fallback; no credentials changed |
| PENDIENTE | Exhaustive animation/Daz permutations and RealisticBlood upper-body montages beyond the tested views; human balance approval and a continuous integrated multi-day world run are not certified by the earlier mathematical simulations |
| EXCLUIDO | All Calysto repairs and its execution-dependent PCG checks, per the user's replacement decision |

Reusable guidance was updated and validated at `A:/Skills Codex/noshellforwinter-character-action-qa/SKILL.md` and `references/runtime-api-notes.md`: physics ownership, serialization, cold reload, reflected versus solver limits, defeat restoration, HUB-only respawn and save/process isolation.

---

# Historical compatibility validation — 2026-09-28

Target: D:/Projects UE5/NoShellForWinter. Source: D:/Projects UE5/LustAsDeadlySin (read-only).
Baseline: c9993314b38704cc89f3d579a8fcfb797ddfb38f.
Final implementation snapshot: 2d5d606e7ab7fedbc99033daabeee6f478debd4f.
Machine-readable evidence: [CompatibilityClosure_20260928.json](Evidence/CompatibilityClosure_20260928.json).

Project-owned repairs and their final runtime checks are complete. **The global Blueprint gate remains FAIL (three assets); the project is not certified free of compatibility errors.** Vendor/Engine restrictions and missing authored physics data prevent the remaining repairs described below.

## Character appearance and rejected evidence

The malformed Male screenshot was a test fixture error: directly replacing the skeletal mesh retained Female material overrides. It was not a planned appearance change. All original blink_male_* captures are INVALID. No Player or body mesh asset was saved from that test.

Corrected tests use the character creator SelectGender path. Male and Female open/closed eyelid captures were inspected; creator_male_pulse.json and creator_female_pulse.json show eyelids returning to zero. Protected asset hashes remain identical to the task baseline.

## Implemented changes and observed results

Unless otherwise stated, named evidence files are under Saved/Analysis/CompatibilityClosure.

| Change | Result and evidence | Commit |
|---|---|---|
| Exact old ACF script package redirects; obsolete developer-setting field removed; Daz bridge ControlRig dependency declared | Old script import warnings 2 to 0 in comparable Visual logs; installed vendor class redirects retained | 41e348a1 |
| Current Female/Male eyelid morph defaults | Creator gender route and blink visuals PASS; creator_male_pulse.json, creator_female_pulse.json | 825b4d93 |
| Project-owned sustained bow action uses simulation time | Pause freezes charge; x1/x2/x4 match game time. Real impact consumes ammo 18 to 17 and reduces receiver health 504 to 462.600769. bow_pause_clock_fixed.json, bow_simulation_speeds_fixed.json, bow_fixed_impact.json | 4f8f238c |
| Restore existing Down montage in project AS_ACF_Player | Up/down, top/bottom exit, Dodge cancellation and pause PASS. Final x1/x2/x4 climb uses 5.166/5.224/5.338 game seconds, 5.140/2.609/1.328 real seconds; all exit Walking, effort zero. ladder_time_speeds.json, ladder_pause_fixed.json, ladder_dodge_cancel.json | 7f890d27 |
| Actual active combat/traversal effort feeds existing max-combined integrator | Attack .8, bow .45, climb .8; idle/cancel zero. Bow sweat peaks 4.45947 and returns to zero after rest. physical_effort_bow_lifecycle.json. General reload/defeat/respawn checks also PASS | 6d4740eb |
| Own spline AI controller prevents native AlreadyAtGoal double advance | quest_end_to_end.json: 19 checks PASS; NPC actually traverses x=500 to 1021.33, four objectives complete, native save/load succeeds, replay/restart rejected. Authored quest has no XP reward; XP remains 75 | 4cc0f6dd |
| Isolated fixtures, regression checks and evidence tools | SaveGuard, current-player input, time/pause and lifecycle checks; no production test NPC added to HUB | 5913a772 |
| LevelBlock construction handles absent LevelVisuals | Replace array-first-element access with nullable GetActorOfClass, retaining existing IsValid branches. Both parent and traversable child compile. Present/absent manager PIE construction PASS; final cook has no empty-array warning | 2d5d606e |

Bow repair copies/reparents the charged action into /Game/_Game/Blueprints/Compatibility and changes only the own ability-set instance. The spline fix preserves both copied Blueprint controller layers and changes only ACFGuy's controller assignment. Compare these copies against vendor updates in future upgrades.

Quest reproduction identified a synchronous native MoveTo callback advancing the spline, followed by a second advance in the AlreadyAtGoal return branch. The own virtual MoveTo adapter guards only this consumed arrival. Evidence: quest_boundary_before_fix.json and final Session_20260928_210915/editor.log. Tutorial UI was visible in quest_final_completed00001.png. Audio and an exhaustive player dialogue-button flow remain PENDING (diagnostic process used -NoSound).

LevelBlock evidence: Graphs/LevelBlock_before.json, Graphs/LevelBlock_after.json, level_block_compile.json, level_block_runtime.json, level_block_valid00000.png and Session_20260928_213425/editor.log. No Calysto gameplay was run.

## Final validation gates

Paths below beginning with a run name are relative to Saved/Analysis/PracticeProgression.

| Gate | Final evidence | Status |
|---|---|---|
| Editor build | Saved/Analysis/CompatibilityClosure/build_editor_final.log; official wrapper, Daz receipt restored/enabled | PASS |
| Game build | Saved/Analysis/CompatibilityClosure/build_game_spline.log; all final runtime C++ included | PASS |
| Canonical automation | Automation_20260928_204032/index.json: 27 succeeded, zero failures/warnings/not-run | PASS |
| General visual/gameplay PIE | Visual_20260928_211117: 46 checks; Activities_20260928_211322: 81 | PASS |
| Save/travel/reload | Reload_20260928_211611: 23 checks; HUB, StorySelection, MenuMap, new HUB | PASS |
| Defeat/respawn cleanup | Respawn_20260928_211822: 24 checks, including active effort cancellation | PASS |
| Full Blueprint audit | Saved/Artifacts/PendingValidation/BlueprintCompile/20260928_212357/AllProjectBlueprintCompile.json: 901 compiled, 3 failures, zero on-disk changes | FAIL |
| Last changed Blueprint pair | level_block_compile.json: LevelBlock and LevelBlock_Traversable up to date; performed after full audit | PASS |
| Cook/package | Package_20260928_214148/uat.log: 6,219 packages, 0 errors, 26 warnings, UAT exit 0 | PASS with warnings |
| Packaged runtime | PackagedQA_20260928_214749/receipt.json: reading, x1/x2/x4, pause, cue aliases and no premature quest errors | PASS within this scope |
| Packaged visual QA | Same run UserData/Saved/Screenshots/Windows: practice_x200000.png, practice_x400000.png, practice_paused00000.png all inspected | PASS within these views |
| Protected invariants | protection_final.json: 7,643 files checked, zero differences | PASS |
| Save isolation | Final PIE sessions and four general suites restore SaveGuard; packaged run uses isolated UserDir | PASS |

General PIE totals 174 checks. Final packaged checks do not independently retest every bow, ladder or quest interaction. Build logs retain existing deprecated API warnings in unrelated diagnostic helpers; cook success is not a warning-free project claim. The final full Blueprint audit preceded the last LevelBlock repair; affected parent/child compilation and final cook/package were repeated afterward.

Baseline cook Package_20260928_173428 summarized 35 warnings; final cook summarizes 26. Counts are scoped to cook summaries, not every log occurrence. log_comparison.json compares baseline Visual_20260928_183115 with final Visual_20260928_211117: old imports 2 to 0; missing blink 1 to 0; vendor SetFontSize 1 to 1; localized startup Condition failures 12 to 12; external HTTP timeout 1 to 1; dispatcher ownership warnings 969 to 969. No severity suppression or persistent language change was used.

## Thirty-day balance experiment

No balance parameters were changed. Native ThirtyDayBalance tests and thirty_day_balance.csv exercise the actual practice formulas under x1/x2/x4 partitions.

| Initial rank | Continuous repetition | Daily 6 hours with rest | Three separated 2-hour sessions |
|---|---:|---:|---:|
| 0 | 1 | 15 | 20 |
| 25 | 25 | 32 | 35 |
| 75 | 75 | 78 | 80 |

mixed_thirty_day_balance.json models daily 2h Strength + 2h Intelligence + 2h Cunning, with remaining time resting/other activities. Starting ranks 0/25/75 end at 9/28/76 respectively per attribute. Simulation partitions and model checkpoint restoration agree.

This is mathematical validation, not a thirty-day integrated world playthrough or human balance approval. It assumes replenished needs and available books/locks. Separate PIE checks cover critical needs, rest, sweat, save, travel and defeat. Tools/Migration/Analyze-CompatibilityLongSessions58.py records formula-source hashes and cross-checks the native CSV.

## Remaining failures and compatibility risks

| Item | Established cause / evidence | Remaining work and status |
|---|---|---|
| MorphPhysics | Seven named bones exist but corresponding constraints are absent: l_pectoral, r_pectoral, LeftGlute, RightGlute, LeftThighFat, RightThighFat, Belly. physics_actions.json, mesh_bones.json | PENDING authored/calibrated physics bodies and constraints; renaming bones cannot repair this. Protected meshes unchanged |
| Calysto BP_CalystoController | Missing Generate/RandomizeSeed and incompatible actor-array pins in global audit | FAIL; separate Calysto integration required, outside this task's explicit scope |
| RealisticBlood demo ABP_Manny | Unknown AnimSequenceBase in connected SequencePlayer/ApplyAdditive base pose; realistic_blood_unknown_node.json | FAIL; recover authoritative animation/pose, do not invent a replacement |
| Archived TattooShop WBP_TattooCustomization | Incompatible Self/Target links under /Game/_MigrationBackup/TattooShop/20260811-231427; no registry referencers found | FAIL; preserved backup, not deleted to hide the gate |
| Compass SetFontSize | Vendor ANS_MapMarker_WBP:SetImage has orphan inFontSize=10 and current DisplayFontSize=0; Graphs/ANS_MapMarker_WBP.json | BLOCKED direct vendor edit; requires value migration, not orphan deletion. Runtime font impact PENDING |
| Duplicate DefaultSlot | Two vendor ACF_Template_ABP slot nodes; project child has no own function graphs. Climbing/traversal montages use this contract; parent_anim_slots.json | BLOCKED vendor graph change; tested actions work, exhaustive montage coverage PENDING |
| Daz/animation empty engine version | Protected Daz materials and some climbing/Arrow assets carry old metadata | Tested visuals/actions PASS within scope; all permutations PENDING. No metadata-only bulk resave |
| PCGEx CachedPins | UE5.8 base property collision and pre-1.76 vendor migration to CachedPinsEx | pcg_migrated_output_pins.json confirms loaded ExecutionDependency/RoamingData/Out. Execution PENDING; vendor resave/update blocked. Old EditorDomain type warnings not arbitrarily redirected |
| MetaHuman cook registry query | MetaHumanCoreTech.dll queries with bIncludeOnlyOnDiskAssets=false while loading MHPerf_Default | BLOCKED precompiled vendor/Engine implementation; byte-identical cook determinism PENDING |
| Engine Condition failed | Paired process-local culture test: startup 12 to 0 in English; English-literal assertions in UnifiedErrorTests.cpp:479–533 | CONFIRMED localization issue, Engine repair BLOCKED. User language unchanged |
| UECP shutdown unregister | UECPAIExtModule.cpp:370–374 unregisters under core ownership; dispatcher refuses handlers owned by AI | BLOCKED vendor fix. Editor exit does not certify retained handlers harmless |
| External free_tier_config | FreeTierConfigManager.cpp:132–163 HTTP timeout with cached fallback | External availability/offline capability PENDING; no credentials changed |
| Legacy vendor ini / substring redirects | Deprecated BaseAscentCombatFramework.ini and six installed compatibility aliases | Retained contracts; vendor migration BLOCKED, no performance claim |
| Other existing missing resources | Vendor SKM_Quinn references missing rig ABP_Quinn_PostProcess; two optional Compass Icon_Circle paths absent | PENDING authoritative replacements/reference ownership; no guessed redirects |

Exploratory culture suites were not green: default 34/41 and English 36/41 passed. Five shared failures lacked the canonical legacy Doctrine fixture configuration; two default-culture failures compare English literals. The separate canonical 27-test run uses its required process fixture without altering production Doctrine settings.

## Fixture recovery, preservation and reusable instructions

The accidental test cancellation was recovered. Failed intermediate fixtures remain as diagnostic evidence, not PASS: direct mesh swap, stale LocalPlayer input after PIE restart, positional Rotator assumptions, missing static mesh assignment, incorrect ladder top-entry argument and a holstered bow. Quest load initially ran before the save callback released its busy state; the final harness waits another tick. Static navigation access was corrected to use the actual world instance.

Session_20260928_210035 was closed after inspecting an unrelated broken demo triggered a blocking compile modal; bounded Computer Use recovery could not connect. Own changes had already been saved, and SaveGuard restored. A fresh session completed the valid tests. Background throttle was temporarily adjusted only on the editor CDO for reliable projectile frames and restored without saving preferences.

No source-tree, Marketplace or Engine files were changed. Player, current Female/Male/Multiple/Frederick, Daz/ACFU and the baseline HUB bytes remain protected. The user's existing HUB edit, Chest folder, ChestMoveRollback.md, ProjectSizeCleanup_20260924.md and Invoke-SafeGeneratedCleanup.ps1 remain unstaged and untouched. A test-regenerated own attribute table was backed up under OwnedAbilitySetsBeforeRepair and restored to its clean baseline. No broad deletion or backup removal was performed.

Reusable skill saved and validated at A:/Skills Codex/noshellforwinter-character-action-qa/SKILL.md, with references/runtime-api-notes.md and agents/openai.yaml. It is discoverable through the user's .codex/skills junction and records these fixture safeguards without treating historical results as future PASS.
