# Calysto Dungeon Director V6 definitive cutover

Status: **Editor build, 38 native tests and 30 Blueprint compiles PASS for the
inline Architecture revision. Manual gameplay/visual QA and cook/package
acceptance remain PENDING.**

Current authoring and exact evidence: [2026-09-05 inline Architecture update](Evidence/Calysto_Dungeon_Director_V6_Inline_Architecture_20260905.md).
The rollout/retirement sections below describe the original acceptance plan,
not proof that every release gate has passed.

V6 is the definitive Calysto authoring/runtime contract. It is one Primary Data
Asset, not a DataTable:

`/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset'/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'`

No legacy policy is a fallback. Legacy code/assets may coexist only while V6 is
being built and verified; they are removed after the V6 acceptance receipt is
complete.

## V6 model

- Resolve exactly one dungeon style for a floor. That style owns general
  dungeon floor, wall, and ceiling materials.
- Evaluate every generated room independently. A room first rolls theme
  presence (`0.25` initially), then chooses one eligible theme from normalized
  weights. Multiple different themes may appear on the same floor.
- Start and End rooms are resolved from required vendor marker streams.
  Additional Critical and Progression rooms are accepted through distinct,
  optional point-marker pins or the strictly filtered `EF_RoomFlags` metadata
  contract. Main-path membership is not reinterpreted as progression. The
  current vendor graph exposes no additional Critical/Progression source, so a
  live non-vacuous proof for those optional roles remains `PENDING` until such
  markers are authored; all present protected roles still fail closed.
- A room theme owns its own floor, wall, and ceiling material set. Theme
  materials override the floor style only inside that room. The Director is
  authoritative; vendor assets provide a validated transient schema but never
  provide a runtime material fallback.
- Material and decal choices are resolved before spawn, included in immutable
  intent/manifest hashes, deduplicated, asynchronously preloaded, then applied
  once. Per-frame material replacement is forbidden.
- Decals are budgeted separately from room-theme probability. Stable IDs,
  surface allowlists, spacing, exclusion radii, deterministic candidates, and
  hard caps prevent duplicate work and hitching.

The initial blood decal catalog uses only:

- `/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04`
- `/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04`

The project-owned outputs are:

- `/EFProcedural/Calysto/Internal/Materials/Decals/M_CalystoBloodDecal`
- `/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall`
- `/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor`
- `/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling`

No Realistic Blood Blueprint, Niagara system, or vendor material is copied or
mutated.

## Authoring tools

All tools run through `Launch-NoShellForWinterEditor58.ps1`. Creation tools are
create-once and become validation-only after their exact output exists.

1. `Create-CalystoDungeonDirectorV6InternalPCGClosure.py` creates the two
   project-owned cooked-safe internal PCG graphs required by the transient
   runtime composition. It duplicates only the reviewed vendor closure,
   rewires the exact allowlisted subgraphs, and never saves `/Game/Calysto`.
2. `Create-CalystoDungeonDirectorRuntimeAssetsV6.py` duplicates the three
   frozen runtime Blueprints to unversioned paths and reparents the chest copies
   to `/Script/EFProjectSystemsGameplay.ProjectCalystoChest`.
3. `Create-CalystoDungeonDirectorV6DecalMaterials.py` creates the project-owned
   master decal and three surface-specific instances. Missing material APIs
   block before creation.
4. `Create-CalystoDungeonDirectorPolicyV6.py` creates the single V6 Primary Data
   Asset from native V6 defaults. It does not load or compile an old policy.
5. Archive the first-run receipts and exact output hashes, then run every
   creator a second time. The second receipt must show zero Content changes
   and zero saves.

Creation order is internal PCG closure, runtime Blueprints, decal materials,
then V6 policy, because the policy's cook closure references the definitive
assets.

## Legacy retirement

The exact Content deletion set is seven assets:

- the V3 policy Data Asset;
- the V4 policy Data Asset;
- the V5 policy Primary Data Asset and rejected V5 DataTable;
- `BP_CalystoLockedChestV4`, `BP_CalystoLockPickChestV4`, and
  `BP_CalystoArmorPickupV4`.

Never delete these files with PowerShell or filesystem APIs. First run
`Run-CalystoDungeonDirectorLegacyRetirementV6.ps1` without `-Execute`; it is an
Unreal Asset Registry audit only. Apply mode requires:

- `-Execute`;
- the exact accepted V6 gameplay SHA-256;
- a fresh acceptance JSON below `Saved/Migration`;
- V6 valid and referenced by runtime/config;
- the three unversioned Blueprints valid and referenced by V6;
- every deletion target clean, byte-exact, and free of out-of-set referencers;
- all required gates and protected-asset hashes green.

The tool deletes the four policy assets first, rechecks Blueprint referencers,
then deletes the three old Blueprints. It removes only empty legacy policy
directories through Unreal APIs and rejects redirectors. A partial operation is
always `FAIL_PARTIAL`, never PASS.

The acceptance JSON consumed by retirement has this minimum shape:

```json
{
  "status": "PASS",
  "v6_gameplay_hash": "<64 hex>",
  "required_gates": {
    "editor_build": "PASS",
    "native": "PASS",
    "blueprints": "PASS",
    "pie_floors_1_10": "PASS",
    "statistics": "PASS",
    "performance": "PASS",
    "development_package": "PASS",
    "shipping_package": "PASS"
  },
  "protected_assets": {
    "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset": "<64 hex>"
  }
}
```

## Staged cutover

1. Capture Git status, exact legacy hashes, protected hashes, dirty packages,
   and a recoverable snapshot of untracked transition work.
2. Build direct V6 types/IR and all consumers side-by-side. Create the new
   unversioned assets and the single V6 policy. Do not delete anything.
3. Switch runtime/config/cook authority to V6 and prove no packaged legacy
   authority. The V6 authority and Primary Asset configuration are part of the
   implementation and must be validated from the live Asset Registry and cook;
   the temporary legacy `NeverCook` guards remain only until retirement.
4. Pass Editor/Game Development/Game Shipping cold builds, exact V6 native
   automation, all Blueprint compilation, deterministic PIE, real Door Floors
   1-10, probability statistics, material/decal visual QA, load-time and hitch
   budgets, cook, package, smoke, and determinism.
5. Produce the acceptance receipt, run retirement audit, then explicit apply.
6. Remove superseded source/tests/tools, archive historical documentation, and
   purge old generated outputs only after durable V6 evidence exists.
7. Cold-build and package again from the clean V6-only tree.

Historical Calysto Director evidence moves byte-preserved to
`Docs/Migration/Archive/CalystoLegacy`. It is not active authority and cannot
satisfy a V6 gate. Generated V3/V4/V5 Saved archives and project-owned
Intermediate/Binaries may be purged with a closed allowlist after the final V6
package is retained. Marketplace, ACFU, DazToUnreal, Player, Female, Male,
Multiple, Frederick, and `/Game/Calysto` remain protected.

## Final acceptance

- Asset Registry reports exactly one Calysto Director policy: the V6 object.
- The seven legacy assets are absent and the three unversioned Blueprints exist.
- No active Calysto source, Config, test, tool, or skill references superseded
  Director classes, paths, telemetry, bundles, or test namespaces.
- V6 policy/gameplay/authoring/material/decal hashes are valid and stable;
  replay is exact and reroll changes the intended domains only.
- The measured themed-room rate is statistically compatible with `25%`, and
  conditional theme frequencies match normalized eligible weights.
- Style and per-room-theme materials are visually correct on floor, wall, and
  ceiling; surface-specific decals obey budgets and never leak across surfaces.
- Fresh Development and Shipping IoStore manifests contain V6 and no legacy
  policies/classes. Protected hashes remain unchanged.
- Unexecuted requirements remain `PENDING`; they are never inferred PASS.
