---
name: ef-body-preset-workbench
description: Audit and onboard imported DAZ bodies in NoShellForWinter UE 5.8, diagnose Epic skeleton conversion, and integrate player body presets or NPC mesh and morph variation. Use for new Male/Female variants and scoped body replacement after the user imports the mesh.
---

# EF Body Preset Workbench

Start with `noshellforwinter-unreal-mcp` and the target project's AGENTS.md. Target is
`D:/Projects UE5/NoShellForWinter`; LustAsDeadlySin is read-only. Discover live tools,
then inspect without saving assets. The shared preset catalog and NPC persistence are
implemented. A G9 conversion adapter and reusable onboarding script are available;
check the live implementation ledger for the current certification gates.

## Intent and ownership

- The user exports/imports DAZ meshes with morphs. Discover the actual mesh under
  `/Game/DazToUnreal`; normal onboarding starts from that imported asset. Do not
  import another FBX or infer asset paths from names alone. An explicitly scoped
  disposable import fixture may be used to test the converter, with separate paths.
- An additional preset preserves the original mesh and its users. Replacement is
  a separate, scoped operation; never turn adding Male preset 2 into a global Male replacement.
- Prefer one project-owned body catalog shared by player and NPC consumers. Reuse
  EFCharacterCreation for appearance and EFProjectSystems for NPC selection. Keep
  clothing authoring in its existing Director, not a duplicate garment database.
- For analysis requests, inspect and document. Apply conversion/registration when
  requested; existing task authorization does not need to be asked for repeatedly.
- Do not use the legacy `ue5-male2-reference-replacement` UE 5.7 commands or its
  historical Male2 paths in this target. Do not delete/rename Male to add a preset.

Read [onboarding.md](references/onboarding.md) for conversion, integration and
validation. Current evidence and implementation gaps are in
`D:/Projects UE5/NoShellForWinter/Docs/Migration/BodyPresetsAndDazOnboarding_20261008.md`.
Reinspect the implementation before relying on that dated baseline.
Read [runtime.md](references/runtime.md) for the implemented schema, verified commands,
test evidence and current limitations. The live implementation ledger is
`Docs/Migration/BodyPresetsImplementation.md` in the target project.

## User's conversion convention

For an unconverted imported body: **Convert To Epic Skeleton > SKM_UEFN_Mannequin**,
**Merge Type = New**, save the generated skeleton at the imported body's own skeleton
path in the same folder, accepting replacement of that specific imported skeleton.
For Male2 this is `/Game/DazToUnreal/Male2/Male2_Skeleton`.
Check referencers and snapshot the affected packages before the authorized conversion.
Do not replace the shared UEFN skeleton with the new body's skeleton.

This menu sequence alone is not a completion gate. In the installed converter source,
`ConvertToEpicSkeleton` commits the edit, then merges into and assigns the selected
mannequin's skeleton, changes its retarget modes, and rebuilds the body's physics asset.
Therefore **read the final mesh Skeleton property after the call**. The New dialog
does not guarantee that the local skeleton remains assigned. Do not rerun conversion
on an already converted body to repair an assignment problem.

Use `Tools/BodyPresets/OnboardBody.py`: inspect first, then prepare the authorized
candidate from a JSON request. It routes raw G9 input to the project-owned adapter,
and already converted input to local rebinding. Do not invoke the vendor's final
shared-skeleton merge. `ConvertGenesis9BodyReport` executes the reviewed G9 recipe,
creates an isolated tree before commit, replaces only the imported local tree and
rebuilds its own physics. It rejects shared/protected resources and a second conversion.
The recipe reproduces the user's New/local replacement outcome without modal UI.
Different DAZ generations are not automatically certified by this G9 recipe.

`EFBodyOnboardingLibrary.BindLocalSkeleton` implements the second path for an already
converted candidate. It validates the local hierarchy and animation core, copies
animation metadata to the local skeleton, and never saves the shared reference.
Do not patch the installed plugin or use a compatibility flag to conceal bad hierarchy.

## NPC rest-state and roster integration

For NPC roster authoring and spawn repairs, also use the project-owned
`../ef-npc-daz-workbench/SKILL.md`. Its manifest, owned initializers and bounded
audit are the current integration path; do not revive one-off Male2 authoring scripts.

Every NPC-enabled male preset must declare supported neutral morph defaults.
`OnboardBody.py` validates `npc_neutral_morphs` before conversion/registration,
recognizes the current `DK_Flacid 03=1` / `DK_Erection=0` pair, and marks those
profiles `bNPCNeutral=true`, with NPC randomization disabled. For another body,
inspect the actual targets and explicitly supply its mapping. Neutral defaults
must survive spawn, preset reset and legacy NPC save migration. Validate actual
visible rest; a zero erection slider alone does not establish flaccidity.

## Completion truth

Separate skill/documentation validation from asset/runtime certification. For a
converted or integrated body, record build, Blueprint compile, PIE, visual QA, cook,
packaged validation and protected invariant hashes with exact paths and logs.
Use PENDING for unperformed gates, FAIL for observed failures. Never report that
Male2 is selectable or NPC diversity is operational based on documentation alone.
