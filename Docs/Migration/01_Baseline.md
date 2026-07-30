# NoShellForWinter baseline

Status: `COMPLETE`

## Recovery and build baseline

| Check | Result | Evidence |
|---|---|---|
| External restorable snapshot | PASS | Private baseline; 5,916/5,916 SHA-256 matches |
| Local Git/LFS baseline | PASS | Private migration baseline captured; public repository history starts from the sanitized tree |
| Target EngineAssociation | PASS | `NoShellForWinter.uproject` reports `5.8` |
| Generate project files | PASS | `Saved/Migration/Logs/Phase1_GenerateProjectFiles_Retry_20260713.log` |
| Development Editor Win64 | PASS | `Saved/Migration/Logs/Phase1_DevelopmentEditor_Build_20260713.log` |
| Development Game Win64 | PASS | `Saved/Migration/Logs/Phase1_DevelopmentGame_Build_20260713.log` |
| Compile target-owned Blueprints | PASS | 34 failing assets reduced to zero; `Phase1_CompileAllBlueprints_AfterOrphanCleanup_20260713.log` |
| Compile all enabled Blueprint content | BLOCKED_EXTERNAL | one immutable ACFU asset remains: `ACF_PickAction_BP`, two unique compiler messages |
| Minimal cook `/Game/FullSample/Test` | PASS | 4,881 packages; `Phase1_MinimalCook_TestMap_20260713.log` |
| Visible PIE baseline | PASS | `Saved/Migration/PIE_Baseline_Python/20260713_040617` and tracked evidence JSON |
| Legacy reference unchanged | PASS | Read-only verification completed; detailed private fingerprint is intentionally not published |
| ACFU/Daz/Player invariants | PASS | 5,043 ACFU files, 213 Daz plugin files, 189 target Daz assets, and four authoritative assets match byte-for-byte |

## Blueprint baseline repair

The initial commandlet returned 83 with 34 failing assets and 61 unique compiler messages. The dominant defect was a stale `/Script/TP_ThirdPerson` module identity in the UE 5.8 template content. Four narrow Core Redirects map the three renamed template bases and the old package to `/Script/NoShellForWinter`.

After the redirects:

- `/Script/TP_ThirdPerson` log occurrences: 269 to 0.
- Failing assets: 34 to 2.
- Unique compiler issues: 61 to 5.

The remaining project asset, `/Game/FullSample/Integrations/ATSIntegrations/Dialogue/SampleDialogueButton_WBP`, was a zero-referencer orphan inherited byte-for-byte from the current FullSample 5.8 distribution. The live Player and widget registry use ACFU 4.3.5 `ACF_MinimalDialogue_WB`. The orphan was quarantined and deleted through Unreal Editor AssetTools after hash and referencer gates; no raw `.uasset` operation was used.

The final global commandlet returns 2 only because immutable Marketplace asset `/AscentCombatFramework/Blueprints/Abilities/ACF_PickAction_BP` still calls removed `GetInventoryComponent` API. Runtime load probes, GAS settings, PIE, and the minimal cook pass. This is recorded as `BLOCKED_EXTERNAL`, and no ACFU file was modified.

## Visible PIE baseline

- Map: `/Game/FullSample/Test`.
- GameMode: `ACFUltimateGameModeBP_C`.
- PlayerController: `ACFUltimatePlayerControllerBP_C`.
- Pawn: `/Game/FullSample/Player.Player_C`.
- Visible HUD: `ANS_DefaultHUD_WB_C`.
- Visible body: `/Game/DazToUnreal/Female/Female.Female` on `SkeletalMesh`, driven by `ACF_GenericRetarget_ABP_C`.
- Compatibility mesh: Female on `CharacterMesh0`, driven by `ACF_MMHumanoid_ABP_C`, not visibly rendered.
- Stable runtime samples: 3.
- Clean PIE exit: PASS.
- Fatal/ensure count: 0/0.
- Window screenshot: 1936x1048, SHA-256 `75F28E01895B95EB875517BCA910D22555FA2ED2C267678656ACDA54CF653062`, visual review PASS.
- Viewport screenshot: 1280x720, SHA-256 `7CDCC968A6D41E15A2DA5DDB05290DEF6DE82DF4E557CEA737D0878D26132F83`, visual review PASS.

## Tooling status

- MCP transport and authentication: PASS.
- Ultimate Engine Copilot tool calls: `BLOCKED_EXTERNAL` because the installed plugin requires license activation.
- Built-in Unreal Python fallback: PASS and scoped by an explicit environment guard.
- UBG coupling: zero runtime dependency. UBG source was consulted only as an editor API map.

## Verified target plugin versions

- ACFU: `4.3.5`, Engine `5.8.0`.
- DazToUnreal: `5.8.0.491`.
- Ultimate Blueprint Generator: `1.7.0`; editor-only and license-blocked for MCP actions.
