# Equipment release: existing Game build prerequisites

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8, Development Game.
Source project: read-only `D:/Projects UE5/LustAsDeadlySin` (untouched).
Equipment checkpoint before this separate prerequisite: `87abead`.

The equipment feature is implemented in EFEquipment and integrates native ACFU.
These changes address existing project compilation failures; they do not change
the dungeon generator, protected vendor assets, Player or body meshes.

## Runtime contracts

- `EFCalystoContentCollisionContract.cpp` formerly obtained deferred visual mesh
  declarations from `FProperty` editor metadata. Game does not expose those APIs.
  `IEFCalystoDeferredMeshBoundsProvider` now supplies explicit property/component
  pairs in every target. `AProjectCalystoChest` declares its existing
  `ChestVisualMesh` and `ProjectCalystoChestVisual` attachment. The original
  resident-resource, mesh type, finite attachment and nonzero bounds checks remain.
  Invalid/duplicate declarations and more than 16 entries fail. Editor authoring
  additionally rejects obsolete metadata-only declarations.
- `ProjectCalystoDormantController.cpp` formerly required `ClassGeneratedBy` and
  the source UBlueprint GUID, which do not exist in Game. The two already-audited
  ACF controller classes now require canonical VM fingerprints at runtime; Editor
  also retains its GUID check. Function operands serialize via UE's public
  `UStruct::SerializeExpr`, using stable object/name/field paths rather than raw
  process addresses. Exact class ancestry, prohibited hooks, component audit,
  unpossessed staging and post-spawn side-effect checks remain active.
- Frozen installed profile: NPC
  `17FF44E72BB1D1510003BAA3A62673440322EED6`, parent
  `C173DC40FA2886C07128355F3E0664F35E0FBE4C`. These hashes are not inferred from
  asset names. `Native_EquipmentControllerAudit01/Native.log` records the loaded
  protected classes after their original Blueprint GUID checks. Its intentional
  initial rejection is an audit capture, not a passing regression gate.
- Never refresh those fingerprints merely to pass a changed controller. Review
  the changed native/Blueprint behavior and repeat dormant staging tests first.
  Cooked fingerprint parity and actual packaged execution remain required gates.

## Commands and evidence

Use the protected wrappers with the editor closed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Build-NoShellForWinterGame58.ps1 -Configuration Development
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Run-CalystoDirectorNative58.ps1 -ExpectedTestsFile Tools/Equipment/Expected-GameCompatibilityTests.txt -TestPrefix NoShellForWinter.CalystoDungeon.Director -TimeoutSeconds 180 -Stamp EquipmentGameCompatibility_NEW
```

The exact seven-test inventory covers native collision, deferred runtime bounds,
and the five existing dormant-controller tests. The audited profile test also
rejects modified bytecode with unchanged path/GUID, then admits the restored
original. Inspection must not mutate the original loaded script.

| Gate | Result | Evidence |
|---|---|---|
| Original Game build, full equipment checkpoint | FAIL: three editor-only API errors in two project files | `Saved/EquipmentQA/build_game_02.log` |
| Initial Editor compatibility build | PASS | `Saved/EquipmentQA/BuildEditor59/build.log` |
| Editor build with runtime contract regressions | PASS | `Saved/EquipmentQA/BuildEditor60/build.log` |
| Seven focused native regressions | PASS, seven exact tests | `Saved/Migration/CalystoDungeonDirectorV7/Native_EquipmentGameCompatibility03/Report/index.json` |
| Game rebuild | PASS, Development, Daz receipt restored/verified | `Saved/EquipmentQA/build_game_03.log` |
| Cooked profile parity and packaged runtime | PENDING | |
| Post-change protected comparison | PASS, 5,260 task-baseline files and Calysto asset baseline | `Saved/EquipmentQA/protected_game_compatibility_result.json`, `calysto_protected_game_compatibility.json` |

Earlier regression attempts remain recorded: `EquipmentGameCompatibility01`
failed because the new fixture appended a reference to its own TArray element;
the test now copies that value before append. `EquipmentGameCompatibility02`
passed all seven assertions but failed the strict runner due to its intentional
tamper warning. The test now explicitly expects exactly one such warning;
unexpected warnings still fail. BuildEditor61/62 PASS precede the accepted run.

These prerequisite gates do not certify equipment UI, body coverage, travel,
commerce, or the entire dungeon migration. See `EFEquipment.md` for feature gates.
