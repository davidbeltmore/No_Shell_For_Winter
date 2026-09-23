# Equipment master closure — 2026-09-23

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.2, installed ACF Ultimate **4.4.2**.
Source `D:/Projects UE5/LustAsDeadlySin` remains read-only. Starting commit:
`5084dcbd1212e9cb124d719d5bb94ae605cde9a8`.

## Final continuation

**Equipment closure COMPLETE.** The authorized terminal-recovery correction
`0121220` passed all six lifecycle/snapshot suites and a fresh 39-check real travel
run. Editor/Game, 36 Blueprints, fresh cook/package and all three packaged modes
passed on `Archive_FINAL3`. All 7,642 protected hashes match and Daz receipts are
restored. See [EquipmentTerminalRecoveryClosure.md](EquipmentTerminalRecoveryClosure.md)
for the final executable hash, exact evidence and final Git receipt.
The Archive_FINAL/FINAL2 results below remain historical phase evidence.

## Preservation and scope

The initial index, worktree, untracked inventory and byte copies are preserved at
`Saved/EquipmentQA/MasterClosure/Initial_20260923_042126`. No reset, checkout,
clean or stash was used. The initial HUB edits are preserved separately from
the code changes. Existing EFProcedural travel publication hooks were already
in the initial worktree; they are preserved, not an additional inventory owner.

The current 7,642 protected files match
`Saved/EquipmentQA/StorageRework/protected_before.json`. The old Phase0
comparison predates the installed ACF 4.4.2 and target Daz asset updates and has
pre-existing differences. It is retained as historical evidence, not substituted
for the current-session baseline.

## Cooked clothing identity

The isolated Editor/cooked diagnostic found **all three** Source, Body and Shared
fingerprints differed for the 12 bindings. Exactly 45 bone rows differed only in
FName display capitalization: `root/Root`, `pelvis/Pelvis`, `head/Head`,
`chin/Chin`, `lowerjaw/lowerJaw`. Paths, bone counts, parents, reference poses,
retarget modes, skeleton GUIDs and virtual-bone metadata matched. Editor keeps
case-preserving FNames; cooked runtime can use the global name pool's spelling.
Case is not part of FName identity.

`EFClothingSkeletonFingerprint.cpp` now canonicalizes FName-based names/paths
before hashing. Compiler and runtime use the same functions. Bone order,
hierarchy, reference transforms and shared skeleton metadata remain validated.
There is no compatibility bypass. V4 compiler **36**, schema **16** regenerated
the 12 bindings and dependent V5 registry through the protected upgrade tool.
Native regressions reject different names, parents and reference poses while
accepting case-only spelling differences. No protected skeletal mesh was saved.

Evidence in `Saved/EquipmentQA/MasterClosure`: `clothing-cause.json`,
`clothing-identity-difference.json`, `EditorIdentity/ClothingIdentityAudit.txt`,
`CookedIdentity01/User/Saved/ClothingIdentityAudit.txt`, `upgrade36-01.log`.
The diagnostic command `EF.Clothing.IdentityAudit` is explicit and bounded to
32 bindings/meshes; it writes its payload to Saved, not the runtime log.

## Test infrastructure and controller audit

The packaged runner preserves `$expected` as an array, anchors every test name,
checks the exact case-sensitive inventory and count, and rejects nonzero exit,
warnings, failures, not-run and in-process results. The shared report checker has
22 regression cases. The initial runner-only test against the old package is
historical evidence, not validation of the corrected release.

The equipment fixture needs Game-world actor lifecycle for native drop/pickup.
Using None caused real lifecycle assertion failures and was rejected. Its
isolated Game world suppresses Enhanced Input world-subsystem creation only
during construction, restoring the developer setting immediately with RAII;
it neither saves config nor changes ordinary gameplay. The controller's
synchronous possession-only fixture uses None. Both retain WorldContext cleanup.

The corrected runner exposed a second pre-existing cooked difference: Blueprint
compiler tracing, block order and temporary names change the controller bytecode
hash. Original frozen Editor hashes remain unchanged. The bounded offline
`Tools/Equipment/AuditControllerCookEquivalence.py` compares every callable root,
both branch successors, calls, assignments and operands, and rejects unsupported
opcodes and unreachable executable instructions. It verified the two exact
cooked forms against the frozen Editor programs. Runtime accepts only those
exact reviewed hashes, never a learned or normalized arbitrary program.
See [the audit record](Evidence/EquipmentControllerCookEquivalence.json).
Preflight tests mutate every scripted function in both classes, verify rejection,
restore the original bytes and verify acceptance.

## HUB return and fixture updates

The expanded real-travel check found that the floor uses `Player_C`, while the
HUB GameMode can spawn its derived `BP_TSChar_C`. Exact-class-only arrival
validation retained the inventory snapshot instead of restoring it. The
project-owned travel adapter now admits the destination GameMode's exact authored
subclass when restoring an in-memory travel snapshot. Disk saves still require
exact pawn class; controller class, full inventory GUID/count verification,
equipment effects and rollback remain unchanged. Native policy tests reject an
unauthored subclass, narrowing to a parent, null class and disk-save mismatch.
No Player asset, map or Marketplace file changes are involved.

The inventory fixture now exercises the approved native bag page and current
full-bag sale rejection. The storage fixture tests whole native potion stacks,
20 kg exact capacity, rejection above capacity, nesting and currency conservation.
LockPick is intentionally not stackable; `LockpickInput_Master01` recorded that
fixture error, and the potion-based `LockpickInput_Master02` passed 64 checks.
Both visual gates require review before a `PASS:` marker. The capture helper no
longer releases the gate automatically. New runs reject reused directories.

Startup-bag checks wait for native gameplay timers before adding fixture bags.
Wall-clock time spent loading assets is insufficient evidence of initialization.

## Render callback lifetime exposed by the first corrected package

`Packaged_Visual_FINAL` failed with exit 3 at
`EFClothingSurfaceDeformerProducer.cpp:883`. Once the stable fingerprints allowed
the garment deformer to run, its one-shot `OnEndFrameRT` callback removed itself
and then read captured shared pointers from the potentially destroyed closure.
The callback now takes strong local copies, completes cancellation/failure-count
checks and readiness bookkeeping, and removes the delegate last. It never reads
captures after removal. This changes no binding format or compatibility checks.
The first `Archive_FINAL` cook/package and its two native modes remain valid
historical results; its visual run is **FAIL**, not release acceptance. A fresh
archive is required after validating the lifetime fix.

## Validation ledger

All evidence paths below are relative to the target project. The phase-two corrected
Development archive and its three modes passed. Its remaining terminal-recovery
failure was subsequently fixed and verified by the final continuation above.

| Gate | Result | Evidence |
| --- | --- | --- |
| Editor build including render lifetime fix | PASS | `Saved/EquipmentQA/MasterClosure/build-render-lifetime01.log` |
| Game Development build including render lifetime fix | PASS | `Saved/EquipmentQA/MasterClosure/build-game-render-lifetime01.log` |
| V4 pre-package certification (12 bindings, 36/16) | PASS | `Saved/ClothingMorphV4QA/compiler_receipt_20260923_054933.json` |
| Blueprint/data preflight (36 Blueprints, no saves) | PASS | `Saved/EquipmentQA/cook_preflight.json` |
| Historical first cook, stage and Development archive | PASS, UAT exit 0 | `Saved/EquipmentQA/MasterClosure/package-final02.log`; immutable `A:/NoShellForWinterMasterClosure/Archive_FINAL.Package.log` |
| Historical first packaged native equipment (2 exact) | PASS, exit 0, zero warnings | `Saved/EquipmentQA/Packaged_Native_FINAL/launch.json`, `Report/index.json` |
| First packaged controller (1 exact) | PASS, exit 0, zero warnings | `Saved/EquipmentQA/Packaged_Controller_FINAL/launch.json`, `Report/index.json` |
| First packaged visual attempt | FAIL, render callback use-after-free; superseded by Visual_FINAL2 | `Saved/EquipmentQA/Packaged_Visual_FINAL/launch.json`, `game.log` |
| Corrected fresh cook/stage/Development package | PASS, UAT exit 0 | `Saved/EquipmentQA/MasterClosure/package-final3.log`; `A:/NoShellForWinterMasterClosure/Archive_FINAL2.Package.log` |
| Corrected packaged NativeEquipment | PASS, exactly 2 suites, zero warnings, exit 0 | `Saved/EquipmentQA/Packaged_Native_FINAL2/launch.json`, `Report/index.json` |
| Corrected packaged NativeController | PASS, exactly `NoShellForWinter.CalystoDungeon.Director.DormantController.AuditedNpcProfilePreflight`, zero warnings, exit 0 | `Saved/EquipmentQA/Packaged_Controller_FINAL2/launch.json`, `Report/index.json` |
| Corrected packaged Visual | PASS, actual screenshot inspected, exit 0 | `Saved/EquipmentQA/Packaged_Visual_FINAL2/launch.json`; `User/Saved/EquipmentQA/Packaged/result.json`, `inventory.png`, `visual_done.txt` |
| Strict Automation report regressions | PASS, 22 cases | `Saved/EquipmentQA/MasterClosure/report-checker-final2.log` |
| Restored Editor and Game Daz receipts | PASS, VerifyOnly | `Saved/EquipmentQA/MasterClosure/daz-editor-final.log`, `daz-game-final.log` |
| Native equipment (2 exact, including travel class policy) | PASS | `Saved/EquipmentQA/NativeAutomation_MasterTravelPolicy01/Report/index.json` |
| Controller compatibility (7 exact) | PASS | `Saved/Migration/CalystoDungeonDirectorV7/Native_MasterControllerFinal01/Report/index.json` |
| Clothing native (2 exact) | PASS | `Saved/EquipmentQA/MasterClosure/unisex-native36.log` |
| Clothing Female/Male/Female PIE (28 checks) | PASS | `Saved/ClothingUnisexQA/MasterClosure36/PIE/result.json` |
| Clothing visual review | PASS | `Saved/ClothingUnisexQA/MasterClosure36/PIE/visual_review.json` |
| Layering, cancellation, stale callbacks (19 checks) | PASS | `Saved/ClothingLatencyQA/MasterEquipment36/result.json` |
| Layering/cancellation after render lifetime fix (19 checks) | PASS | `Saved/ClothingLatencyQA/MasterRenderLifetime01/result.json` |
| Outfit/body transitions (24 trials) | PASS | `Saved/ClothingLatencyQA/MasterTransitions36/result.json` |
| HUB inventory, save/load, legacy migration, drop, trade, death (100 checks) | PASS | `Saved/EquipmentQA/MasterInventory01/result.json`, `inventory-review.png`, `visual_done.txt` |
| Deposit, bag capacity/stacks/nesting, lockpick input, chest save/load (64 checks) | PASS | `Saved/EquipmentQA/LockpickInput_Master02/result.json`, `bag-review.png`, `minigame-slate.png` |
| Actual HUB → floor 1 → floor 2 → HUB (39 checks) | PASS | `Saved/EquipmentQA/MasterTravel03/result.json`; original bag/child GUIDs, overload, reservations and one starter bag at every destination |
| Historical lifecycle/snapshot batch | FAIL, 5/6; superseded by EquipmentTerminalRecovery01 PASS 6/6 | `Saved/Migration/CalystoDungeonDirectorV7/Native_MasterTravelFinal01/Report/index.json` |
| Protected files after clothing | PASS, 7,642 unchanged | `Saved/EquipmentQA/MasterClosure/protected-clothing36.log` |
| Protected files after all PIE runs | PASS, 7,642 unchanged | `Saved/EquipmentQA/MasterClosure/protected-after-pie.log` |
| Final protected comparison against the requested storage baseline | PASS, 7,642 unchanged; HUB matches initial bytes | `Saved/EquipmentQA/MasterClosure/protected-final-comparison.json`, `protected-final.log` |

Historical failed fixture experiments and diagnostic builds remain under
MasterClosure with their original FAIL results. They are not release gates.
The first package attempt stopped before cooking because its wrapper still
expected compiler 35/schema 15. The strict 36/16 certification above supersedes
that failure; no validation checks were removed. The first executable source snapshot remains at
`Saved/EquipmentQA/MasterClosure/FinalSource_20260923`. The final corrected runtime
and packaging inputs are committed through `5a76c24`, including `a012780`
(render lifetime) and `49c7b20` (certification versions). No runtime changes were
made after the final builds/package; the commits preserve those tested bytes.

The failing lifecycle case is `DetachedSnapshotTerminalRecovery`. The runtime
returns false for the expected rejected detached snapshot; the Director then
returns before reconstruction. A narrow runtime-reference handoff is proposed
in `Saved/EquipmentQA/MasterClosure/proposed-terminal-recovery.md`. The user subsequently
authorized this narrow exception with ?Continua?. Commit `0121220` implements
the explicit runtime handoff; EquipmentTerminalRecovery01 passed all six tests.
The original failed receipt remains unchanged.

## Historical Archive_FINAL2 and reviewed image

Archive: `A:/NoShellForWinterMasterClosure/Archive_FINAL2`.
Executable: `Windows/NoShellForWinter/Binaries/Win64/NoShellForWinter.exe`.
SHA256: `C6AF9339478EF053053FCE709908F5884EC2FF626A43F5921E143EA2A4F96D51`.
All three launch receipts identify this same executable. Both the original
StorageClosure archive and the first MasterClosure archive were preserved.

The real packaged image shows the complete front-facing character, rendered
RagShirt/RagPants with authored tears and blue underwear coverage, inventory grid,
five visible outerwear slots and scrollbar, fixed underwear/satchel column,
Open bag button and readable weight/currency/footer. No exploded or missing
clothing geometry was observed. The screenshot does not certify hidden scrolled
slots or a rear view; the separate PIE evidence provides that broader scope.
The marker was written only after viewing the image. First-process outfit Ready
was 0.454 s, including item-class loading with existing OS caches; this is not a
claim of uncached disk or GPU presentation latency.

## Preserved commits and final repository evidence

- `c37eb19`: stable cooked clothing identity and regenerated 36/16 bindings.
- `d4b213d`: separate preserved HUB map changes, initial bytes unchanged.
- `971faa2`: audited exact cooked controller variants and isolated fixture.
- `49c7b20`: compiler/schema certification tools aligned with 36/16.
- `57a7042`: native storage retirement and procedural chest consumers.
- `a012780`: render confirmation state retained across delegate removal.
- `5a76c24`: equipment travel/persistence, real fixtures and strict packaged QA.
- The documentation/skill commit following these records this ledger and recipes.

`Saved/EquipmentQA/MasterClosure/final_git.json` records the first closure phase's post-commit HEAD,
empty `git status --short`, and `git diff --check` result. Generated packages,
Saved evidence, logs and backups are not tracked. At that first closure phase,
the initial EFProcedural runtime edits remained byte-identical to their starting
copies, as recorded in `preserved-procedural-runtime.json`. The subsequent
authorized correction is separately committed in `0121220` and documented in
the final continuation; no unapproved recovery fix was applied.
