# Implemented runtime and tested workflow

Target: `D:/Projects UE5/NoShellForWinter`. Evidence ledger:
`Docs/Migration/BodyPresetsImplementation.md`; machine reports in `Saved/BodyPresetsQA`.
Recheck pending gates there. Core source commit: `aaae20ae` (2026-10-08).

## One catalog, two consumers

`/Game/_Game/Data/CharacterCreation/DA_BodyPresets` is `UEFBodyPresetCatalog`:
stable ID, display label, gender, soft mesh, enabled/NPC flags, weight, and per-body
named morph ranges/defaults/random ranges. Row order determines the default and UI order.
Current IDs: `Male.01`, `Male.02`, `Female.01`. Five bodies per gender needs data rows,
not more gender branches. Importing an asset does not itself certify/register it.

Use `EFCharacterCustomizationComponent.select_body_preset(id, reset_morphs)`;
never directly swap only the visible component. `set_gender` preserves a current
same-gender variant and remembers the last variant for each gender. Existing
`select_gender` remains the public UI contract. Body choices are gender-filtered.
The state saves BodyPresetId, legacy mesh path, resolved morphs, version and per-gender
memory. Old path-only saves migrate; deleted presets fall back to the gender default.
Morph discovery uses actual mesh targets, identically in editor and cooked game.

The catalog directory is an explicit cook root in `Config/DefaultGame.ini`; keep this
rule when adding rows, so soft-selected new bodies reach normal packaged builds.

## NPC lifetime

`ProjectEnemyVisualVariationSubsystem` selects a weighted catalog body before morphs.
Only the chosen mesh is synchronously loaded. Cold-load/frame costs must still be
measured; warm switch time is not a streaming guarantee. Existing optional-morph
content-policy gates remain active. Player-controlled pawns and remote clients are excluded.

`ProjectNPCAppearanceState` is an ALS-savable world actor. It stores resolved state,
generator version and seed. `ProjectNPCAppearanceTravel` carries records between worlds.
Placed pawn identity uses normalized actor path; dynamic identity uses the ALS-saved
`EF.AppearanceId.<guid>` actor tag. An explicit stable `EF.AppearanceId.*` tag wins.
Eligible enemy classes are recognized by loaded ancestry against soft paths, so a
freshly loaded class does not wait for the whole enemy roster. Other NPCs opt in with
`EF.BodyVariation` authored before processing. Do not duplicate explicit identity tags.
Save/load wins over randomization; generation waits while ALS is busy.

## Male2 and clothing

Male2 owns `/Game/DazToUnreal/Male2/Male2_Skeleton`, 196 mesh bones and 416 morphs.
It initially pointed at the shared UEFN skeleton with five conflicting parent links.
Rebinding its compatible imported local skeleton repaired the validator failure.
It initially lacked a default deformer; the candidate now uses
`/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth`.

Register each body once in the existing Clothing Director. Native source fits accept
a declared compatible local skeleton only when garment skin-palette bones and their
ancestors match. Unused imported internal branches are not deformation requirements.
Separate body-skeleton fingerprints protect local metadata. Keep source garments and
the shared skeleton untouched. The compiler produced 18 valid fits for six garments
and three bodies; visual certification is a separate gate.

## Verified tooling and pitfalls

- `EFBodyOnboardingLibrary.inspect_body`: actual mesh-local bones/parents/poses,
  skin palette, morph count, LODs and assigned/proposed skeleton compatibility.
- `Tools/BodyPresets/CaptureBaseline.py before|after`: protected plugins/assets,
  candidate backups. Existing before evidence is immutable.
- `OnboardBody.py` is the maintained inspect/prepare entry point. The old one-off
  Male2 catalog/profile recipes are not current executable entry points.
- Native test `NoShellForWinter.BodyPresets.CatalogAndState`: deterministic pool,
  selection, gender memory, cancel, real SaveGame codec, legacy/fallback contracts.
  Use a transient character in the initialized editor world: manually initializing a
  second world caused a WorldSettings naming fatal in this project.
- `RunNPCSaveQA.ps1`: real 12-NPC ALS disk round trip through SaveGuard; the successful
  2026-10-08 run restored IDs, profile morphs and skin exactly. Spawn timing excludes
  deferred appearance work. Preserve failures and SaveGuard restoration reports.
- `RunVisualQA.ps1`: six garments across the requested body cases, coverage, morphs,
  movement and screenshots. Numeric PASS does not replace viewing the images.
- `Project.BodyPresets.QA [quit]`: development runtime fixture for packaged body
  switches, pose checks, movement and creator screenshots; verify its current result.

## Animation integration gate

The initial visual run failed even though both components had an AnimInstance and
the skeleton validator passed. A skeleton-changing swap recreates CharacterMesh0's
AnimInstance. ACF 4.3.5 caches its old instance in the movement component at BeginPlay;
fragment-authored MovesetLayers/OverlayLayers are absent on the replacement instance.
This can freeze both the candidate and the next switch back to an established body.
The project ACFU bridge restores the declared tables, reconnects the movement cache
and reselects the active gameplay tags. Check the live cache identity, linked layers,
actual bone motion and equipment changes after every skeleton-changing transition.
Never certify animation from the existence of an AnimInstance alone.

A local skeleton also changes UE's cross-skeleton pose remapping. Inspect both mesh
bind transforms and skeleton animation reference transforms. Converted DAZ meshes
were prepared for direct UEFN animation rotations; matching names/parents alone does
not prove reference-frame equivalence. The onboarding adapter now aligns animation
frames on the common hierarchy while preserving the mesh bind pose and local extra
branches. Real PIE body/cloth/physics and creator visual gates have passed; consult
the ledger for cooked/package status rather than inheriting a prior run's result.

Use the protected build/launch wrappers. `-ExecutePythonScript` exits unless the
script keeps Python alive. With spaces, invoke the launcher from PowerShell directly
under process-scoped execution policy, or put the command in a project script;
nested `powershell -File` quoting can split AdditionalArguments incorrectly.

Fresh G9 conversion passed a legacy FBX import fixture: 196 bones, 416 morphs,
unchanged morph-delta digest/materials/LODs, local skeleton and rebuilt local physics.
The result exactly matched the existing Male2 mesh's names, parents and bind transforms.
`FreshImportConversionQA.py` uses an explicit FbxFactory, as the installed DAZ bridge
does; UE 5.8 Interchange's FbxImportUI adapter omitted the requested physics asset.
Use set_editor_property for editor-only import fields. Never use the fixture importer
as the normal production onboarding entry point.

`ConvertGenesis9BodyReport` retains error details in Python. A Blueprint bool return
plus an out error string is exposed by Python as None on failure, discarding the error;
do not mistake None for a successful empty error string.

The creator exposes mutually exclusive Body / Skin preset checkboxes directly below
Gender on Info, and also under Body. Do not hide body selection exclusively in Body
or in the saved-appearance PRESETS accordion. Its automation checks actual checkbox
delegates, two male/one female choices, exclusive selection, gender memory and
Apply/Cancel/reopen. The retarget follower inherits
ACF fields but does not own currentMovesetInstance: require that layer only on the
CharacterMesh0 animation source. Require bone motion on both components.

Physics profile precedence is explicit settings override, then catalog row. Resolve
the actual simulation owner and verify live constraints and IsSimulatingPhysics;
the visible follower does not necessarily own simulation. A catalog is retained by
a GC-owned settings UPROPERTY, never by a static strong UObject pointer.

The raw FBX reproduced the same 3,077 zero normals and four zero tangents as Male2
before conversion. Keep this geometry/import warning separate from skeletal hierarchy
errors; do not change topology or strip morph vertices to silence it without validation.

End PIE before closing bounded Python hosts, wait for teardown, release delegates
and UObject wrappers, then quit. Preserve process-exit failures separately from numeric
test passes, and verify SaveGuard restored the user's saves.


## Python automation shutdown (UE 5.8)

When launched with `-ExecutePythonScript`, complete the asynchronous test, stop PIE,
unregister callbacks and set `EditorPythonScripting.set_keep_python_script_alive(False)`.
Let `FEditorPythonExecuter` destroy its progress notification and schedule deferred
QUIT_EDITOR. Calling `quit_editor()` directly while keep-alive is true bypasses
that normal path. NPCSave03/04/05 passed save assertions but crashed during Slate
text/widget destruction at process exit; NPCSave06 used normal script completion
and exited cleanly with saves restored. Runtime/packaged success is separate from
process-exit success. Remote-injected scripts without the startup runner may still
request ordinary editor quit after cleanup. Never patch Engine for this harness fix.

The current supported commands, G9 scope and evidence are also recorded in the
project's `Docs/Migration/BodyPresetWorkflow.md` and `BodyPresetsImplementation.md`.
