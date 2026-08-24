# Dungeon Director V3 validation gates

Every gate records exact paths, commands, logs, screenshots, hashes, and a
commit or snapshot identifier. Unrun work is `PENDING`, never implicit PASS.

## Before mutation

1. Confirm target project, UE 5.8, and the dirty worktree.
2. Run read-only Unreal MCP preflight and record relevant toolsets.
3. Write an explicit protected-asset hash baseline under `Saved`.
4. Record live dirty states for the Calysto brain, graph, mesh, spawner, theme, retired policy assets, and the V3 policy.
5. Confirm the dungeon map has no baked dungeon actor.

## V3 policy authoring

Run `Tools/Migration/Create-CalystoDungeonDirectorPolicyV3.py` only after the
V3 native class compiles. When absent it may create and save exactly
`/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy`; when
present it must validate without overwriting or saving. Its receipt must derive
the actual Content delta, prove `calysto_asset_mutations=[]`, and validate the
class, versions, PolicyId, and validated sizes.

## Static and automation

- Build with `Tools/Migration/Build-NoShellForWinterEditor58.ps1`.
- Validate schema, references, IDs, ranges, PERT/Bernoulli, progression, RNG-domain independence, ecology, pity, cooldowns, adaptation, budgets, and canonical hashes.
- Prove same-seed restart and replay exactness; prove reroll change; prove retry does not double-commit ecology.
- Prove the EFProjectSystems before-Advance bridge submits finite/clamped outcomes for both Harness and real-door Advance; missing health/needs/timing signals must resolve to `0.5` neutral.
- Cover zero and 25 enemies, food/chest extrema, invalid numeric input, anchor shortage, duplicate callbacks, and fail-closed recovery.
- Scan active code/config/assets for pre-V3 runtime paths, structs, presets, and aliases before retirement.
- Compile affected project Blueprints in memory without saving protected packages.
- Re-hash all protected Calysto, ACFU, DazToUnreal, Player, Female, Male, Multiple, and Frederick invariants.

## PIE UE 5.8

Use `Tools/Migration/Run-CalystoDungeonMasterPIE58.ps1` and inspect both JSON
and log. Require fixed-seed new run, exact replay, changed reroll, same-seed
restart, one runtime dungeon/door/PCG, real ACF advancement through Floor 10,
debug jumps to 25/50/100, and the telemetry order:

`PCGComplete <= NavigationPathReady <= PopulationRealized <= DoorEnabled`.

Exercise each validated size with at least 20 seeds. Any failing size stays out
of `ValidatedDungeonSizes` and is `PENDING`. Cover zero/cap population,
resource extrema, outcomes, shortage, timeout, callback loss, and HUB recovery.
The validator must measure on-disk Content deltas and dirty-package saves;
normal PIE requires both arrays empty.

Reject Blueprint Runtime Error, PCG errors, ensure, fatal, duplicate generation,
`cancelled or cleaned`, `Object Transform`, and `GetAttributeFromPointIndex_0`.

## Statistics, soak, performance, cook, and package

- Resolve 100,000 intents/manifests across depth bands and Floors 1–1000.
- Require probability results within six-sigma binomial tolerance and increasing median scale/threat by depth band.
- Run at least 25 live generations without monotonic actor/memory/residue growth.
- Require Floor Ready P95 <= 1.25× comparable baseline and always below 30 seconds.
- Perform fresh non-iterative cook and Development/Shipping packages.
- Prove V3 policy/catalog inclusion and absence of retired policy tables from the cooked manifest.
- Smoke Floors 1–10 plus zero-enemy and cap-25 scenarios in packages.
- Confirm debug jump, intent controls, and sampling are inert in Shipping while production travel and the real door work.
- Re-run protected hashes after every stage.

V3 is complete only when build, automation, PIE, visual QA, cook, Development
package, and Shipping package are all PASS. Document exact blockers as PENDING.
