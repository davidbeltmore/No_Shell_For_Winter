# Known issues

## NSW-0001 — Canonical gameplay refactor is incomplete

- Severity: release blocker.
- Status: `IN_PROGRESS`.
- Scope: Inner Doctrine, DXP, Curse, Cursed, Guard Recovery, the seven attributes, single-game presentation routing, defeat outcome, Social, and retained adult-interaction integration.
- Exit gate: cold UE 5.8 builds, native tests, Blueprint compile, PIE, visual QA, cook and packaged runtime.

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

## NSW-0006 — Single-game adult-content routing is not finalized

- Severity: packaging and presentation gate.
- Status: `PENDING`.
- Final requirement: one neutral-primary game with no SFW/NSFW selector and no gameplay-content opt-ins. Intimacy and Fap unlock at Charisma level 10.
- Intimacy still requires verified adult characters, explicit consent, life, consciousness, non-hostility, no active combat, an allowed location, and safe cancellation.
- Mature defeat remains a separate dark-fantasy consequence: after a real minigame loss it uses one 10-percent roll independent of Charisma and Intimacy consent. The other 90 percent respawns directly.
- `-StreamerSafe` must suppress every adult presentation path without deleting its content.
- Remaining: remove obsolete selector/opt-in integration, validate Charisma 9/10 boundaries and every live entry point, run visible single-game/Streamer Safe QA, audit Blueprint references, and verify the final cooked dependency closure.
- Adult interactions must remain mechanically isolated from Curse, DXP, combat power and forced recruitment.

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
- Required: final build, all target-owned Blueprints, visible PIE for the single-game presentation and Streamer Safe override, visual QA, clean cook, packaged launch and final prohibited-name scan across EXE, DLL, PAK, UTOC and UCAS.

## NSW-0010 — Private historical evidence is intentionally excluded

- Severity: public-repository hygiene.
- Status: `MITIGATED`.
- Detailed historical paths, repository fingerprints, import receipts and obsolete internal asset inventories remain outside the public repository.
- Public gate summaries retain honest outcomes without exposing the legacy private build.

See [Public_Evidence_Redaction.md](Evidence/Public_Evidence_Redaction.md).
