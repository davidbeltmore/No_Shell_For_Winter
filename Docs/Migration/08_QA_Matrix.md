# NoShellForWinter QA matrix

Status: `CANONICAL_REFACTOR_FINAL_VALIDATION`

Allowed results are `PASS`, `PASS_EXPECTED_DELTA`, `FAIL`, `BLOCKED_EXTERNAL`, and `PENDING`. A scope note never converts an unexecuted gate into `PASS`.

| Area | Required validation | Current result | Evidence |
|---|---|---|---|
| Protected ACFU and DazToUnreal plugins | File-count and SHA-256 baseline comparison | `PASS` at session baseline | `00_Invariants.md` |
| Protected player and character assets | Player, Female, Male, Multiple and Frederick hash comparison | `PASS_EXPECTED_DELTA` for documented pre-existing target differences; recheck after every phase | Session baseline outside public history |
| UE 5.8 native build | UHT, Development Editor and Development Game | `PENDING` after canonical refactor | Build logs |
| Blueprint integrity | Compile all target-owned Blueprints without saves, then compile intended full content set | `PENDING` | Blueprint compile report |
| Inner Doctrine | Attributes at levels 0, 4, 5, 9 and 10, positive and negative cases | `PASS` | Full canonical automation: 79/79 completed, 0 failed |
| DXP | Source allowlist, Charisma bonus scope, and zero reward from retained adult interactions or defeat presentation | `PASS` for the existing progression contract; final routing integration is `PENDING` | `NoShellForWinter.InnerDoctrine.DxpScope` and progression tests |
| Curse boundaries | 0, 49.99, 50, 75, 99.99 and 100; single Cursed episode, no refresh, correct residual | `PASS` | `BoundariesAndIdempotence` |
| Curse applications | Authority, unique application ID, duplicate rejection and source classification | `PASS` | `ApplicationContext` and `BoundariesAndIdempotence` |
| Curse resistance | Multiplicative composition, category modifiers and 25-percent floor | `PASS` | `ResistanceComposition` |
| Curse persistence | Map travel persistence, defeat clamp and out-of-combat decay | `PASS` | `TravelPersistenceAndDefeatClamp`, `ExternalDecayMultipliers` |
| Cursed and recovery | Duration, movement multipliers, recovery damage penalty and normal completion | `PASS` | Canonical Curse/status automation |
| Guard Recovery | Trigger, capacity, overflow, expiration, synthetic/native feedback split and no absolute invulnerability | `PASS` | `NoShellForWinter.InnerDoctrine.Defensive.GuardRecovery` |
| Pain Spike | Increased reserve, final resources, zero-health-damage poise pulse and range | `PASS` | Defensive milestone branch in `GuardRecovery` |
| Locomotion composition | Normal, walk, crawl, Doctrine bonus, Cursed, recovery, Dizzy and Frenzy | `PASS` | Locomotion/status automation, including `RecoveredMomentumWindow` |
| Single-game presentation routing | No content selector or gameplay opt-ins; neutral primary presentation; Charisma-10 adult social unlocks; `-StreamerSafe` fail-closed suppression | `PENDING` after the final policy change | Updated routing automation and visible PIE |
| Intimacy eligibility | Verified adults, explicit consent, alive/conscious/non-hostile/out-of-combat/location gates and safe cancellation | `PENDING` integration recheck after Charisma gating | `FailClosedMatrix`, `ConsentIsBilateral`, `EligibilityBoundaries`, plus Charisma 9/10 boundaries |
| Secondary adult interactions | Intimacy and Fap unlock at Charisma 10, remain available in the single game, and grant no Curse, DXP, combat buff, forced recruitment or exclusive progression | `PENDING` after the final policy change | Social and provider-contract automation |
| Defeat presentation | Real minigame loss only; no Charisma or voluntary-consent gate; authoritative rolls 0, 0.099 and 0.10; payload persistence and no reroll | `PENDING` integration recheck after policy simplification | `PayloadResolutionIsIdempotent`, `RollBoundaries`, direct-respawn flow |
| Defeat statistical check | Seeded ten-percent distribution within declared tolerance | `PASS` | `SeededTenPercentRate` |
| Direct respawn path | Ninety-percent branch, ordinary defeat, retreat, cancellation and technical error return without character animation | `PASS` for the underlying flow; weighted single-game integration is `PENDING` | Six focused PIE defeat-flow cases completed successfully |
| Chronicle protected barks | SHA-256 `E76C4C4A50BB3DD98DB769BF820DF10BF81FFC6FB341E96FEB7B7B25DBF6185A` | `PASS` at session baseline; must remain byte-exact | Chronicle evidence |
| Chronicle weighted resolver | Solo/Group by Melee/Ranged/Mage/Fallback; neutral external substitutions at 90 percent, protected originals at 10 percent; eight-second cooldown; Streamer Safe always neutral | `PENDING` after the final weighting change | Boundary, seeded distribution, role matrix, cooldown and visible PIE |
| New game persistence | `ProjectInnerDoctrineV1`; previous private slot ignored without deletion | `PENDING` | Save/load automation |
| Redirect retirement | No temporary Core Redirect or redirector remains after project-owned resave | `PENDING` | Config, Asset Registry and Editor scan |
| Public-language scan | Source, config, docs, metadata, registries and packaged binaries contain no prohibited historical identifiers outside approved Chronicle payload | `PENDING`; documentation scan passes | `Public_Evidence_Redaction.md` |
| Visual QA | Single-game neutral HUD, Charisma-10 social entries, mature-defeat branch and Streamer Safe suppression in visible PIE | `PENDING` | Screenshots and reviewer record |
| Cook and package | Clean cook, IoStore/Pak, package launch and packaged single-game/Streamer Safe matrix | `PENDING` | Final cook/package logs after the policy change |

The project is not release-ready until every required `PENDING` gate has a result and all protected invariants have been rechecked.
