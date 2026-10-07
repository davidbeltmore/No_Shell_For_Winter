# Wait selector Slash binding — 2026-10-07

Target: `D:/Projects UE5/NoShellForWinter`, starting snapshot `df39b8e9793438fcd9378a2222e5a01d2531bfc3`. The user supersedes apostrophe with unshifted `/`, immediately left of right Shift on a US keyboard (`EKeys::Slash`).

The production Slate route, development input probe, wait-panel hint, HUD tooltip and existing bounded PIE/packaged fixtures now use Slash. P and -/= retain their native pause and stepped speed behavior. Waiting still simulates at x4 until its calendar deadline, then returns to x1; pressing Slash again closes the selector or cancels an active wait. No HUD layout, vendor plugin or asset change is part of this patch.

Read-only native MCP preflight: `EditorToolset.EditorAppToolset.IsPIERunning=false`; `editor_toolset.toolsets.scene.SceneTools.get_current_level=/Game/_Game/Hub/HUB`. Protected baseline: `Saved/Analysis/ACFUTime20261007/protected_slash_before.json`, PASS, 7,899 unchanged files.

Implementation snapshot: `0c6961bf`. Evidence root: `Saved/Analysis/TimeSlash20261007`; durable receipts/images under `Docs/Migration/Evidence/TimeSlash_20261007`.

| Gate | Result |
|---|---|
| Editor build | PASS — `editor_build.log`, 74.60 seconds; Daz receipt repaired |
| Game build | PASS — `game_build.log`, 101.74 seconds; Daz receipt repaired |
| Blueprint compile and PIE | PASS — `Saved/Analysis/TimeKeys20261007/PIE_20261007_140841/receipt.json`, 26 checks, including three Blueprint compiles, Slash open/close, x4 deadline completion, native clock agreement and P/-/= regression |
| Visual | PASS — `wait_selector00002.png` and `native_pause00002.png` inspected; Slash hint, slider/buttons and native pause footer visible |
| PIE shutdown and saves | PASS — process exit 0; SaveGuard restored original hashes |
| Cook/package | PASS ? `Saved/Analysis/TimeControl/Package_20261007_141307/uat.log`, 442.05 seconds, exit 0, 0 errors / 26 known warnings |
| Packaged Slash routing | PASS ? `Packaged_20261007_142041/receipt.json`, all nine expected states, exit 0; wait selector and native pause screenshots inspected |
| Protected release / restored editor | PASS ? `protected_slash_release.json`, 7,899 unchanged; canonical Daz-enabled launch, live MCP confirms HUB and PIE false |

Limits: 240 seconds for each running PIE/packaged fixture and 30 minutes for cook/package. These fixtures dispatch the real Slate key path; physical OS key injection is not claimed. Unrelated HUB/Chest/cleanup changes remain outside this patch.

The first run (`PIE_20261007_140619`) stopped at the x4 upper-bound step. Its log shows `ProjectWaitWidget_0` opened at frame 58, before the fixture's scheduled Slash action, pausing the world and invalidating that speed assertion. An out-of-sequence Slash input is inferred, not attributed to a particular actor. The fixture now records actual speed/pause/menu state for every step. One repeat with identical production binaries passed all 26 checks and exited normally; no gameplay workaround was introduced for the invalidated first run.

The final editor startup required a bounded MCP retry while HUB loaded. Once loading completed, native MCP confirmed the target map and no running PIE. Both this task's successful PIE run and packaged test exited with code 0. The historical teardown failure was not reproduced here; no engine shutdown fix is claimed.
