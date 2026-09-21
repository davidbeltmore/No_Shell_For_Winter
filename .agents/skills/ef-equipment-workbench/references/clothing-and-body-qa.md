# Layering and real body QA

EFClothingMorph owns visual fit. Its sole editable Director is
`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`. `Regions` declares
semantic identifiers; each clothing row supplies its layer and occupied regions.
Generated body profiles copy these declarations and translate skin slots through
the registered body's aliases. Do not author generated profiles independently.

The six initial garments occupy Torso, Legs or Feet. Bikini occupies both Torso
and Legs visually, while EFEquipment separately reserves both inner equipment
slots. Initial underwear reserves 0.03 cm thickness plus 0.03 cm outer gap. These
are editable visual values, not inventory conflict rules. Partial rag garments
must not hide complete body skin sections. Preserve existing genital material/
bone coverage and body-deformer restoration.

`Tools/Equipment/Audit-ClothingLayers.py` is read-only. The explicit bootstrap
`Author-ClothingLayers.py` adds missing known regions/occupancies and publishes
the derived V5 catalog; it preserves existing occupancy tuning. Review it before
using it over intentionally different authored rows. Run either through the
protected launch wrapper's `-PythonScript` argument.

Run one editor at a time with a fresh run name:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-EFClothingMorphV5NativeAutomation58.ps1 -TestPath EF.ClothingMorph.V51.Contracts -TimeoutSeconds 120 -Stamp EquipmentLayers_NEW
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-ClothingLatency58.ps1 -Mode baseline -EquipmentQA -RunName EquipmentLayers_NEW
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-ClothingLatency58.ps1 -Mode baseline -Repeats 3 -RunName EquipmentTiming_NEW
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-ClothingLatency58.ps1 -Mode baseline -Repeats 1 -FpsCap 30 -RunName EquipmentTiming30_NEW
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-ClothingLatency58.ps1 -Mode baseline -Transitions -RunName EquipmentTransitions_NEW
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/ClothingMorphV5/Run-UnisexPIE58.ps1 -ExtendedOnly -RunName EquipmentBodies_NEW
```

The scripts now equip/unequip through the real player coordinator. FootWrap uses
the Feet leaf, and Bra/Shirt or Panty/Pants coexist. Bikini displaces inner
garments without displacing compatible outerwear. The transition suite includes
three-, four- and five-garment outfits plus Female/Male/Female swaps.

The unisex overlap fixture now uses actual Pants + Panty coverage owners. It no
longer swaps a mesh artificially, suspends the bridge, or calls ForceReconcile.
Extreme morph tests use actual body morph names, four layered garments and real
movement. This supersedes older shared-skill recipes referring to a parent Armor
FootWrap slot or a synthetic chest-mesh overlap.

All three PIE runners use `Tools/Migration/QAUserSaveGuard.ps1`: backup preexisting
top-level saves before launching, preserve changed QA output and restore verified
original bytes only after editor exit. An interrupted run retains UserSaveBackup;
do not overwrite it. Native contract tests do not run PIE gameplay subsystems.

Inspect actual screenshots, not only Ready flags. Ready means validated render
submission, not presentation time. Compare warmed timings against 0.5 seconds;
report cold packaged loads separately. Use the production bridge in baseline
mode; notify/watchdog simulations are diagnostics, not release evidence.
