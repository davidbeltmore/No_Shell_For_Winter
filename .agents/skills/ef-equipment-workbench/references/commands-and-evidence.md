# Equipment commands and evidence

Run from `D:/Projects UE5/NoShellForWinter`. Inspect `git status --short` first.
Preserve existing unsaved user work before a cold build or commandlet. The
workbench refuses to act while an editor is open; it does not kill processes.
This reference does not grant permission to overwrite later manual asset edits.

## Task-specific baseline

```powershell
python Tools/Equipment/Check-ProtectedEquipment.py --capture --baseline Saved/EquipmentQA/MyTask/protected_before.json
python Tools/Equipment/Check-ProtectedEquipment.py --baseline Saved/EquipmentQA/MyTask/protected_before.json
```

Capture refuses to overwrite an existing baseline. Compare the same baseline
after each phase. Include Git HEAD and the working-tree snapshot in the phase
record. Never use another task's historical hashes as a new task's starting state.

## Cold build, inspection and authoring

Use a fresh `Stamp` for every run; it prevents stale log/receipt reuse.

```powershell
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action BuildEditor -Stamp EquipmentBuild01
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action InspectUI -Stamp EquipmentInspect01
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action AuthorUI -Stamp EquipmentAuthor01
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action AuthorWorld -Stamp EquipmentWorld01
```

These call the protected migration wrappers, which restore the Daz receipt.
InspectUI is read-only. AuthorUI mutates the exact project UI packages, fills
missing registry icons and creates missing Designer widgets. Review this bootstrap
before applying it to a project with later manual changes. It never saves Player,
body meshes or Marketplace packages. A successful process exit alone is not a
successful Python run: check the fresh JSON receipt and `LogPython: Error` lines.

New icon source images can be reproduced with
`python Tools/Equipment/Generate-SlotIcons.py`; normal authoring imports only
missing icon assets. Existing imported textures remain designer-owned.
`Set-PrototypeThumbnails.py` is a separate explicit bootstrap migration affecting
the six catalog garments and three satchels. Ordinary AuthorUI does not run it or
overwrite later item artwork.

`AuthorWorld` creates missing project-owned prototype visuals and the native pickup
copy, preserving existing prototype assignments. It does not change the ACF plugin.
`UEFEquipmentSettings::WorldPickupClass` points to
`/Game/_Game/Items/Equipment/BP_EquipmentWorldItem`. The game-world subsystem resolves
this soft class and assigns the runtime `UACFInventorySettings::WorldItemClass`.
Keep the original native hard reference in `Config/DefaultPlugins.ini`: pointing it
at our subclass creates a circular script-registration dependency while ACF loads
its settings CDO. Moving EFEquipment to PreDefault did not resolve that cycle.
The pickup copy gates the original sound/unregister/end-interaction continuation on
`WasPickupCompleted`, after calling the native parent. Otherwise a rejected pickup
would disappear from the interaction list. Test rejection and acceptance separately.
Use the deferred `SpawnWorldItem` plus verified inventory transfer for drops;
`SpawnWorldItemWithInstance` can reconstruct stackable items and lose their GUID.
Prototype visuals are functional temporary geometry, not final art.

`Retire-RejectedUI.py` is a one-time migration, not routine authoring. It accepts
only its exact untracked rejected widgets, checks editor referencers, creates an
exclusive backup/receipt, and deletes through EditorAssetLibrary. Never generalize
it to recursive filesystem asset deletion or rerun it against later authored UI.

## Native ACF tests and visible inventory

```powershell
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action NativeTests -Stamp EquipmentNative01
Tools/Equipment/Invoke-EquipmentWorkbench.ps1 -Action PIE -Stamp EquipmentPIE01
```

Native reports live under
`Saved/EquipmentQA/NativeAutomation_<stamp>/Report`. The equipment runner requires
the policy and vertical tests and accepts only a fully successful suite. The older
clothing runner expects exactly one test and must not receive the equipment prefix.
This is native component testing,
not a substitute for visible PIE or a packaged run.

The PIE script creates transient fixture items on the real HUB player, opens the
original ACF menu through `ANSUIPlayerSubsystem`, and writes
`Saved/EquipmentQA/<stamp>/result.json`. It never saves packages or save slots.
It tests exact slot counts, wheel direction and actual Slate pointer drag/drop.
`AWAITING_VISUAL_REVIEW` means inspect the menu before completing the fixture.
A pending result after an editor crash is a failed run; inspect its log.

For every new editor connection, use native MCP `list_toolsets`, then describe
`EditorToolset.EditorAppToolset`. `CaptureEditorImage` captures UMG and the full
editor; viewport-only screenshots can omit menus. Its text result contains JSON
`returnValue.mimeType` and base64 `returnValue.data`. Save the original image,
inspect it, and record the judgment independently of widget-count assertions.
After actual review, write either `PASS: <observed result>` or
`FAIL: <specific visual defect>` into that run's `visual_done.txt`; the script
ends PIE and closes its editor. It times out after six minutes if unreviewed.

## Known integration details

- `ANSUIPlayerSubsystem` is a GameInstance subsystem. In Unreal Python, find it
  through `ObjectIterator` with `get_outer() == GameplayStatics.get_game_instance(world)`.
- Unreal Python exposes `WidgetLibrary`, not `WidgetBlueprintLibrary`, and does
  not expose WidgetBlueprint.WidgetTree. Use `InspectInventoryUI` or the narrow
  editor helper `FindInventoryWidget` for Designer trees.
- Geometry helpers are `unreal.SlateLibrary`; compare copied GUID values using
  `guid.to_string()` rather than Python wrapper identity.
- Use `get_editor_property('height_override')` for a SizeBox. There is no Python
  `get_height_override()` method in this engine build.
- Preserve the BP `ACF_ItemButtonInterface_WB` between the item icon and its native
  parent. Reparenting the icon directly drops Item, Index and selection contracts.
- Its `Item` property is native `FBaseItem`, which already contains a stable GUID:
  use `GetItemGuid()` and resolve against the owning player's ACF inventory.
  `ACF_ItemList_WB.GetCurrentItem` returns a selected button widget, not an
  FInventoryItem; it is not a suitable substitute for reading the button's GUID.
- Register new widgets through `UWidgetBlueprint::OnVariableAdded` and remove
  obsolete entries through `OnVariableRemoved`, then notify structural changes
  before compilation. Preserve existing GUIDs, including animation entries.
  Structural notification alone does not maintain UE 5.8's widget GUID map.
- A `GeneratePathToWidgetUnchecked` path is layout-only. Before routing synthetic
  Slate pointer events, reconstruct `FWidgetPath` from `FWidgetAndPointer` entries
  so every widget has a virtual-pointer-position entry. This is editor QA code,
  not a runtime input dependency and not OS input automation.
  Use the arranged path's leaf geometry for pointer coordinates; cached tick
  geometry can be zero for widgets that do not tick.
  Route hover before a button press/release: SButton needs the hover state to
  recognize a click. A handled press alone does not prove that its action ran.
- Inventory equipment host `SizeBox` previously had width override zero. The
  user's approved adjustment uses a 660-unit equipment host, a 420x600 native
  `ANS_CharacterRenderer_WB` host and 240-unit item-description/comparison panels.
  The outer HorizontalBox fill ratios also control actual allocation: inventory
  .50, equipment .36, item info .14. SizeBox widths alone do not change that split.
- Never use UECP execution as a fallback around a license rejection. Native MCP,
  protected Python commandlets and project-owned editor helpers are sufficient.

Editor/Blueprint tests do not imply Game build, cook or packaged acceptance.
Current evidence and remaining gates belong in `Docs/Migration/EFEquipment.md`.

Run `Audit-Integration.py` through the protected launch wrapper's `-PythonScript`
option before extending save/load, trade or storage adapters. It uses `rg` only to
find candidate packages, then reads their real Blueprint call graphs through the
project editor helper. Evidence goes to `Saved/EquipmentQA/IntegrationAudit`.
The audit does not compile or save packages. Include native C++ consumers in the
review as well; a Blueprint-only result is not an exhaustive runtime call graph.

Save integration authoring:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Launch-NoShellForWinterEditor58.ps1 -PythonScript 'D:/Projects UE5/NoShellForWinter/Tools/Equipment/Author-SaveIntegration.py'
```
The receipt is `Saved/EquipmentQA/author_save_integration.json`. SaveLocalPlayer,
LoadLocalPlayer, SaveGameWorld and LoadGameWorld must use the verified adapter.
ALS actor-loaded fires before component-loaded; do not restore effects there
while the adapter is busy. Restore them after all native component callbacks.
World pickups must expose their native storage through GetComponentsToSave.
The actual level-reload fixture preserves a dropped full bag through native ALS.
