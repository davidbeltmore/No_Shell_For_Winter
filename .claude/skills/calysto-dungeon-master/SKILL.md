---
name: calysto-dungeon-master
description: Safely inspect, tune, extend, and validate Calysto procedural dungeons through the project-owned EFProcedural master plugin without editing BP_MassiveDungeon or Calysto core assets. Use for seeded runs, uncapped floor advancement, exact replay, same-floor reroll, End_Point doors, PCG seeds, floorless V2 policy tables, spawner/theme presets, and Dungeon Harness debug controls.
---

# Calysto Dungeon Master

The authoritative cross-agent runbook is `.agents/skills/calysto-dungeon-master/SKILL.md` with its `references/` and `scripts/`. Read it completely before inspecting or changing Calysto dungeon behavior.

Always keep `D:/Projects UE5/LustAsDeadlySin` read-only, begin with the native UE 5.8 MCP in read-only mode, and treat `/Game/Calysto` plus `BP_MassiveDungeon` as protected vendor core. Make durable changes only in `Plugins/EFProcedural`, thin debug integration in `EFProjectSystems`, and the three floorless policy tables under `/Game/_Game/Data/CalystoDungeon/V2`.

Phase 2 runs live only in the current GameInstance: there is no SaveGame resume. Floors are positive `int64` values without a Floor 3 cap. Follow the authoritative runbook for exact new-run, replay, reroll, advance, seed, policy-hash, authoring, and validation contracts, including the real ACF door gate from Floor 1 through Floor 10.
