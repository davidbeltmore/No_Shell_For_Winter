---
name: ef-npc-outfit-workbench
description: Author and validate NoShellForWinter DAZ NPC outfit presets, shared EFEquipment admission, appearance-linked inventory restoration and bounded clothing crowd measurements. Use for NPC clothing selection and persistence; body onboarding and clothing binding compilation remain in their existing workbenches.
---

# NPC outfit workbench

Start with `noshellforwinter-unreal-mcp` and read-only target context. Read
`Docs/Migration/NPCOutfits.md` for current measured gates; this skill does not
certify the outstanding release gates. Source game, vendor plugins, Player and
protected body assets remain read-only.

## Authoring and ownership

- `Tools/NPC/Outfits/Definitions.json` maps stable outfit IDs to owned DataAssets
  under `/Game/_Game/Characters/Data/Outfits`. Each item references an existing
  armor class and exact leaf destination. `FullRag` uses RagShirt/Chest,
  RagPants/Legs and RagFootWrap/Feet. No gender restriction or parent Armor slot.
- `Tools/NPC/Roster.json` is the assignment entry point. An archetype's `outfits`
  array contains `{ "id": "FullRag", "weight": 1 }` rows. Empty opts out; one
  choice is fixed; multiple nonnegative weights select deterministically using
  the existing appearance identity seed. At least one weight must be positive.
- `ProjectNPCOutfitComponent.OverrideChoices` overrides selection per instance;
  `bEnabled=false` opts that instance out. Existing saved equipment still wins
  over changed selection data. Adding an outfit does not require a character BP.
- ACF owns item instances. `UEFEquipmentCoordinatorComponent::CreateForNPC`
  explicitly enables shared slot/reservation operations, without player polling,
  carry projection, UI notifications or death routing. Do not make automatic
  `FindOrCreate` admit every Pawn. Native slot arrays can contain repeated Ammo:
  deduplicate before checking ancestry, rather than removing weapon slots.
- EFClothingMorph Director remains the only fitting/layer/coverage authority.
  The NPC integration attaches its existing runtime/bridge only to eligible
  owners; global non-player enable stays false. Use `ef-equipment-workbench`
  for new item rules and `ef-clothing-unisex-workbench` for garment registration.
  Use `ef-body-preset-workbench` for new bodies; an outfit cannot certify a fit.

## Ordering, persistence and cancellation

The existing appearance subsystem queues outfits after body/morph application.
Native equipment readiness and ALS idle precede mutation. The world queue admits
one owner/frame and four initializing outfits. This is an initial admission bound,
not proof of a global binding-load budget or supported crowd size.

`FProjectNPCAppearanceRecord` retains outfit ID/version and the shared native
inventory serialization envelope under the existing identity. A versioned record
is restored, never regenerated—even an emptied inventory. `RestoreSerial`
distinguishes an actual load from repeating appearance initialization. Do not
replace GUIDs with classes/indices or serialize reservations as inventory items.

Initialization and presentation have a 20-second deadline. Initial grant failures
attempt native inventory rollback and report `Failed`; invalid saves do not grant
replacement clothing. Load handles cancel on component teardown and use weak
callbacks. The character itself is not hidden by this integration. `Ready` is
render-submission evidence and must be supplemented by image review.

Inventory mutations must be captured before same-frame destruction or saving.
Deferring the only inventory snapshot to a later tick resurrected a removed shirt
on identity-based recreation; use the lifecycle fixture to verify this boundary.

## Commands and evidence

All paths below are relative to the target project. `Run.py` uses the maintained
localhost editor Python bridge and accepts remaining arguments for its action.

```powershell
python Tools/NPC/Outfits/Protection.py before
python Tools/NPC/Outfits/Run.py Inspect
python Tools/NPC/Outfits/Run.py Author
python Tools/NPC/Outfits/Run.py Audit --compile
python Tools/NPC/Outfits/Run.py RunPIE --count 4 --seed 731
python Tools/NPC/Outfits/Run.py RunNativePIE --body Male.02 --count 1 --capture 2 --functional 1 --name unique_pilot
python Tools/NPC/Outfits/Run.py SaveQA
python Tools/NPC/Outfits/Run.py LifecycleQA
python Tools/NPC/Outfits/Run.py RunNativePIE --selection --count 16 --seed 731 --name unique_selection
python Tools/NPC/Outfits/Run.py ClothingRegression --bodies Female.01,Male.01,Male.02
python Tools/NPC/Outfits/Run.py LogicQA --mode ai
python Tools/NPC/Outfits/Sweep.py --counts 1 10 25 50 --prefix unique_measurement
python Tools/NPC/Outfits/Protection.py after
```

The protection baseline is immutable; reuse `after`, never overwrite `before`.
Before PIE, use `SaveGuard.ps1 -Mode Backup` in a fresh session directory; after
closing that editor, use `-Mode Restore` and verify every prior save hash. This
covers independent subsystems that write during EndPIE. Keep backups on failures.

Author saves only the definitions and roster, with scoped backups. Audit compiles
owned variants without saving them. `--require-all` is the rollout gate. The
current FullRag rollout passes all 40 variants; keep explicit configured,
unconfigured and failed-variant counts when adding or excluding variants.
Runtime receipts live in `Saved/NPCOutfits`, compact published evidence in
`Docs/Migration/Evidence/NPCOutfits`. A successful invocation is not runtime PASS:
require the fresh `runtime.json` and `host.json` where provided.

Native command `Project.NPCOutfits.QA` accepts count, body, archetype, dressed,
seed, capture, timeout, name, functional, loading and optional `quit`. It freezes
fixture navigation for controlled fit/performance work. It does not certify AI;
use the existing NPC logic fixture for actual interaction and pursuit.
`gc` forces collection after fixture destruction; the PIE host exposes `--gc`.
For a cooked build use `Tools/NPC/Outfits/ValidatePackaged.ps1 -StagedRoot ...`
with optional `-Body`, `-Archetype`, `-Count` and `-Seed`. It requires native
functional assertions, GC completion, zero pending work, image capture, feature
logs and a normal process exit. Review the actual image separately. A fresh
process can preload clothes through HUB; do not call it an isolated cold-resource
fixture. Use the existing body-preset HUB package wrapper after protected builds
and clothing freshness checks; its cook includes the owned Characters/Outfits
and V4/V5 generated directories through DefaultGame.ini.

Measure matched dressed/undressed groups 1, 10, 25; 50 is diagnostic. Capture
30–45 seconds per group, separate cold from warmed loads, retain raw CPU/GPU/frame
samples and teardown counts. `Measure.py` compares paired receipts. The user-frozen
25-NPC incremental budget is CPU 2 ms, GPU 3 ms, frame p95 4 ms and warmed readiness
2 s, with 60 FPS only the provisional whole-frame reference. Never loosen it after
measurement. Missing telemetry is PENDING, not zero cost. See the migration doc
for actual capacity and current limitations.

`--prewarm` prepares the same group in the same PIE world before measurement.
Preparation is not a cold packaged-load benchmark. `Measure.py` requires actual
quality CVars, matching provider configuration and successful preparation receipts
for warmed acceptance. Never overlap measurements or run heavy builds/hashing
while a capture is active. Traces include buffered history: filter the measured
interval before interpreting Insights totals.

Initial optimizations reuse immutable GPU binding buffers and stop witness cycles
only at an exact fixed point. They preserve per-frame animated surface evaluation.
The prepared 1-NPC comparison passes; 10/25 still fail the frozen budget, and 50
remains diagnostic. See `Docs/Migration/Evidence/NPCOutfits/performance-initial.json`.
Fixture version 2 requests the final body before clothing admission to avoid
timing an unintended double fit; comparisons require matching fixture versions.
The version-2 25-NPC pair still fails. The shared clothing regression validates
coverage/movement mechanically but requires separate screenshot review; the
first Female.01 extreme-morph capture after body transitions failed visually.
Later matched stock/cache and fresh-editor runs did not reproduce it; retain the
failed image without claiming its cause is established. The shared regression
temporarily disables background editor throttling and restores it on completion.
Use `--immutable-buffers 0` for a matched stock-provider diagnostic, keeping the
same body sequence. A single-body pass cannot certify the body-transition case.

`--selection` uses transient outfit definitions for repeated weighted generations,
zero-weight exclusion, fixed instance choices and saved precedence. It does not
change authored definitions or the roster. The sixteen-identity fixture passed;
the exact distribution is evidence of deterministic repetition, not a statistical
claim about a small sample.

`--profile-install` logs installation stages for a short diagnostic. Never use it
for acceptance FPS captures; `Measure.py` rejects that mode. `--prepared-binding 0`
retains repeated immutable uploads for A/B. Prepared reuse keeps only weak lookup
entries (maximum 128); live providers own the immutable buffers, while transforms,
morphs and runtime sliders remain per-instance. Three fresh packaged body cases
have passed explicit GC and normal shutdown; repeat after ownership changes.
`--profile-gpu` records one detailed frame after readiness, with profiler UI off
and temporary settings restored. It is diagnostic and is rejected by `Measure.py`.

Do not assume garment mesh reduction reduces fitting work. The withdrawn 25%
Rag LOD experiment retained roughly the same witness count while concentrating up
to 885 references on a vertex, above the unchanged 256 bound. Catalog freshness
did not detect that runtime incompatibility. Preserve its failed receipt; original
garments/bindings were restored. Any future LOD work must certify runtime incidence
and examine morph/animation transitions as well as render vertex counts.

`LogicQA --mode ai` retries only pursuit/spacing, using the current single authored
navigation volume to place the existing 1200 cm corridor. The old fixed arena can
fall outside a user-edited HUB. This wrapper never moves, rebuilds or saves bounds.
It preserves prior receipts before invocation; require newly written native and
host completion files. Use `--mode services` or `all` only when those gates need
revalidation. The native 150 s / host 180 s bounds belong to the existing logic host.

Use the protected build/launch wrappers. Preserve user-saved HUB changes separately
from outfit commits. Complete Editor/Game, Blueprint, representative PIE/visual,
cook and packaged gates before declaring the persistent goal complete.
