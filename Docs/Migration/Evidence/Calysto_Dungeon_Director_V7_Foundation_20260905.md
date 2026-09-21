# Director foundation and first structural correction

Target: `D:\Projects UE5\NoShellForWinter`, UE 5.8. Source project remains read-only.
Working snapshot: skill/workflow commit `973981f` plus the uncommitted native foundation;
initial dirty worktree and protected hashes remain in `SkillPreflight_20260905_042056`.

## Build and native tests

Protected cold Editor build succeeded at approximately 10:03 UTC. A test-only
implicit `FBox.IsValid` conversion was corrected to an explicit comparison.
The wrapper restored both Daz plugin states and reported its receipt guard PASS.

The first 50-test native run is **FAIL**, not a passing gate:
`Saved/Migration/CalystoDungeonDirectorV7/Native_20260905_050353/Native.log`.
The new 100,000-trial probability test completed successfully in approximately
0.34 seconds, followed by the boundary/identity and five transaction tests.
The structural navigation fixture then initialized an already initialized test
world a second time. Unreal exited with code 3 after the duplicate WorldSettings
fatal error. The supervisor also hung while draining redirected output and was
stopped after its exact owned process command line was verified. Both fixes are
implemented; a fresh successful process exit and exact inventory remain required.

## Warm runtime observation of the structural repair

The first cold probe hit the existing V6 preload watchdog before native geometry
could be inspected. It stopped at its 40-second diagnostic cap:
`EntryProbe_20260905_052121/evidence.json`.

One warm follow-up used run seed `2959332854660340481`, producing the exact failing
topology seed `1779679224`. Evidence:
`Saved/Migration/CalystoDungeonDirectorV7/EntryProbe_20260905_052326/evidence.json`.

After reconnecting the complete room stream to native geometry:

- Floor instances increased from 85 to 165. Floor bounds now span Y −4650 to 4650.
- Native Start remained at (−1350, −4050, 0); End remained at (−1350, 3450, 0).
- Both downward traces returned hits and both markers projected onto navigation
  at Z=10. The old runtime snapshot reported Ready and navigation-path ready.
- The old runtime-created bounds volume still had zero extent. The existing map
  bounds supplied coverage; this is not proof of corrected runtime registration.
- Generated floor components shown by the probe used the grey material. Full
  Theme precedence, boundaries, player traversal and visual acceptance are PENDING.

The probe stopped its owned PIE at 40 seconds and reported no dirty editor
packages before or after. This establishes the missing-room geometry defect and
its first-seed correction. It does not establish complete V7 gameplay readiness.

## Migration and candidate status

The first importer exported the unchanged V6 source and stopped before any save
because Python struct export omits default-valued fields inside nested arrays.
`ImportDirector.json` records FAIL, zero saves, and matching V6 hashes.
A new editor-only complete-value export helper is being compiled before retry.

The candidate runtime is unversioned and does not compile through V6 structures.
It remains explicitly gated during implementation. Its native parity fixture
requires empty population and blocked decals; those are declared test inputs,
not substitutions in the authored master asset. Full catalog, gameplay bridges,
material boundary ownership, loading performance and all release gates remain
PENDING. Neither authoring validation nor native parity reports Gameplay Verified.

Historical protected-baseline discrepancies remain separate and unchanged in the
preflight report. No historical baseline has been replaced.
