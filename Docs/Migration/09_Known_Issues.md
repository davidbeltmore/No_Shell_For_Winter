# Known issues

## NSW-0001 — Canonical gameplay refactor requires final runtime validation

- Severity: release blocker.
- Status: `IN_PROGRESS`.
- Scope: Inner Doctrine, DXP, Curse, Cursed, Guard Recovery, the seven attributes, single-game presentation routing, defeat outcome, Social, and retained adult-interaction integration.
- Intimacy rework status: final scoped Editor build, final 8/8 automation, directed two-Blueprint compile, focused functional PIE, representative active-theme visual QA, final Development Game compile plus incremental cook/stage/package/archive over the clean 6703/6703 baseline, packaged launch/media smoke, and final no-new-delta invariant comparison pass. Packaged Intimacy interaction is still pending, and the canonical Editor build is externally blocked by protected DazToUnreal rule discovery.
- Exit gate: resolve the vendor build blocker, canonical Development Editor/Game builds, full target-owned Blueprint compile, the remaining global PIE/visual matrix, packaged Intimacy interaction, and the other global closeout gates.

## NSW-0002 — Asset serialization migration remains

- Severity: release blocker.
- Status: `PENDING`.
- Scope: project-owned DataTables, DataAssets, Widget Blueprints, action IDs, icons, parameters, import metadata and string references.
- Constraint: use temporary redirects only inside the private migration process. The public final tree must contain no redirects or inherited redirectors.
- Exit gate: targeted Editor resave, redirector fix-up, Asset Registry scan, clean build and clean cook without redirects.

## NSW-0003 — Protected target baseline contains pre-existing differences

- Severity: invariant tracking risk.
- Status: `BASELINED`.
- ACFU and DazToUnreal plugin manifests match their protected baselines.
- A set of target-owned Daz assets differed before this refactor began. These differences are not attributed to the current work and must not be overwritten.
- `Saved/Migration/Evidence/IntimacyRework_Final_ProtectedInvariants.json` has the same 74 historical mismatches as the pre-change snapshot, with 0 new and 0 resolved. Player, Female, Multiple, and Male are byte-identical to the pre-change snapshot; Frederick remains `PENDING_RUNTIME_INSPECTION` because no authoritative path/hash is resolved in the baseline.
- Exit gate: compare every protected file to the captured session baseline after each migration phase.

## NSW-0004 — One immutable ACFU Blueprint has a vendor compiler defect

- Severity: external compatibility issue.
- Status: `BLOCKED_EXTERNAL`.
- Asset: `/AscentCombatFramework/Blueprints/Abilities/ACF_PickAction_BP`.
- Constraint: do not modify the Marketplace plugin.
- Exit gate: vendor update, or a project-owned adapter only if gameplay validation proves the action is required and broken.

## NSW-0005 — Chronicle weighted substitutions are not validated

- Severity: presentation-routing gate.
- Status: `PENDING`.
- The protected original bark payload has the required session-baseline hash.
- Remaining: verify the 90-percent neutral and 10-percent protected-original distribution, full Solo/Group and combat-role matrix, cooldown, Streamer Safe neutral enforcement, and packaged behavior.

## NSW-0006 — Intimacy Climax rework passes scoped runtime/visual/package bootstrap; packaged interaction remains

- Severity: packaging and presentation gate.
- Status: `PENDING`.
- Implemented native contract: Lust is removed from Intimacy. Independent temporary Player and Partner Climax values drive repeatable orgasms without ending the session; Orgasm Rush is one session-local state; Talk and minigame bonuses only accelerate a chosen Climax target.
- Implemented entry contract: at Charisma 10, `T` plus `Hyphen`/`Subtract` requests immediate Intimacy and `T` plus `Y` exposes `Partner > Intimacy` for a compatible Male partner.
- The normal authored companion route continues to honor authored social participation and allowed-location metadata.
- The targeted Charisma-10 route may supply missing social or zone metadata through the project-owned temporary adapter for authored Male companions and registered Male Enemy classes. It intentionally overrides hostile classification for those targets, but never omits verified-adult, bilateral-consent, alive, conscious, out-of-combat, or safe-cancellation gates. Combat lockout and ACF controller/global-battle state are revalidated at preflight and every active tick. The updated Enemy path passed the scoped build, 8/8 automation, hostile-target PIE, cook/package, and packaged smoke gates; packaged interaction remains pending.
- Intimacy removes exactly one percent of Curse maximum per active second and prevents passive decay from double-counting that rate. It still grants no DXP, combat power, forced recruitment, or exclusive progression.
- The dual Player/Partner Climax HUD follows the active HUD Theme and reports orgasm/Rush/Curse state.
- Mature defeat remains a separate dark-fantasy consequence: after a real minigame loss it uses one 10-percent roll independent of Charisma and Intimacy consent. The other 90 percent respawns directly.
- `-StreamerSafe` must suppress every adult presentation path without deleting its content.
- Scoped runtime/visual result: `Saved/Migration/Evidence/IntimacyRework_RuntimePIE_HostileTarget.json` selects a real `ACFMeleeEnemyBPMale`, starts successfully from Charisma 10 despite the hostile target, opens the Intimacy action/HUD, and advances both Climax values. Partner climaxes twice, then Player once; Rush transfers exclusively to Player (`Player=true`, `Partner=false`) and the session persists. Curse falls 3.2926 percent over 3.2122 gameplay seconds. Cancel restores input and passive Curse-decay ownership. The companion regression remains in `IntimacyRework_RuntimePIE_Final4.json`; visible captures pass for `Partner > Intimacy` and the active dual-Climax HUD Theme. `T`, `Hyphen`/`Subtract`, and `Y` bindings are statically verified; the receipts do not claim physical operating-system key-chord capture or separate coverage of every HUD Theme preset. `Space`, `Enter`, and arrows are not consumed by Intimacy outside an active session.
- Build/content result: `Saved/Logs/IntimacyRework_EditorBuild_HostileTarget.log` records `Result: Succeeded`; `Saved/Migration/Automation/IntimacyRework_HostileTarget_20260802_131200/index.json` and `Saved/Logs/IntimacyReworkAutomation_HostileTarget.log` record 8/8. The directed no-save compile of `BP_IntimacyScene_0001` and `WBP_ProjectEmoteMenu` remains pass. `Saved/Logs/IntimacyRework_UAT_HostileTarget.log` records final Game compilation including `ProjectIntimacySubsystem`, updated cook, stage, Pak, IoStore, and archive with `BUILD SUCCESSFUL`, `ExitCode=0`. The updated package is under `Saved/Migration/Package/IntimacyRework_HostileTarget_20260802/{Stage,Archive}`; `Saved/Logs/IntimacyRework_PackagedSmoke_HostileTarget.log` records engine initialization, Pak/IoStore mount, and exit 0.
- Remaining: exercise the complete Intimacy interaction inside the packaged executable, including partner selection, both entry routes, HUD, repeated Climax, Curse, cancellation, and Streamer Safe behavior. Physical OS chord capture and every HUD Theme preset also remain unclaimed. The canonical Editor build remains `BLOCKED_EXTERNAL` by the protected DazToUnreal `RulesError`.
- Fap remains mechanically separate. No other adult interaction inherits Intimacy's explicit Curse-cleanse exception.

## NSW-0007 — Final defeat routing requires revalidation

- Severity: gameplay gate.
- Status: `PENDING`.
- Existing probability and direct-respawn tests cover the underlying flow.
- Remaining: revalidate the final single-game integration with no Charisma or voluntary-consent gate, boundary rolls, seeded 10-percent distribution, payload persistence across travel, no reroll, direct respawn, and technical-error behavior.

## NSW-0008 — Procedural dungeon runtime is not release-complete

- Severity: gameplay gate.
- Status: `IN_PROGRESS`.
- Static assets and portions of level flow have prior UE 5.8 evidence.
- Remaining: StartPoint generation, PCG/navigation completion, dungeon cleanup, cook and packaged runtime.

## NSW-0009 — Full release validation remains

- Severity: release blocker.
- Status: `PENDING`.
- Required: resolve the protected DazToUnreal RulesError and complete canonical Development Editor/Game builds, compile all target-owned Blueprints, complete the remaining global single-game/Streamer Safe and HUD-theme visual matrix, exercise Intimacy inside the packaged executable, resolve Frederick's authoritative runtime path/hash, and run the final prohibited-name scan across EXE, DLL, PAK, UTOC and UCAS. Scoped Intimacy PIE, visual, cook/package bootstrap, launch/media smoke, and no-new-delta invariant gates do not promote this global issue.

## NSW-0010 — Private historical evidence is intentionally excluded

- Severity: public-repository hygiene.
- Status: `MITIGATED`.
- Detailed historical paths, repository fingerprints, import receipts and obsolete internal asset inventories remain outside the public repository.
- Public gate summaries retain honest outcomes without exposing the legacy private build.

## NSW-0011 — Canonical Editor build is blocked by protected DazToUnreal rules discovery

- Severity: external build blocker.
- Status: `BLOCKED_EXTERNAL`.
- Evidence: `Saved/Logs/IntimacyRework_CanonicalBuild.log` records `Result: Failed (RulesError)` while UBT expects the installed `DazToUnreal` module rules type.
- Constraint: DazToUnreal is a protected installed plugin and must not be edited or overwritten as part of this migration.
- Current scope: the project-owned Intimacy code passes the scoped Development Editor build, and Development Game cook/stage/package/archive bootstrap passes; neither substitutes for the canonical complete-plugin Editor gate.
- Exit gate: a compatible vendor installation/update or another authorized external resolution that allows the canonical complete-plugin Editor build to proceed.

See [Public_Evidence_Redaction.md](Evidence/Public_Evidence_Redaction.md).
