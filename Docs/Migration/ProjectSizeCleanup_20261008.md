# Safe project size cleanup — 2026-10-08

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.
Source: `D:/Projects UE5/LustAsDeadlySin`, untouched (no source-tree operations).
Initial snapshot: `4821abcf`; prior local HUB/chest edits preserved in `951f99e9`.
Evidence: `Saved/CleanupEvidence/20261008/`.
Versioned compact receipts: [index.json](Evidence/ProjectSizeCleanup_20261008/index.json).
Tooling snapshot: `9495cfbd`; all gates use the unchanged game source from
`4821abcf` plus the preserved HUB/chest state in `951f99e9`.

## Measured reduction

Initial recursive measurement including `.git`: 153,948,152,480 bytes,
81,262 files (143.375 GiB). The image supplied by the user agrees with this size.
`Saved` accounted for 112,696,496,972 bytes; productive `Content` was
11,111,254,879 bytes. Repeated validation packages were the primary cause.

The first cleanup removed 99,241,939,855 bytes (92.426 GiB), consisting of
46 `Cooked`/`Staged` output directories inside old `Package_YYYYMMDD_HHMMSS`
folders under `Saved/Analysis`, `Saved/Migration` and `Saved/BodyPresetsQA`.
Every candidate was classified, checked for project containment and quarantined
before removal. Logs, screenshots, reports, saves, autosaves, backups and the
most recent successful package were retained. No productive Unreal asset was
deleted, renamed, moved or saved. No plugin was removed or modified.

Final measurement including `.git`, rebuilt binaries, caches, retained package and
evidence: **54,821,201,697 bytes (51.056 GiB), 79,196 files**. Net reduction:
**99,126,950,783 bytes (92.319 GiB, 64.39%)**. `Content` remains exactly
11,111,254,879 bytes. Minor log/evidence growth while the editor runs is expected.
Final breakdown: `.git` 13.473 GiB, `Saved` 12.634 GiB, `Content` 10.348 GiB,
`Intermediate` 9.322 GiB, `Plugins` 4.077 GiB, `Binaries` 1.124 GiB.

## Configuration recovery

The initial local `Config/DefaultGame.ini` diff removed 119 committed lines
and added two blank lines. This predated this task. The preceding Codex session
`01a11d24-81cf-7d02-9519-b7c4e637e6a4` already recorded that change as pre-existing
at 20:12 UTC and preserved it; its onboarding baseline contained only 54 lines.
No intentional removal was established from the inspected session records.
Later explicit changes in that session added packaging rules, not removal of
character, Calysto, clothing, HUD, cue, progression or blink configuration.

The pre-recovery file is preserved at
`Saved/CleanupEvidence/20261008/DefaultGame.pre-recovery.ini`.
The missing configuration was recovered from `4821abcf`. All deliberately added
body/clothing cook roots were already in that commit and were retained.
The recovered file matches Git HEAD exactly. This recovery is the sole expected
productive-file hash difference relative to the initial cleanup baseline.

## Validation gates

| Gate | Result and evidence |
| --- | --- |
| Target editor / native MCP | PASS: UE 5.8 target opened and finally reopened through the protected launcher; fresh MCP lists/describes toolsets, current level `/Game/_Game/Hub/HUB`, PIE false, HUB not dirty |
| Cleanup-only protection | PASS: `protected-after-packages.json`, all 11,206 baseline files unchanged |
| Editor cold build | PASS: `editor-build.log`, Win64 Development, 105.70 s; Daz receipt repaired |
| Chest Blueprint compile | PASS: `chest-blueprints.json`, original deposit and locked-chest paths up to date |
| Player / creator compile and PIE | PASS: fresh runtime fixture, 24.231 s, body switches, poses, movement, checkboxes, Apply/reopen and Cancel |
| PIE visual review | Sampled character/creator capture inspected; narrow docked viewport wraps preset labels. Full resolution/layout acceptance is PENDING; no widget changes in this task |
| Game build | PASS: `game-build.log`, Win64 Development, 99.98 s; descriptor restored byte-exact and Daz receipt repaired |
| Fresh cook / package | PASS: `Package_20261008_194816`, fresh cook, UAT exit 0, 325.838 s wall time, 0 errors / 26 warnings |
| Packaged runtime / visual | PASS for sampled HUB/body/creator gate: `PackagedQA_20261008_195347`, assertions 23.728 s, total 40.589 s, exit 0, zero feature or startup errors; inspected 1400x1000 creator capture, both presets legible and Preset 2 selected |
| Final productive-file comparison / save restoration | PASS: `protected-post-validation.json`, 11,206 files, zero unexpected changes; only recovered DefaultGame.ini differs. Fresh PIE SaveGuard restored every original save |
| Calysto protected assets | PASS: `calysto-final.json`, 13 assets unchanged against this task's pre-recovery snapshot |
| Final ACFU comparison | PASS: `acfu-post-validation.json`, all 7,425 files present, zero added/missing/changed files after cook and packaged QA |

The historical Phase 0 invariant check remains FAIL with 4,428 pre-existing
differences (4,217 ACFU, 207 target DAZ asset-set entries and four authoritative
assets). Daz plugin set itself has zero differences. See
`historical-invariants.json` and its log. This is separate from parity against
this task's baseline; no historical drift was silently rebased or repaired.
These scoped gates do not certify the entire migration or every gameplay system.
The initial runtime launch overlapped the unfinished cold build and was interrupted;
its SaveGuard restored the originals. Only the subsequent fresh, cleanly exited
fixture (`runtime-qa-final.log`) supplies the passing PIE gate.

The retained package is now `Saved/BodyPresetsQA/Package_20261008_194816`.
After its successful packaged QA, a second guarded cleanup removed the prior
retained package's two generated directories: 4,334,084,877 bytes. Its reports,
screenshots and logs remain. No test package or cook output is committed to Git.

## GitHub and retention

The 99 previously unpublished commits through `4821abcf` were pushed to
`origin/feature/efclothing-v4-multicloth`, including 234 LFS objects (930 MB).
Generated project/build/cache folders are already ignored; the only inventory
matches tracked under nested `Saved` paths are deliberate versioned QA evidence.
No history rewrite, force push, source-project hydration or branch merge occurred.

`git lfs prune --verify-remote` aborted because 1,088 historical candidate objects
could not be confirmed on the remote. Those objects were retained. Their removal
would risk local historical recovery; current branch push success does not prove
every historical/orphan LFS object exists remotely. Local LFS object integrity at
`4821abcf` PASS (`lfs-fsck.txt`: `Git LFS fsck OK`); the three added HUB/chest
objects were also uploaded by the subsequent successful push. Final cleanup
receipts are published on the same branch; post-push parity/status output is
retained in `Saved/CleanupEvidence/20261008/git-final.txt` (zero commits ahead or
behind, clean working tree). No merge into another branch is implied.

`Tools/Migration/Remove-OldValidationPackages.ps1` provides a dry-run by default;
`-Apply` removes only old cooked/staged validation outputs and requires a successful
retained package receipt. It preserves evidence and refuses active packaging tools,
reparse paths and paths outside the target. Its classifier uses the installed
`project-safety-guard` skill at `A:/Skills Codex`.
Do not remove `Intermediate/PipInstall`: enabled editor plugins recreate that
environment. Keep required binaries and incremental compilation caches for normal
development. Future validation runs should retain one successful package and prune
older generated copies after recording their receipts and screenshots.
