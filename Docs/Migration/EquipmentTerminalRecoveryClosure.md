# Equipment terminal recovery closure — 2026-09-23

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.2, ACFU 4.4.2.
Continuation starts at `ecebb4c` with a clean tree. The user's “Continua”
authorizes the previously presented narrow EFProcedural recovery exception.
This closes the equipment work; it does not certify the unrelated full Calysto
V7 migration/release matrix.

**Equipment closure: COMPLETE.** All requested gates, including the previously
failing terminal recovery suite and the final corrected package, passed.

## Cause and correction

After native cleanup, the PCG runtime called the provider's final release on a
rejected, detached pre-travel graph. The provider correctly retained it because
the source world was gone. Its false result made the Director return before
terminal reconstruction, so automatic recovery never ran.

Commit `0121220` adds `IEFCalystoContentAttemptProvider::FinishRuntimeSnapshot`,
used by both the production runtime and lifecycle fixture. For a valid request
and an owned rejected detached snapshot, it drops only the runtime's local
reference. The Director still owns the exact graph and reconstructs it before
closing the request. PCG also clears the duplicate reference in its attempt
request. The provider's final release runs once, after reconstruction.

All other release results remain authoritative; no provider failure is converted
to success. Accepted detached graphs, foreign snapshots and invalid requests
cannot use this handoff. Ordinary release failure retains the exact graph.
Failed reconstruction retains the Director's graph and protected Retry state,
without another topology attempt or competing runtime lease.

Only three project-owned source files changed: the content-provider interface,
the PCG runtime terminal call, and the Director lifecycle tests. No assets,
catalogs, inventory owners, protected meshes or Marketplace files changed.

## Evidence

Paths are relative to the target unless absolute. Common evidence directory:
`Saved/EquipmentQA/TerminalRecoveryClosure`.

| Gate | Result | Evidence |
| --- | --- | --- |
| Read-only MCP context | PASS, target UE 5.8.2/HUB, no dirty packages | `editor-context.json` |
| Editor build | PASS, Daz repaired | `build-editor02.log` |
| Game Development build | PASS, Daz repaired | `build-game.log` |
| Lifecycle/snapshot suites | PASS, exactly 6, exit 0, no failures | `Saved/Migration/CalystoDungeonDirectorV7/Native_EquipmentTerminalRecovery01/StrictSummary.json` |
| Real HUB → floor 1 → floor 2 → HUB | PASS, 39 checks | `Saved/EquipmentQA/TerminalRecoveryTravel01/result.json` |
| Live PIE MCP context | PASS, correct target, PIE active, no dirty packages | `travel-live-context.json` |
| User-save preservation | PASS, all 7 verified/restored | `Saved/EquipmentQA/TerminalRecoveryTravel01/user_save_after.json` |
| Protected files after code | PASS, all 7,642 unchanged | `protected-after-code.log` |
| Protected Calysto assets | PASS, all 13 unchanged | `calysto-before.json`, `calysto-after.json` |
| Historical invariant drift | Unchanged, no new differences | `historical-drift-comparison.json` |
| 36-Blueprint preflight | PASS, no assets saved | `A:/NoShellForWinterMasterClosure/Archive_FINAL3/EquipmentPackagingPreflight.json` |
| Fresh cook/stage/Development archive | PASS, UAT exit 0 | `package.log`; `A:/NoShellForWinterMasterClosure/Archive_FINAL3.Package.log` |
| Packaged NativeEquipment | PASS, exactly 2 suites, exit 0, zero test warnings | `Saved/EquipmentQA/Packaged_Native_FINAL3/launch.json`, `Report/index.json` |
| Packaged NativeController | PASS, exact AuditedNpcProfilePreflight, exit 0, zero test warnings | `Saved/EquipmentQA/Packaged_Controller_FINAL3/launch.json`, `Report/index.json` |
| Packaged Visual | PASS, actual image reviewed, exit 0 | `Saved/EquipmentQA/Packaged_Visual_FINAL3/launch.json`, `User/Saved/EquipmentQA/Packaged/result.json`, `inventory.png`, `visual_done.txt` |
| Final protected comparison | PASS, all 7,642 unchanged against requested storage baseline | `protected-final.log`, `protected-final-comparison.json` |
| Editor/Game Daz receipts | PASS, VerifyOnly | `daz-editor.log`, `daz-game.log` |

The six suites retain their exact names in
`Tools/Equipment/Expected-TravelLifecycleTests.txt`. The formerly failing
`DetachedSnapshotTerminalRecovery` now includes successful automatic recovery,
failed recovery followed by explicit retry, invalid request, foreign ownership,
accepted detached rejection and ordinary provider-failure conservation.
The initial build attempt caught a pointer/reference comparison in the added
test; `build-editor.log` remains FAIL, superseded by `build-editor02.log`.

The historical Phase0 invariant baseline still reports its known old ACFU 4.3.5
and target Daz-asset drift. It is recorded separately in `historical-before.json`;
it is not the current-session baseline and has not been silently replaced.
The required storage/clothing baseline comparison remains strict.

## Final package and repository

Archive: `A:/NoShellForWinterMasterClosure/Archive_FINAL3`.
Executable: `Windows/NoShellForWinter/Binaries/Win64/NoShellForWinter.exe`.
SHA256: `FE504761515C631E0CF273A37BEDDEE1F5ACBA189F1F482CCE846ED87077C7F5`.
All three isolated launch receipts identify this exact executable. Prior archives
remain untouched. `source-manifest.json` ties the compiled correction to `0121220`.

The actual screenshot shows the complete front-facing character, rendered rags
with authored tears and underwear coverage, readable inventory grid and controls,
five visible outerwear slots with scrollbar and the fixed underwear/satchel column.
No missing/exploded garments were observed. The visual marker was written only
after inspecting the image. The broader inventory, storage, lockpick and
Female/Male/Female evidence from [EquipmentMasterClosure.md](EquipmentMasterClosure.md)
remains applicable: those implementations and assets were unchanged here.

`Saved/EquipmentQA/TerminalRecoveryClosure/final_git.json` records the final
post-documentation commit, empty `git status --short` and successful
`git diff --check`. Evidence, archives, logs and backups remain outside Git.
