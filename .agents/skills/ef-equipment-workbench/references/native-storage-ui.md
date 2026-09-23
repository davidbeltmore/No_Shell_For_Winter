# Native storage UI maintenance

The bag page is `/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentBag`, a
project-owned copy of the installed native deposit widget. Its logic parent is
`UEFEquipmentStoragePage`. Keep the approved main inventory layout unchanged.

## Layout contract

Keep `CanvasPanel_0` as the full-screen root and preserve its native background,
top tabs and bottom navigation. Insert bag metadata into `SizeBox_67`, above
`ANSWidgetSwitcher_47`. Never wrap the entire native canvas in a vertical box
with metadata above it: that shrinks the native page and exposes the game HUD.

`BagSummaryBackground` covers the metadata with black. `BagSummary` contains the
compact bag selector and weight text. Player and bag weights use separate lines.
Do not show technical GUIDs. Keep errors in native notifications, rather than
permanently displaying a previous merchant rejection on the storage page.

The editable authoring recipe is `BuildStorageRework` in
`EFEquipmentStorageRework.cpp`; it is idempotent and fails if native structural
widgets are missing. It updates only the exact project widget packages.

## Apply and inspect

1. Close the target editor safely, then run the protected
   `Tools/Migration/Build-NoShellForWinterEditor58.ps1` wrapper.
2. Run `Tools/Migration/Launch-NoShellForWinterEditor58.ps1 -PythonScript
   "D:\Projects UE5\NoShellForWinter\Tools\Equipment\Author-StorageRework.py"`.
   Check `Saved/EquipmentQA/StorageRework/author_ui.json` and the compile log.
3. Launch `Tools/Equipment/Launch-InventoryPIE.ps1 -StorageQA -RunName <unique>`.
   The fixture pauses at `visual`; inspect the actual UMG and then write its
   `visual_done.txt` marker. A viewport-only screenshot does not capture UMG.
4. For Slate captures, run `python Tools/Equipment/Capture-StorageUI.py
   Saved/EquipmentQA/<run>/bag-slate.png`. This discovers the native editor MCP
   toolset, calls `CaptureEditorImage` and decodes the PNG. It avoids unrelated
   Windows capture overlays. Do not use HighResShot for UMG evidence.
5. Check narrow and wide views, tabs, keyboard/back, selector, both transfer
   directions, quantities and currency. Keep unexecuted checks `PENDING`.

Run PowerShell wrappers with `-NoProfile -ExecutionPolicy Bypass`. Do not bypass
the Daz receipt guard. Preserve user saves through the existing QA launcher.

## Context contract

Pass the owning widget as `WidgetContext` to storage adapters. Deposit and bag
transfers never invoke merchant pricing. Bag inventory is the actual GUID-bound
ACF component; widgets do not manufacture a second inventory. Merchant sales
reject nonempty bags. Changing presentation must preserve these routing rules.

Storage pages collapse the nested buy/sell `Title` widgets at runtime. Keep
their top tabs as the only section headings; merchant panels retain native
titles. `StorageBackdrop` is a full-screen, non-interactive black canvas child.

## Starting bag

`UEFEquipmentSettings::StarterBagClass` defaults to the editable
`/Game/_Game/Items/Equipment/BP_StarterBag` (`Travel Bag`). Its initial authoring
copies tier one's existing 10 kg/10% profile, icon and provisional visual.
Change the settings reference to select a different initial bag. Subsequent
authoring preserves an existing starter asset's edits.

The game-instance equipment subsystem composes the coordinator when a local
player possesses a pawn. The coordinator waits for native inventory readiness
and grants the bag unequipped only during fresh-game initialization, if no bag
already exists. A game-instance guard prevents repeats across possession;
native save/load and travel restoration remain authoritative. Never add the
starter through widget construction, polling refresh, or protected Player CDO.

## Lockpick input regression

ACF 4.4 sets its interactable component busy before dispatching the actor event.
When the lock gate consumes an interaction while still locked, release that
native busy state with `EndInteraction`; no storage page exists to release it.
Keep the owner of the prompt/session as the target for Begin and Confirm, since
proximity detection can change while UI owns focus. Continue through the ACF
interaction component so its requirements still apply.

ACF starts `InteractionActionTag` before the actor gate. Its montage notify does
not preserve the lockpick Begin/Confirm payload. The lock component therefore
temporarily clears that instance's action tag while locked, retaining the
authored value for restoration on unlock. Use the validated reflected
`FGameplayTag` property adapter (ACF has no public setter); never change the
plugin or its defaults. Successful chest continuation is deferred until the
Confirm callback finishes, then enters native `Interact` once. Repeated Begin
must preserve the same session; stale focus callbacks must not refocus removed
prompts. Spacebar confirms only on key-down without repeat; Escape/BackSpace
cancels and clears the session.

User revision (2026-09-23): confirmed attempts consume one required lockpick,
on success or failure. Opening/cancelling is free. Consume through the native
root inventory on authority, verify the count delta before unlocking, and guard
against reentrant/duplicate confirmations. This supersedes the earlier plan's
non-consumable rule. Missing tools at confirmation must never unlock the chest.
The minigame owns its keys ahead of CommonUI's global accept/back handler only
while open; release that processor together with movement/input capture.

`Validate-StorageRework.py` run names beginning with `LockpickInput` exercise
the actual Slate button down/up path, then existing-focus BackSpace, E, Enter
and Spacebar. This is distinct from physical OS input validation. Never label
a direct interaction call as proof of a working button. Use the MCP Slate
capture for the visible minigame. The initial fixture also checks native
deposit/bag transfers and currency conservation.

To run unattended through the visual checkpoints, create `visual_done.txt` and
`minigame_visual_done.txt` inside `Saved/EquipmentQA/<run>` before launching
`Launch-InventoryPIE.ps1 -StorageQA -RunName <run>`. Omit the second marker for
a bounded screenshot checkpoint. Do not manually restart PIE during a fixture;
that invalidates its world and widget references. Keep failed/interrupted runs
as evidence, and record the final run separately.

`Capture-LockpickFixture.py Saved/EquipmentQA/<run>` can capture that checkpoint
through native MCP and release it automatically; inspect the resulting PNG.
After ALS world loading, reacquire world/pawn/chest references by saved actor
identity. Pair synthetic pointer down/up within one tick: desktop cursor updates
can otherwise remove the synthetic hover. Distinguish keyboard-assisted fixture
continuations from independent physical mouse validation in the report.

When configuring a live fixture, use `PropertyAccessChangeNotifyMode.NEVER`
for its stock settings. Default editor notifications reconstruct SCS lock
components and invalidate both references and transient cached action tags.
This is fixture setup, never a substitute for testing authored game assets.
