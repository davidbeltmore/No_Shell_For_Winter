# Travel and release validation

Close the owned editor normally, preserving user work. Never run a build while
another wrapper has temporarily excluded Daz for UBT. Use fresh evidence names.

## Real inventory travel

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Equipment/Launch-InventoryPIE.ps1 -TravelQA -RunName Travel_NEW
```

This runs HUB -> authored floor 1 -> floor 2 -> HUB through existing production
travel APIs. It checks an equipped full satchel, exact contained GUID, fractional
base capacity, legitimate overload and rebuilt Bikini reservations. It does not
certify walking through the floor door, companion gameplay or generator authoring.
The HUB return must call the project provider's PreservePlayerForHubTravel
immediately before OpenLevel. This uses CaptureTravelForMap and an exact-destination
PostLoadMap restore; the HUB's current startup does not invoke LoadTraveledPlayer.
The snapshot is single-use, and a failed restore retains it with a visible error.
After rejected native cleanup, `FinishRuntimeSnapshot` relinquishes only the
runtime's local reference to an owned detached graph. The Director retains the
same graph and reconstructs it before finishing the request. Do not call the
provider's final release before that reconstruction, or convert its ordinary
release failures to success. The six exact tests in
`Tools/Equipment/Expected-TravelLifecycleTests.txt` cover terminal recovery,
failed recovery/retry and the portable equipment snapshot contracts.
Other explicit native callers retain their existing LoadTraveledPlayer adapter.
The floor's Player class may return into the HUB GameMode's authored derived
BP_TSChar class. Only in-memory travel admits that exact authored subclass;
disk saves, unrelated subclasses and controller-class mismatches remain rejected.
Wait for native gameplay initialization before adding fixture bags, then verify
the initial Travel Bag remains unique at the HUB and both floor destinations.
Capture failure must retain the source world;
do not replace it with a raw OpenLevel or recapture before lengthy cleanup.
The wrapper backs up and restores every existing top-level user save. Check both
`result.json` and `user_save_after.json` under the fresh run directory. Python
enum rendering includes type/value delimiters: normalize `.name`, as in the
existing production traversal fixture, instead of testing str(enum).endswith().

## Current cook preflight

`Validate-EquipmentCookClosure.py` validates slot/rule data, the sole clothing
Director, generated V5 manifest and configured V7 travel Director. It compiles
affected Blueprints in memory and proves their on-disk bytes unchanged. No asset
is saved. `Package-NoShellForWinter58.ps1` runs this after the existing strict V4
binding certification and adds the active travel Director to the exact cook roots.
The preserved V6 receipt certifies migration inputs, not active runtime authority.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Build-NoShellForWinterGame58.ps1 -Configuration Development
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Package-NoShellForWinter58.ps1 -Configuration Development -ArchiveRoot 'A:/NoShellForWinterEquipmentQA/Archive_NEW' -WorkRoot 'A:/NoShellForWinterEquipmentQA/Work_NEW'
```

The optional fresh WorkRoot relocates cook/stage output when the project disk
lacks space. It uses UE 5.8 UAT CookOutputDir and stagingdirectory; no existing
output is deleted or moved. Choose a volume with enough free space for all outputs.
The wrapper verifies Game source freshness and Daz receipts before/after packaging.
Preflight and archive results remain PENDING until the actual command succeeds.

## Packaged checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Equipment/Run-PackagedEquipmentQA.ps1 -ArchiveRoot 'A:/NoShellForWinterEquipmentQA/Archive_NEW' -Stamp Native_NEW -Mode NativeEquipment
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Equipment/Run-PackagedEquipmentQA.ps1 -ArchiveRoot 'A:/NoShellForWinterEquipmentQA/Archive_NEW' -Stamp Controller_NEW -Mode NativeController
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Equipment/Run-PackagedEquipmentQA.ps1 -ArchiveRoot 'A:/NoShellForWinterEquipmentQA/Archive_NEW' -Stamp Visual_NEW -Mode Visual
```

Each run creates a fresh `Saved/EquipmentQA/Packaged_<Stamp>/User` and passes it
as UserDir. No ordinary user save location is used. Exact Automation names/counts
are required, with no warnings, failures, unrun or in-process tests, and process
exit code must be zero. Run `Test-EquipmentAutomationReport.ps1` after report
checker changes; its 22 regressions reject extra, missing and renamed tests.
Preserve singleton selections as arrays around the entire conditional expression.
Controller mode verifies exact audited bytecode forms. Never refresh frozen
fingerprints to hide a mismatch: `AuditControllerCookEquivalence.py` and
`Docs/Migration/Evidence/EquipmentControllerCookEquivalence.json` record the
independent audit behind the two accepted cooked variants. Visual mode starts
the real HUB, equips the authored outfit,
checks full-bag save/load/overload/drop/pickup and opens the original inventory.
Its native harness only starts with `-EFEquipmentQA` in Development, requires a
fresh explicit UserDir and is compiled out of Shipping/Test.

Inspect `User/Saved/EquipmentQA/Packaged/inventory.png`. Only after actually viewing
the image, write `visual_done.txt` beside it, starting `PASS:` and describing what
was observed. The game then exits and the wrapper requires a PASS result. Cold
timing includes first item-class loading in a fresh process; OS caches are not
flushed. Do not compare it to the warmed 0.5-second target without that qualification.
Save the archive executable hash, exact logs, JSON and screenshot in migration docs.
Ready does not replace this rendered packaged check: the first compiler-36 package
exposed a render delegate removing itself before reading its captures. One-shot
callbacks must retain required state in local strong references and remove the
delegate only after their bookkeeping. Repeat real layering/cancellation QA and
produce a fresh archive after any renderer lifetime fix.
