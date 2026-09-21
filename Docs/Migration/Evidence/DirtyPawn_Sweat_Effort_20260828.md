# Dirty Pawn sweat effort tuning — 2026-08-28

## Scope

The target project-owned plugin `Plugins/DirtyPawnRuntime` owns the canonical
runtime sweat component. No source-project, Marketplace, Engine, ACF Ultimate,
or DazToUnreal files are modified.

## Change

`UDirtyPawnComponent::SweatMovementGainPerSecond` changed from `10.0f` to
`1.0f`.

`UpdateSweatState()` applies that rate only while the character is running,
jumping, or performing the Alt-roll activity. With `SweatMaxPoints = 100`, a
fresh runtime component now requires approximately 100 seconds of those movement
activities to reach 100% sweat instead of approximately 10 seconds. The 100%
threshold, decay, wash behavior, visual curve, and explicit debug fill remain
unchanged. `SweatIntimacyGainPerSecond` is currently not consumed by the sweat
update path and is intentionally unchanged.

## Evidence and validation

| Gate | Status | Evidence |
| --- | --- | --- |
| Live UE 5.8 preflight | PASS | `http://127.0.0.1:8000/mcp`; `Player.Player` CDO contains no authored Dirty Pawn component, while the project-owned `DirtyPawnWorldSubsystem` creates the canonical runtime component. Live `DirtyPawnComponent` CDO values before the edit: max `100`, movement gain `10`, decay delay `45`, decay rate `2`. |
| Static rate/trigger inspection | PASS | `DirtyPawnComponent.cpp::UpdateSweatState()` selects `SweatMovementGainPerSecond` for running, jumping, and Alt-roll, then `ApplySweatActivity()` clamps to `SweatMaxPoints`. |
| UE 5.8 editor build | PENDING | The official build wrapper stopped before compilation because Live Coding is active. Its Daz receipt guard completed `PASS`. A Live Coding attempt likewise stopped with UBT `RulesError`, because that mode cannot use the mandatory temporary Daz plugin exclusion. A cold build requires closing the Editor. |
| Blueprint compile | PENDING | `/Game/FullSample/Player.Player` is currently dirty in the Editor; it was not saved, compiled, or closed by this change. Compile/load after the native cold build. |
| PIE functional + visual QA | PENDING | Verify a fresh player reaches 100% sweat after about 100 s of qualifying movement, then inspect the sweat visual/status. |
| Cook and packaged validation | PENDING | Scope requires a new cook, package, and packaged smoke after runtime QA. |
| Protected invariants | PENDING_BASELINE_RECONCILIATION | `Saved/Migration/Evidence/DirtyPawnSweatOnePoint_ProtectedInvariantVerification_20260828.json` was generated after the 1 point/s edit. It reports the same 90 existing baseline mismatches as the preceding scoped rehash: ACFU `1`, DazToUnreal `0`, target Daz assets `85`, and Player/Female/Multiple/Male `4`. The changed project-owned `DirtyPawnRuntime` header is outside every protected set. No no-new-delta comparison was available for this scope, so this gate is not promoted to `PASS`. |

## Snapshot

No commit was created by this change. The pre-existing unrelated worktree modification is
`Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset` and is not part of this scope.
