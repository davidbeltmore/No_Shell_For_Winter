# Male player initial morph state

Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8). Source `D:/Projects UE5/LustAsDeadlySin` was not accessed or written during this correction.

## Change

Reverted the earlier global `DefaultGender=Male`, the two configured `MorphEntries` overrides, and the change that forced configured gender over the actor's authored mesh. Female remains the configured fallback; authored gender inference is restored.

`Plugins/EFCharacterCreation/Source/EFCharacterCreationRuntime/Private/EFCharacterCustomizationComponent.cpp`, `InitializeMalePlayerMorphState`, now populates absolute initial state values for a player-controlled Male pawn: `DK_Erection=0.0`, `DK_Flacid 03=1.0`. Called for first initialization and explicit gender selection. Reinitialization retains captured state; preset loading retains supplied values. The morph entries' original zero defaults and Reset behavior are unchanged. NPCs do not receive this player-only initial state.

## Evidence

- Native MCP `http://127.0.0.1:8000/mcp`, BlueprintTools and ObjectTools: inspected `/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale`; its mesh is `/Game/DazToUnreal/Male/Male.Male`, animation class `/Game/FullSample/Animations/AnimBPs/ACF_MMHumanoid_ABP`. Its graphs delegate to their parent; no direct morph assignment was found in these graphs.
- Native Unreal Python runtime inspection: the previous global override affected NPC customization components too (`DK_Erection=0.72`), so that override was removed.
- Live visual probe on the actual possessed `BP_TSChar_C_0` in HUB: zeroing Erection alone left the shape extended; applying `DK_Flacid 03=1.0` produced the flaccid appearance. No skeletal mesh or NPC asset was saved.
- Build PASS: canonical `Tools/Migration/Build-NoShellForWinterEditor58.ps1`, compiled and linked `EFCharacterCreationRuntime`; `Saved/Migration/MaleMorphCorrection/Build.log`. Daz receipt repair PASS. Editor reopened through `Launch-NoShellForWinterEditor58.ps1`.
- Blueprint compile: native MCP `BlueprintTools.compile_blueprint` for `/Game/FullSample/Player.Player` returned successfully; asset not saved.
- Fresh PIE started after rebuild; native Python `select_gender(MALE)` executed. Automated metadata read then failed because the private property was not exposed to Python. A subsequent probe found no player after PIE ended. Do not treat that probe as an automated assertion PASS.
- User visual acceptance: **“Ya funciona”**, after the rebuilt session and Male selection.
- Full cook and packaged validation: PENDING, not performed for this correction.
- Protected hash scan: `Saved/Migration/MaleMorphCorrection/ProtectedInvariants.json`. Daz plugin matches the July Phase0 manifest. ACFU and target Daz content differ from that historical manifest; historical equivalence PENDING. No baseline was rewritten and no attribution of those differences to this correction is claimed.
- Current changed-file SHA256 snapshot: `Saved/Migration/MaleMorphCorrection/ChangedFilesSHA256.json`. Working tree contains unrelated existing changes; no commit created.

Temporary native Python remote execution was restored to disabled after inspection. UECP Python was unavailable due to license verification; it was not used to execute the inspection.
