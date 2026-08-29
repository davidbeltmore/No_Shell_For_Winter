# Intimacy Climax rework evidence — 2026-08-02

Status: `IMPLEMENTED_SCOPED_RUNTIME_VISUAL_COOK_PACKAGE_BOOTSTRAP_PASS`

## Scope

This receipt records the current project-owned Intimacy rework. It does not authorize or record any write to the legacy reference project, ACFU, DazToUnreal, or protected player/character assets.

The implemented contract is:

- Lust is removed from the Intimacy runtime path.
- Player and Partner have independent temporary Climax values for the active session.
- A participant reaching Climax has an orgasm, can later climax again, and does not end the session. Only exit or cancellation ends it.
- Orgasm Rush is one session-local state; Talk and minigame bonuses only accelerate Player or Partner Climax.
- Active Intimacy removes exactly one percent of Curse maximum per second, with passive Curse decay suppressed for that ownership window to prevent double counting.
- Charisma 10 unlocks immediate `T`+`Hyphen`/`Subtract` and `T`+`Y` `Partner > Intimacy` for a compatible Male partner.
- The normal authored companion route honors authored social participation and allowed-location metadata. The targeted Charisma-10 adapter is restricted by an explicit allowlist containing the authored Male companions and registered Male Enemy classes. It may supply missing social or zone metadata for those targets and overrides hostile classification at Charisma 10; verified-adult, bilateral-consent, alive, conscious, and out-of-combat gates remain mandatory.
- Non-hostility plus ACF controller/global-battle state are revalidated during preflight and on every active-session tick. `Space`, `Enter`, and arrow navigation are not consumed while Intimacy is inactive.
- The native HUD exposes dual Player/Partner Climax presentation through the active HUD Theme.

## Validation matrix

| Gate | Result | Evidence | Notes |
|---|---|---|---|
| Scoped UE 5.8 Development Editor build | `PASS` | `Saved/Logs/IntimacyRework_EditorBuild_HostileTarget.log` | The updated project-owned Intimacy code and tests compiled and linked with `Result: Succeeded`. This scoped command disabled unchanged protected `DazToUnreal` and `EFCharacterCreationDazBridge`; it is not the canonical complete-plugin release build. |
| Canonical UE 5.8 Development Editor build | `BLOCKED_EXTERNAL` | `Saved/Logs/IntimacyRework_CanonicalBuild.log` | UBT stops during plugin rule discovery with `Result: Failed (RulesError)`: the protected installed `DazToUnreal` module rules type cannot be resolved. No project-owned Intimacy compilation failure is reported, and the protected plugin was not modified. |
| Focused Intimacy automation at final snapshot | `PASS` | `Saved/Migration/Automation/IntimacyRework_HostileTarget_20260802_131200/index.json`; `Saved/Logs/IntimacyReworkAutomation_HostileTarget.log` | 8 tests executed, 8 `Success`, 0 failed: Climax math/default state, content policy, Charisma/spatial eligibility, fail-closed matrix, partner defaults, Climax metadata, and Talk fallback. |
| Directed Blueprint compile/integrity without save | `PASS` | `Saved/Logs/IntimacyRework_BlueprintInspection_20260802_063939.log` | `/Game/_Game/Animations/Intimacy/Scenes/BP_IntimacyScene_0001` and `/Game/_Game/Widgets/Y/Main/WBP_ProjectEmoteMenu` compiled successfully without saving. This is not a compile of every target-owned Blueprint. |
| PIE functional entry, hostile Charisma target, Climax, Curse, and cancellation | `PASS` | `Saved/Migration/Evidence/IntimacyRework_RuntimePIE_HostileTarget.json`; `Saved/Logs/IntimacyRework_RuntimePIE_HostileTarget.log` | The test selected a real `ACFMeleeEnemyBPMale`, set Charisma to 10, and the binding-equivalent quick-start request opened `Actions.Together.0001Scene` despite the hostile target. Both Climax values advanced; Partner climaxed twice, then Player climaxed once. Player Rush became true while Partner Rush became false, the session remained active, Curse fell 3.2926 percent over 3.2122 gameplay seconds, and explicit cancel restored move/look input plus passive Curse-decay ownership. The earlier companion regression remains in `IntimacyRework_RuntimePIE_Final4.json`. |
| Static input binding contract | `PASS` | Project input/config and project-owned routing inspection | `T` targeting, `Hyphen`/`Subtract` immediate request, and `Y` menu routing are present. This is static plus binding-equivalent runtime evidence, not a physical operating-system keyboard capture of either chord. |
| Adapter/combat/input guardrails | `PASS` | `Saved/Logs/IntimacyRework_EditorBuild_HostileTarget.log`; `Saved/Migration/Evidence/IntimacyRework_RuntimePIE_HostileTarget.json` | The Charisma adapter includes registered Male Enemy classes and overrides hostile classification for Charisma 10. Adult/life/consciousness, consent, recent-combat lockout, ACF controller/global-battle state, and input isolation remain guarded; the hostile Enemy target path passed. |
| Visible `T`+`Y` menu QA | `PASS` | `Saved/Migration/Evidence/IntimacyRework_TYVisual_20260802_063527.txt`; `Saved/Migration/Evidence/IntimacyRework_TYVisual_20260802_063527/` | Visible PIE captures show the themed action menu, `Partner`, and its enabled `Intimacy` entry for the prepared Charisma-10 compatible target. Input was driven by the deterministic visual harness; no physical OS chord is claimed. |
| Visible active Intimacy HUD QA | `PASS` | `Saved/Migration/Evidence/IntimacyRework_HudVisual_20260802_063747.txt`; `Saved/Migration/Evidence/IntimacyRework_HudVisual_20260802_063747/` | Visible PIE captures show the active dual-Climax HUD and session feedback using the active HUD Theme. The run does not validate every HUD Theme preset separately. |
| Development Game cook/stage/package/archive | `PASS` | `Saved/Logs/IntimacyRework_UAT_HostileTarget.log`; `Saved/Migration/Package/IntimacyRework_HostileTarget_20260802/Stage/`; `Saved/Migration/Package/IntimacyRework_HostileTarget_20260802/Archive/` | Final Game compilation included `ProjectIntimacySubsystem`; the updated cook/build produced the hostile-target snapshot, then stage, Pak, IoStore, and archive completed with `BUILD SUCCESSFUL` and `ExitCode=0`. This scoped bootstrap does not clear the canonical Editor vendor blocker. |
| Packaged launch smoke and Intimacy media inclusion | `PASS` | `Saved/Logs/IntimacyRework_PackagedSmoke_HostileTarget.log`; `Saved/Migration/Package/IntimacyRework_HostileTarget_20260802/Stage/`; `Saved/Migration/Package/IntimacyRework_HostileTarget_20260802/Archive/` | The updated packaged executable initialized the engine, mounted Pak/IoStore, and exited with code 0; staged Intimacy media is present. |
| Packaged Intimacy interaction | `PENDING` | No packaged-session interaction receipt | The packaged smoke did not select a partner or exercise entry, HUD, Climax, Curse, cancellation, or Streamer Safe behavior. |
| Final protected invariant comparison | `PASS_NO_NEW_DELTA` | `Saved/Migration/Evidence/IntimacyRework_Final_ProtectedInvariants.json` | Pre and final both contain the same 74 historical mismatches: 0 new and 0 resolved. ACFU is 5043/5043 `PASS`; DazToUnreal is 213/213 `PASS`; Player, Female, Multiple, and Male hashes remain unchanged from the pre-change snapshot. Frederick remains `PENDING_RUNTIME_INSPECTION` because the baseline has no resolved authoritative path/hash. |

## Interpretation

The scoped Editor build, final 8/8 automation run, directed two-Blueprint compile, focused functional PIE, representative active-theme menu/HUD visual QA, Development Game cook/stage/package/archive, packaged launch smoke, media inclusion, and no-new-delta invariant comparison pass within their stated scopes. The canonical Editor build is `BLOCKED_EXTERNAL` by the protected DazToUnreal `RulesError`. Physical OS key-chord capture, every HUD Theme preset, packaged Intimacy interaction, Frederick runtime resolution, full-project Blueprint compilation, and broader release gates remain `PENDING`.
