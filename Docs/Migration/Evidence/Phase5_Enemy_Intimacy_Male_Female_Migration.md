# Phase 5 — Enemy/Companion Male/Female and Intimacy migration

Date: 2026-07-16
Branch: private migration branch (not published)
Reference: legacy private build (read-only; identity omitted)
Target: NoShellForWinter

## Result

The project-owned enemy and Intimacy migration is implemented for 22 current UE 5.8 character classes:

- 16 hostile classes: eight Male and eight Female.
- 6 companions: three Male and three Female.
- Hostile-only systems are excluded from companions.
- Identity, selection, Social Card, Chronicle, history and Intimacy apply to all 22.
- Optional mature morph configuration contains only the eight eligible hostile Male classes.
- Female receives shared body/skin variation and no optional mature morph configuration.

The public interaction id remains `Actions.Together.0001Scene`.

## Implemented runtime contracts

- `UEFProjectEnemySettings` contains explicit Male/Female identity registries and a hostile-only runtime registry.
- `UProjectCharacterIdentitySubsystem` applies one exclusive ASC gender tag and creates/updates `UProjectIntimacyPartnerComponent` for existing and spawned actors.
- `UProjectEnemyVisualVariationSubsystem` separates the shared 16-hostile list from the eight-character optional mature morph list.
- Intimacy profiles repair legacy gender from the live partner component/class registry and retain relationship/history progression.
- `T -> Y` can open the contextual Project Emote menu for a selected Intimacy partner during ACF combat.
- `T -> -` starts `Actions.Together.0001Scene` through the quick-start contract.
- `Y` cancellation restores movement/look input, clears both combat shields, closes the session and restores the selected target.

## Character assets and naming

All final character packages use the organized paths below:

- `/Game/_Game/Characters/Male/*BPMale` — 11 packages.
- `/Game/_Game/Characters/Female/*BPFemale` — 11 packages.

The 11 Female object paths end in `BPFemale`; no final Female object path contains `BPMale1` or a numeric suffix. A binary text scan can still see old generated function-symbol strings embedded in renamed packages; those are not package/object references.

Two pre-existing, untracked Male root redirector files remain locally under `/Game/_Game/Characters` after Editor delete, commandlet delete and `ResavePackages -FixupRedirects` all reported success without removing the physical files. They have no referencers and are deliberately excluded from Git. Status: `PENDING_EDITOR_REDIRECTOR_CLEANUP`.

## Intimacy assets

The optional presentation is isolated under the project-owned Intimacy content root. Exact
package-level import receipts are retained in private engineering evidence, in accordance with
the public evidence boundary. The scene uses the current `/Game/DazToUnreal/Female/Female` and
`/Game/DazToUnreal/Male/Male` meshes.

## Validation

| Gate | Result | Evidence |
|---|---|---|
| 22 character Blueprints + scene compile | PASS | Live UE 5.8 MCP compile: 23 compiled, zero failures |
| Final names/old paths | PASS | `BPMale1` asset search empty; three legacy `ExportedAnimations/Together` asset paths absent |
| Enemy registry automation | PASS | `Saved/Migration/Phase5/Tests/EnemiesFinal/index.json`: 2 succeeded, 0 failed |
| Intimacy automation | PASS | `Saved/Migration/Phase5/Tests/IntimacyFinal/index.json`: 14 succeeded, 0 failed |
| Cold Editor build after input fixes | PASS | UBT `NoShellForWinterEditor Win64 Development`, 48.10 s |
| `T -> Y` contextual menu | PASS runtime; PARTIAL visual | Runtime menu/widget states pass and `PARTNER` is visible in `02_ActionsCategory.png`; first desktop capture is rejected because Steam covered it, and accepted captures contain a small external reimport prompt |
| `T -> -`, active session and `Y` cancel | PASS | `Saved/Migration/Phase5/Runtime/EnemyIntimacyQuickStartPIE58.json` |
| Build/cook/stage/pak/archive | PASS | `Saved/Migration/Phase5/Package/EnemyIntimacyWin64FinalProtected`; UAT `BUILD SUCCESSFUL` in 238.14 s |
| Packaged content inventory | PASS | UnrealPak: 22 organized character assets and all 3 Intimacy assets present |
| Packaged interactive smoke | PENDING | Headless launch stayed alive but did not honor automated quit; no interactive packaged-session claim is made |
| Animation retarget/pose parity | PENDING | Explicitly permitted exception; asset load, references and package are blocking and pass |
| Optional mature morph transition | PASS for the private reference run; `PENDING` after policy rework | The authoritative Male mesh contained both configured transition endpoints. Current validation must be rerun in NSFW with adult acknowledgement and the Intimacy opt-in; SFW and Streamer Safe must remain neutral. Exact mesh-owned morph identifiers stay in private engineering evidence. |

The focused PIE result proves:

- quick start returned true;
- session and runtime action became active;
- runtime action id was `Actions.Together.0001Scene`;
- the active partner was preserved during the scene;
- player and partner shields were applied during the scene;
- after `Y`, session/action were inactive, target was restored, shields were absent, and move/look input were enabled.

## Source and protected invariants

Source read-only verification is `PASS` in `Saved/Migration/Phase5/Hashes/EnemyIntimacy_SourceReadOnly_Final.json`; source HEAD/status and recorded modified-file hashes are unchanged.

The immutable-reference verification covered the scene, both synchronized animation roles and
the Intimacy preview. Package paths and file-level hashes are retained in private engineering
evidence rather than the public repository.

Protected comparison against the original Phase 0 baseline still reports the 69 differences already recorded on 2026-07-15. Comparing the final state against `ContentOrganization_Post_ProtectedInvariants.json` gives:

- mismatch count before/after: 69 / 69;
- mismatch delta: 0;
- authoritative-asset delta: 0;
- task delta result: `PASS`.

ACFU (5,043 files) and the DazToUnreal plugin (213 files) match Phase 0 exactly. Final authoritative hashes are:

| Asset | Final SHA-256 | Task result |
|---|---|---|
| Player | `8B7E5EF8B831F4F13339FF6E06935E8D0DEAE1E7841F310A01D2D2AB6175EBAB` | unchanged from task start |
| Female | `B3BC3B6AEDF79C025F826305EFB219F827E3BE487DF7F2F7A4DC31FF17377BAB` | unchanged |
| Multiple | `350B862EC2CC547BC1820F17559880C25ACCCC063A669906E8FE6F1F8C17F780` | unchanged |
| Male | `6E4D5C11DFE71CC8FFB638950FAF897A96C580C06A2386D0D33CDAAE2C584217` | restored exactly to task-start LFS object after an Editor resave changed package metadata |
| Frederick | — | `PENDING_RUNTIME_IDENTITY`; no unambiguous package path exists in current evidence |

The rejected post-Editor Male package was preserved only under `Saved/Migration/Phase5/Hashes/ProtectedRecovery`; it is not staged.

## Targeting lifecycle hotfix and expanded visual QA

Date: 2026-07-16

The reported ACF targeting crash was reproduced to the possession-change cleanup path. ACF's
`GetBestTargetPointForTarget` dereferenced its internal `ControlledPawn` after the project attempted to
restore a target while the old pawn was already detached. Project-owned cleanup now skips target restore
during pawn changes and `EndPlay`, and `UProjectTargetingFixComponent` requires a valid, currently possessed
local pawn, camera manager and live targeting component before invoking ACF.

The expanded runtime reproduction is `PASS` in
`Saved/Migration/Phase5/Runtime/TargetingPawnChangePIE58_Hotfix.json`:

- selected a Male ranged enemy and started `Actions.Together.0001Scene`;
- called `UnPossess` while the session and runtime action were active;
- verified the controller had no pawn, then possessed the original pawn again;
- verified session/action inactive, pawn restored, movement/look enabled and both combat shields removed;
- no ACF access violation occurred.

That reproduction also exposed an independent project-owned reflection helper lifetime defect: parameter
memory was allocated with `FMemory_Alloca` in a helper and returned after its stack frame ended. All reflected
argument/return invocations now use `FStructOnScope`, keeping initialized parameter storage alive across
`ProcessEvent`.

### Hotfix validation

| Gate | Result | Evidence |
|---|---|---|
| Cold UE 5.8 Editor build | PASS | UBT `NoShellForWinterEditor Win64 Development`; `Result: Succeeded` |
| Detached-pawn automation | PASS | `Saved/Migration/Phase5/Tests/TargetingLifecycleHotfix/index.json`: 1 succeeded, 0 failed |
| Live possession-cycle regression | PASS | `Saved/Migration/Phase5/Runtime/TargetingPawnChangePIE58_Hotfix.json` |
| Enemy registry/gender suite | PASS | `Saved/Migration/Phase5/Tests/EnemySystemsHotfix/index.json`: 2 succeeded, 0 failed |
| Intimacy suite | PASS | `Saved/Migration/Phase5/Tests/IntimacyHotfix/index.json`: 14 succeeded, 0 failed |
| Build/cook/stage/pak/archive | PASS | `Saved/Migration/Phase5/Package/EnemyIntimacyWin64TargetingHotfix`; UAT `BUILD SUCCESSFUL`, 238.49 s |
| Packaged executable smoke | PENDING | Inner executable remained alive for 30 s under `-NullRHI`; automated `ExecCmds=quit` was not honored and the process was stopped explicitly |

### Expanded visual QA

Twenty-two clean screenshots were captured and manually inspected after disabling content-directory monitoring
for the QA processes:

- Female `T -> Y`: Root, Actions with `PARTNER`, and animation list in
  `Saved/Migration/Phase5/Visual/TargetedFemaleYMenuHotfix`.
- Male Intimacy: Social Card, scene HUD, Talk categories/detail, Items categories/detail, session interaction and session peak in
  `Saved/Migration/Phase5/Visual/IntimacyFullHotfix`.
- Female Intimacy: the same eight states in
  `Saved/Migration/Phase5/Visual/IntimacyFemaleHotfix`.
- The earlier three Male `T -> Y` screenshots remain functionally valid but are not counted as clean because an
  Auto Reimport notification was visible.

Female visual/runtime evidence shows `Gender: Female`, randomized `Lv. 2` and HP 564, an active Female partner
profile, `Actions.Together.0001Scene`, Chronicle events, session-interaction/peak state, target restoration and Niagara
cleanup. Male visual/runtime evidence shows the equivalent partner/session contracts and target restoration.
Menus and HUD panels are readable and do not overlap each other. The inherited bows/weapons visibly intersect
the actors during `0001Scene`; animation retarget/equipment hiding therefore remains `PENDING` rather than a
visual PASS.

Final invariant reports are:

- `Saved/Migration/Phase5/Hashes/EnemyIntimacy_SourceReadOnly_TargetingHotfix.json`: source read-only `PASS`.
- `Saved/Migration/Phase5/Hashes/EnemyIntimacy_ProtectedInvariants_TargetingHotfix.json`: ACFU 5,043/5,043 and
  DazToUnreal plugin 213/213 `PASS`; the same 69 pre-existing target Daz mismatches as the post-organization
  reference, with identical mismatch payload and zero hotfix delta.
- Player, Female, Multiple and Male hashes remain exactly the values recorded above.

## Final automated runtime hardening

Date: 2026-07-16

The earlier visual-equipment paragraph is superseded by this final pass. Project-owned scene playback now
snapshots attached equipment actors and their primitive visibility/collision state, suppresses them every
post-update tick while `Actions.Together.0001Scene` is active, and restores the exact state on cancel. This is
necessary because ACF can refresh equipped component visibility after an actor-level hide.

Visual comparison against the read-only private reference captures also exposed a Female substitution in the mixed
local Character Creation integration. The independently staged migration gate now authors and validates all 11
organized Male Blueprint CDOs against `/Game/DazToUnreal/Male/Male`; Female remains unchanged. On the validated
integrated workspace the final Male runtime has no scene mesh mismatch, uses the Male role animation, and logs
the optional mature morph transition. Concurrent Character Creation changes remain outside this commit.

### Final gates

| Gate | Result | Evidence |
|---|---|---|
| UE 5.8 build after equipment/gender fixes | PASS | `Saved/Migration/Phase5/Logs/MaleRuntimeGenderFixBuild.log`; UBT `Result: Succeeded` |
| Intimacy native suite | PASS | `Saved/Migration/Phase5/Tests/IntimacyGenderMeshFinalFull/index.json`: 15 succeeded, 0 failed |
| Enemy registry/gender/morph/mesh suite | PASS | `Saved/Migration/Phase5/Tests/EnemyMaleMeshRegistryFinal/index.json`: 2 succeeded, 0 failed; all 11 Male CDOs use the authoritative Male mesh |
| Automated Male/Female runtime soak | PASS | `Saved/Migration/Phase5/Runtime/IntimacySoak58_GenderMeshFinal/summary.json`: 2/2 cycles; four equipment actors suppressed and restored exactly per cycle |
| Male full visual sequence | PASS for the private reference run; `PENDING` after policy rework | Eight accepted captures in private engineering evidence; Male body/pose, optional presentation transition, Social Card `Gender: Male`, no intersecting weapons, HUD/Talk/Items/session interaction/peak |
| Female full visual sequence | PASS for the private reference run; `PENDING` after policy rework | Eight accepted captures in private engineering evidence; Female pose/body, no role-inappropriate optional mature morph, no intersecting weapons, HUD/Talk/Items/session interaction/peak |
| Private reference visual comparison | PASS | Captures inspected read-only; target preserves the synchronized pose and removes the reference weapon intersections |
| Cook/stage/pak/archive | PASS | `Saved/Migration/Phase5/Package/EnemyIntimacyGenderMeshFinal`; UAT `BUILD SUCCESSFUL` in 325.45 s |
| IoStore inventory | PASS | `Saved/Migration/Phase5/Package/EnemyIntimacyGenderMeshFinal.inventory.txt`: 11 Male, 11 Female and 3 Intimacy assets; `ACFMMEnemyBPMale` present |
| Packaged startup smoke | PASS | `Saved/Migration/Phase5/Package/EnemyIntimacyGenderMeshFinal.smoke.json`: inner executable alive for 20 seconds under `NullRHI`, then stopped explicitly |
| Source read-only | PASS | `Saved/Migration/Phase5/Hashes/EnemyIntimacy_SourceReadOnly_GenderMeshFinal.json` |
| Protected invariants | PASS task delta | `Saved/Migration/Phase5/Hashes/EnemyIntimacy_ProtectedInvariants_GenderMeshFinal.json`: ACFU 5,043/5,043, DazToUnreal plugin 213/213, and the same 69 pre-existing target-Daz mismatch payload |

The runtime automation executes the complete session path: target resolution, Social Card, quick-start scene,
active Intimacy profile/HUD, Talk, Items, session interaction, forced session peak/Niagara, cancel, target recovery, movement/input
recovery, combat-shield cleanup and equipment restoration. Male and Female both pass the same lifecycle.
