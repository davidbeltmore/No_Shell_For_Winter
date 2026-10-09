# Project-owned DAZ NPC roster — 2026-10-09

Target: `D:/Projects UE5/NoShellForWinter`, Unreal 5.8.3, ACFU 4.4.2.
Version corrected from local descriptor and Epic installation metadata on 2026-10-09;
the previous 4.3.5 label was stale. Protected files have not changed between tasks.

**Scope correction:** the original runtime gate below validates appearance and
animation with AI stopped. It did not prove interactions or pursuit. The reported
`Default Name`/empty dialogue regression requires effective inherited component
repair; functional logic remains PENDING until the dedicated NPCLogic gates pass.
Legacy source `D:/Projects UE5/LustAsDeadlySin` was not changed or resaved.
Starting snapshot: `80e4fd958e77ef0b9737dafaf99637e933f260bc`.
Pre-existing FullSample assets and unsaved HUB work were preserved separately in
`0549ab4ab5761efda5b38bd7a93a897148bc5d0f`. The original disk HUB and the saved
pre-migration HUB are retained under `Saved/NPCDAZ/Baseline` and `Originals`.

## Delivered scope

`Tools/NPC/Roster.json` is the source-to-owned authoring manifest: 20 humanoid
archetypes, each with Male and Female variants (40 total). Existing 22 enemy and
companion public paths are preserved; 18 service, civilian, Zombie and NPCAI
variants complete the roster. Spider, horse, wyvern, boats, player and menu-only
classes are explicitly excluded because they are not humanoid NPC body consumers.

| Owned path below `/Game/_Game/Characters` | Purpose |
| --- | --- |
| `Male`, `Female` | 40 spawnable role-preserving Blueprint variants |
| `Framework` | Six copied Blueprint ancestors, reparented to owned copies through the native C++ boundary |
| `Animation/ABP_DAZHumanoid`, `ABP_DAZZombie` | Complete animation templates, including the Zombie-specific behavior |
| `Data/Male`, `Data/Female` | 40 initializers matching each mesh and animation class |
| `Data/DA_NPCRoster` | Runtime soft-class catalog keyed by archetype ID and role |

Sources are the installed target's `/Game/FullSample/Blueprints/Characters`,
`/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs` and
`/Game/FullSample/MetaHumans/ACF_NPCAI_BP`. Exact paths are in the manifest and
`Evidence/NPCDAZ/author.json`. Native ACF classes, component libraries, abilities,
equipment, dialogue data and animation sequences remain shared dependencies.
This isolates character Blueprint ownership without copying the entire framework.

HUB Vendor and LevelingGuy instances were replaced through Unreal's native
`ReplaceActors`, retaining actor paths and common instance properties; no source
roster NPC instances remain in HUB. Vendor GUID/transform/path preservation was
explicitly compared. LevelingGuy path preservation was checked; its first GUID
comparison used Python wrapper representations and was invalid, so independent
before/after GUID equality for that first replacement is **PENDING**. The native
replacement implementation preserves GUIDs; this is not presented as a measured
comparison. No wholesale map/reference consolidation was performed.

## Appearance and animation contracts

`UProjectEnemyVisualVariationSubsystem` accepts `EF.BodyVariation` before spawn
processing and uses the shared `/Game/_Game/Data/CharacterCreation/DA_BodyPresets`.
Same-gender body weights, supported morph dimensions and skin tone vary from a
stable identity seed. ALS stores the resolved appearance. Placed NPC identity uses
the normalized object path; dynamically spawned identity tags are saved. Class
defaults must never contain `EF.AppearanceId.*`. `EF.BodyPresetFixed` opts out of
body selection while retaining morph/skin variation.

Both current male profiles now explicitly contain `DK_Flacid 03=1` and
`DK_Erection=0`, marked `bNPCNeutral`, with NPC randomization disabled. Previously,
the body profile reset could erase the desired rest state when selecting a new
preset. `FProjectNPCAppearanceRecord::MigrateNeutralDefaults` repairs generator-v1
records once without discarding their body, skin or other morph values. New records
use generator version 2; later saved custom states are not continually reset.

`OnboardBody.py` checks supported neutral targets before conversion/registration.
New male bodies with different target names must supply `npc_neutral_morphs`.
Adding a body does not replace existing assets or require another copy of every
NPC. The catalog and the bounded audit enforce the rest-state contract.

SkillGuy's simple source animation template had empty moveset/overlay tables and
no authored initializer. The owned variants use the complete animation template
and matching initializer. The existing project ACF bridge refreshes animation
tables and the movement-component AnimInstance cache after skeleton/body changes.
Assigning only an AnimClass is insufficient: initialization could otherwise restore
the original mesh or leave an instance without locomotion data.

The MetaHuman-derived NPCAI renders one DAZ body; original face/clothing/groom
payloads are hidden/cleared. External AI dialogue reports a missing API key in the
current project. External conversation/voice and DAZ facial lip-sync are **PENDING**,
not certified by this body/animation migration. Role graphs and components are
retained; exhaustive combat, service UI, networking and crowd performance are
outside this bounded validation.

## Validation ledger

Versioned receipts are in `Evidence/NPCDAZ`; complete logs, backups and failed
diagnostics remain under `Saved/NPCDAZ`. Each receipt belongs to the implementation
snapshot recorded in `Evidence/NPCDAZ/manifest.json`.

| Gate | Result / measured scope | Receipt |
| --- | --- | --- |
| Editor build | PASS, 44.981 s; project wrapper preserves enabled DAZ receipt | `editor-build02-process.json` |
| Game build | PASS, 33.504 s | `game-build02-process.json` |
| Authoring | PASS, 40 variants, 20 archetypes, six owned ancestors | `author.json` |
| Blueprint/structural audit | PASS after cold editor reload, 409 checks; all 40 variants compiled in 11.016 s | `audit.json` |
| PIE + visual | PASS, 12 representative live, upright, animated DAZ NPCs; 11.085 s | `pie-runtime.txt`, `pie-roster.png`, `pie-host.json` |
| Legacy/restoration | PASS, v1 neutral migration and 12 appearance restorations | `pie-runtime.txt` |
| Real ALS disk save/load | PASS, 12 appearances exact, both male bodies, varied morphs, player unchanged; 20.813 s | `npc-save.json`, `saveguard-summary.json` |
| Cook/package | PASS, 289.389 s outer process, exit 0 | `package-result.json`, `package-process.json` |
| Packaged runtime + visual | PASS, 12 NPCs, 11.038 s test / 46.487 s process, exit 0, zero feature errors | `packaged-runtime.txt`, `packaged-roster.png`, `packaged-process.json` |
| Protected invariants | PASS after assets and final phase, 7,643 files unchanged; final rehash 28.48 s | `protected-summary.json` |

The native smoke samples Male/Female MeleeEnemy, SkillGuy, Vendor, Routine, Zombie
and NPCAI. It requires supported body selection, neutral values, living/non-ragdoll
upright pose, valid ACF cache identity and changing bone transforms. The visual gate
is separate: an earlier numeric-only PASS had horizontal meshes due to positional
Python Rotator arguments. That run is rejected and preserved as
`Saved/NPCDAZ/numeric-only-invalid-visual.txt` and `invalid-mesh-frame.png`.
Named arguments and upright/alive checks corrected the observed failure.

The packaged run emitted two external-dialogue missing-key errors, documented above.
It also emitted ACF ActionsManager initialization and DirtyPawn binding warnings;
the scope does not certify those optional consumers. There were no Blueprint runtime
errors, handled ensures, fatal errors or missing owned-roster assets. Gallery images
show upright DAZ bodies and variation; a HUB door occludes some rear-row bodies,
so the image alone is not evidence of every sampled character's anatomy.

Package retention dry-run and apply removed only the prior validation package's
Cooked/Staged directories: 4,417,408,293 bytes before, zero remaining at those two
paths. `Package_20261009_031349` is retained as the successfully validated package.
All old receipts, logs and rollback/save backups remain. See `package-retention.json`.
The final editor session is back in HUB with no dirty content or map packages.

## Repeatable maintenance

Use `.agents/skills/ef-npc-daz-workbench/SKILL.md` and the versioned
`ef-body-preset-workbench`. Start with read-only live Unreal MCP context.

1. Extend `Tools/NPC/Roster.json`; inspect source role, parent and initializer.
2. Run `Author.py` through `Tools/InnerState/RunEditorPython58.py` with
   `Bounded.py author 210 -- ...`. Internal authoring limit: 180 seconds.
3. Run `Audit.py` through the same channel (120-second internal gate, 150 outer).
   Expected counts derive from the manifest. No vendor packages are saved.
   Also run `AuditLogic.py` after authoring/reparenting: compilation and appearance
   alone do not detect lost inherited SCS defaults. See `NPCLogicRepair.md`.
4. Run `RunPIE.py` (120 seconds; native watchdog 90) and review the screenshot.
   For gameplay changes run `RunLogicPIE.py` (180 host / 150 native) and require
   exact service menus plus natural melee approach/ranged spacing.
5. For persistence changes use bounded `RunNPCSaveQA.ps1` with SaveGuard (240 outer).
6. Build through Migration wrappers, package with `Package-BodyPresets58.ps1`
   (900 outer), then `Tools/NPC/ValidatePackaged.ps1 -StagedRoot <package>/Staged`
   (150 internal, 180 outer). Its UserDir is isolated from real saves.
   Run `-Mode Logic` as well for gameplay changes (210 internal ceiling).
7. Compare `Protection.py after` against the immutable task baseline. Preserve one
   successfully validated package using the project's dry-run/apply retention tool.

Durable authoring uses `set_editor_property` for struct arrays and explicit saving
of only scoped packages, followed by cold/cooked confirmation. Rebuild class/CDO
reference maps after each Blueprint compilation. Use named Rotator arguments and
`Guid.to_string()` when comparing identities. Do not grow the test matrix once the
scoped gates pass; each retry must address a specific observed failure.
