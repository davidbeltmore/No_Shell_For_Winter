# NPC gameplay parity repair — 2026-10-09

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.3 / installed ACFU 4.4.2.
Source game and installed plugins remain read-only. User HUB changes were preserved
in commit `1f156079` and two backups under `Saved/NPCLogic/Baseline` before repair.

## Cause and scope

The earlier authoring copied/reparented character Blueprints but did not rebuild
inherited SCS override keys against their new owned ancestors. Compilation and
appearance tests passed while effective gameplay component defaults were lost.
`InspectLogic.py` measured original Leveling Guy/Vendor names and dialogue assets
against owned `Default Name` and empty arrays. Defender also retained an extra
ambush condition absent from its source archetype. Evidence is in
`Saved/NPCLogic/inspect-logic.json`, `conditions.json` and the repair receipts.

`bGenerateNavigationOnlyAroundNavigationInvokers=True` also required an invoker
beyond the user's ordinary NavMeshBoundsVolume workflow. The scoped config repair
sets it false; procedural floors retain their bounded nav volumes and readiness
checks. No Calysto source assets or installed plugins are patched.

There was no intervening ACFU update: 7,643 protected file hashes match the earlier
task. See `ACFUInstallationAudit.md`. The old 4.3.5 documentation label was wrong.

The previous `Project.NPCDAZ.QA` stops brains and movement to measure bodies and
poses. It is appearance evidence only, and was insufficient to certify gameplay.

## Maintained repair boundary

`SyncOwnedNPCLogicDefaults` compares effective source SCS/native component defaults
and possession settings; it recreates inherited override keys in owned packages.
Combined-animation conditions are copied as owned subobjects. DAZ meshes, owned
initializers and intentionally suppressed MetaHuman visual payloads are preserved.
Unsupported instanced property families fail explicitly. `Author.py` invokes this
step after reparenting so subsequent roster extensions retain the source logic.

`AuditLogic.py` checks all 40 variants. `RunLogicPIE.py` drives
`Project.NPCLogic.QA` with native 150-second/host 180-second limits. It sends E through
the PlayerController input path, checks native detection/name/dialogue state, and
observes natural AI perception, a complete path and >200 cm approach without
forcing targets or issuing MoveTo. This is bounded representative validation,
not certification of every quest branch, network mode or combat action.

## Gates

| Gate | Status / receipt |
| --- | --- |
| Installed release / unchanged prior protected baseline | PASS; `Saved/InstallationAudit/acfu.json`, `Saved/NPCLogic/Baseline/preexisting-plugin-drift.json` |
| Editor C++ build | PASS; final exit-path build `Saved/NPCDAZ/logic-editor-exit-build-process.json`, 51.519s / 240s ceiling |
| Restore six owned ancestors and 40 variants | PASS; `Saved/NPCLogic/repair.json`, 46 Blueprints, 15.781s; commit `563f8a0c` |
| All-variant effective logic parity / Blueprint compile | PASS; `Saved/NPCLogic/audit.json`, 40 variants / 45,546 properties / zero differences; appearance structural audit 409 checks |
| Authored HUB navigation after rebuild | PASS; `Saved/NPCLogic/navigation-rebuild.json`, 10/10 projections, 1.0s; no floor/volume transform edits |
| Service interaction / exact menu / closure | PASS; `Saved/NPCLogic/services-pass.txt`, 6/6 menus and 2 quest-NPC detection contracts, 63.239s; host cleanup 65.843s |
| MetaHuman replacement payload | PASS; `Saved/NPCLogic/metahuman-payload.json`, 44 component observations, 8.703s; both primary meshes DAZ, secondary mesh/groom payloads empty |
| Native Guy comparison in current HUB quest state | PASS; `Saved/NPCLogic/native-guy-baseline.txt`, 7.296s; same name/detection/interaction and no generic dialogue as owned variants |
| Natural melee pursuit / ranged spacing in PIE | PASS; `Saved/NPCLogic/combat-pass.txt`, 4/4, 27.408s native / 32.938s host |
| Appearance/neutral morphology regression and visual review | PASS; `Saved/NPCLogic/appearance-pass.txt`, 12/12, 20.849s native / 23.610s host; `Saved/NPCDAZ/Runtime/Roster.png` reviewed |
| Game C++ build | PASS; final exit-path build `Saved/NPCDAZ/logic-game-exit-build-process.json`, 82.058s / 360s ceiling |
| Fresh HUB cook/package | PASS; `Saved/BodyPresetsQA/Package_20261009_182537/package-result.json`, 350.911s host / 900s ceiling |
| Packaged interaction and AI | PASS; `Saved/NPCLogic/Packaged_20261009_184653/process.json`, 6 exact menus + 2 quest interaction contracts + 4 movement samples, 92.056s native / 106.892s process, exit 0, zero detected feature errors/ensures |
| Packaged appearance / visual | PASS; `Saved/NPCDAZ/Packaged_20261009_184853/process.json`, 12/12 + legacy migration + restore, 12.112s native / 24.220s process, exit 0, zero detected feature errors/ensures; roster and vendor images reviewed |
| Final protected comparison | PASS; `Saved/NPCLogic/Baseline/after.json`, 7,643 files / zero changed, 12.175s host; snapshot `1276d7e6` |
| Validation package retention | PASS; dry-run then apply removed only two old Cooked/Staged outputs, 4,980,466,326 bytes; current `Package_20261009_182537` retained |
| Final editor appearance / clean HUB | PASS; 12/12 on final binaries, 23.034s native / 37.578s host; `Saved/NPCLogic/final-editor-state.json`: no PIE, no dirty content/maps |
| GitHub code synchronization | PASS; remote branch `feature/efclothing-v4-multicloth` confirmed at `1276d7e6b5ba6a2765b018738a396998f1fdbe2a`; 48/48 LFS objects uploaded. Final documentation/evidence commit is verified at delivery. |

## Failed automation retained

- A preliminary Python class-level navigation call targeted a CDO and hit an
  ensure; no valid AI baseline resulted. `probe-navigation-ensure.log` is retained.
- The first native comparator incorrectly treated locally owned SCS nodes as
  inherited; repair rejected them before saving the failing package.
- The initial deep-comparison call passed flags as the array index, crashing the
  editor. The corrected signature is `Identical_InContainer(A,B,0,Flags)`.
  `repair-comparison-crash.log` and the crash context are retained. The crash reporter
  held the DLL and caused a linker failure; a verified cold build repaired it.
- A subsequent startup failed in DevHttp/DDC `InheritedContext`, before running
  the repair. `startup-http-crash.log` is retained. The readiness probe now waits
  for the asset-registry initial broadcast before opening MCP requests; this avoids
  queuing timed-out requests during heavy startup, without modifying Engine code.

Unperformed or failed gates remain PENDING/FAIL, regardless of a host script's exit
status. Async PIE requires a fresh runtime receipt and completed cleanup.

## Runtime diagnosis notes

The native dialogue component Blueprint overrides its started event without the
native parent's flag update: `IsInDialogue` remains false even when the graph and
dialogue widget run. Verify the graph and visible active dialogue interface instead.
Service responses must open the exact service widget; HUD/Navbar reactivation is
not service evidence, and test cleanup must close only the opened service menu.

Dynamic fixtures provision a missing controller with `SpawnDefaultController`, as
ACFGroupAIComponent does. Bare native MeleeEnemy has no initializer asset; a native
baseline must include its source initializer to represent a configured archetype.
The first HUB pursuit fixture targeted an existing companion outside the nav volume.
The isolated corridor then acquired the player but still did not move. Both the
owned pawn and the configured native pawn reached `ACFCombatBT::Move To[22]`,
`Chase Target`, with a valid route, an active `AITask_MoveTo`, and idle path following.
Receipts: `owned-ai-task-stall.txt` and `native-configured-ai-stall.txt`.

Engine `UCrowdFollowingComponent::SetMoveSegment` synchronously rejects a path
whose NavData differs from CrowdManager's single NavData. That rejection happens
inside RequestMove, before AITask_MoveTo binds its completion delegate. The project
already handles this constraint for transient Calysto enemies. The new
`UProjectNPCNavigationSubsystem` checks actual selected/Crowd NavData on controller
changes and uses base path following only on mismatch, for owned NPC ancestry.
It preserves original controller/BT selection and never forces targets or MoveTo.
Startup readiness retries are bounded to 10 at 0.2s. The first adapter PIE measured
204.5cm/3.58s and 208.1cm/2.50s approach for male/female melee enemies. Logs confirm
selected `EFCalystoPlayerNavMesh-Player` versus crowd `RecastNavMesh-Default`.
The final representative combat gate passed all four samples: male/female melee
approach and male/female ranged retreat. Receipt: `combat-pass.txt`.

The initial ranged assertion was incorrect for the source role: `DA_ACFRangedCB`
chooses ranged attacks above 100cm and flee below 350cm (70% chance). The fixture
now observes ranged attack state, approaches with the player and requires natural
retreat, while melee samples require approach. This changes the test stimulus,
not authored combat behavior. `ranged-distances.json` records the actual thresholds.

A cold MetaHuman-derived class load took 8.597s and consumed the appearance
fixture's five-second settling interval before the first update could run. The
settling deadline now starts after all synchronous spawning. The failed receipt
is retained as `appearance-cold-load-settling-fail.txt`. The corrected run passed
all 12 pose, morphology and restoration samples; the roster image was reviewed.

## Packaged catalog GC regression

The first packaged logic run opened all six service menus, then exited with code 3
at the first regular collection. `Saved/NPCLogic/Packaged_20261009_183140/game.log`
identifies `Default__EFCharacterCreationSettings.LoadedBodyPresetCatalog` as a
disregard-for-GC object referencing the runtime-loaded `DA_BodyPresets` outside the
root set. This was a latent body-catalog ownership error, not a missing NPC menu.
The settings CDO now holds only its configured soft reference. A regular
`UEFBodyPresetCatalogCache` EngineSubsystem owns the loaded asset and releases it
on Deinitialize. Early commandlet access can load without attaching a hard reference
to the CDO. This also avoids static strong-pointer destruction after UObject teardown.

Only native code changed after the successful cook. Binary restaging reuses that
cook after checking no Content/Config/project-descriptor drift from `2361abd9` and
compares SHA256 of the staged and newly built executable. Both packaged gates must
pass with clean process exit; the failed first run remains preserved.
The restage UAT itself passed (exit 0, 82.996s). Its one-off host initially omitted
`Staged/Windows` from the executable lookup and therefore reported FAIL; no package
operation was rerun. `binary-verification.json` independently verifies the corrected
path against the built executable (SHA256 `AC23DE532227652442C8754B23300DC92F2EEEA62E63C09D01CDBBFDF8878815`).

The next run, `Packaged_20261009_184109`, passed all gameplay assertions in 83.965s
and exited with code 0, confirming the catalogue collection fix. Its log nevertheless
contained a shutdown ensure in `EFEquipmentDeathRouting::Replace`: expired weak
listeners resolved to null during the delegate serialization round-trip and were
inserted twice. The project adapter now skips unbound listeners and rebuilds unique
bindings in first-valid-listener order. The final package is accepted only after a
fresh run also has zero ensures. This is an exit-path repair, not an equipment redesign.
The last code-only correction refreshes only the staged `.exe` and its `.pdb`;
the Content/Config/descriptor comparison remains empty. Final executable SHA256:
`a69171c9ac8a5ae0d864a6dfe4118ba2ac5f686644df5d2bfb3cf1637fd49a11`,
snapshot `1276d7e6`, refresh/verification 14.300s.

The final logic and appearance packaged runs both exited normally with zero feature
errors/ensures. Versioned receipts and selected images are in
`Docs/Migration/Evidence/NPCLogic`; `manifest.json` records artifact hashes, exact
source paths, 92 owned asset hashes and the tested code snapshot. The roster view
confirms upright varied DAZ bodies, but the existing HUB door occludes some middle
characters; numeric morph/pose checks supply the complementary per-sample evidence.
This gate does not certify firearm holding poses, every quest branch, actual shop
transactions, network play or large-crowd performance.
The final cold editor startup exceeded the initial 120-second readiness probe while
loading plugins/assets. Without restarting, the additional probe reached MCP HUB in
67.623s within its 90-second outer ceiling. Both receipts are retained. The subsequent
short PIE appearance test passed and the editor was left in a clean HUB.

## Firearm pose report (separate follow-up)

The user reports deformation while equipping/holding firearms on both player and
NPC. They explicitly requested only a brief diagnosis and prioritization of NPC
logic completion. No firearm-animation fix or pose certification is claimed.
The existing movement screenshot `Runtime/Enemy_02.png` captures the retreat phase,
not a controlled held-weapon pose, and cannot establish the root cause.
Both consumers use ACF animation data with DAZ bodies; the player additionally has
a visible retarget follower. A shared weapon overlay/hand-IK/retarget mismatch is a
candidate, not a confirmed diagnosis. Isolating it requires comparing the evaluated
held-weapon pose before/after its overlay and IK layers in both consumers. This
bounded follow-up remains PENDING, independently of interaction/navigation gates.
