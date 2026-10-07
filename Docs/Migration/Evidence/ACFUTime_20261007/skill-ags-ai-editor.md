---
name: acfu-ags-ai-editor-workbench
description: Author and inspect ACFU AGS actions/conditions, ADS dialogue graphs, and the installed dialogue, voice and facial-animation editor generators in NoShellForWinter. Use for those ACFU tools, not generic Blueprint generation.
---

# ACFU AGS and AI editor workbench

Start with `noshellforwinter-unreal-mcp` for the target project. Installed ACFU was 4.4.2 for UE 5.8 on 2026-10-07; re-read its descriptor before assuming APIs. Its source root is `D:/Unreal Engine 5/Library/UE_5.8/Engine/Plugins/Marketplace/ACFUAsce5ab7c1439afbV5/Source`. This vendor source is read-only. Put authored assets under `/Game/_Game` and adapters in project plugins.

The same plugin includes 40 vendor skills in `Resources/Skills`. Consult the relevant `ags-graph-runtime`, `configure-dialogue`, `elevenlabs-tts-integration` or `ai-framework` instructions as reference, then verify symbols against installed source. Some vendor instructions name `ue_dialogue_create_graph` / `ue_dialogue_validate_spec`; these were not exposed by the native Unreal MCP server in this audit. Discover the current tools before promising these operations. Do not install all vendor skills or replace project rules automatically.

## AGS actions and conditions

Read `AGSGraphRuntime/Public/AGSAction.h`, `AGSCondition.h` and the concrete node header. `UAGSAction::Execute` establishes context then calls the BlueprintNativeEvent `ExecuteAction(APlayerController*, UAGSGraphNode*)`. Conditions override `VerifyCondition(APlayerController*) const`; native `VerifyForNode` establishes node context. Preserve `GetGraphOwnerActor()` semantics: NPC/graph owner differs from player controller.

The [official AGS documentation](https://slimwiki.com/dark-tower-int/acfu/new-v4-ags-actions-and-conditions-gzusmpwb4y-kwpxy0jccg70) explains reusable inline actions and conditions. Installed source takes precedence for symbols:

- AND/OR classes are `UABGANDCondition` / `UABGORCondition`, despite different names in the documentation.
- `UADSGraphNode` owns `ActivationActions` and `ActivationConditions`; `UADSGraphEdge` also has activation conditions. These arrays are not present on every base `UAGSGraphNode` in 4.4.2.
- Quest nodes have their own activation/completion arrays. Inspect their concrete declarations and call sites before editing.
- Use instanced subobjects owned by the actual node. Preserve graph edge topology and ownership when duplicating; do not share mutable runtime action instances between NPCs.

Discover the live object/data-asset/Blueprint MCP schemas; use them for reflection and narrow edits. Test one passing and one failing condition through the actual graph execution path and verify action count/owner. Compile affected Blueprints in memory before saving only authorized project assets.

## ADS editor generators

Read `AscentDialogueSystemEditor/Public/ADSEditorSubsystem.h` and `Private/ADSGraphNodeDetailCustomization.cpp`. These editor C++ functions are **not automatically Python/MCP UFunctions**. Prefer the existing native UI buttons; if automation is needed, add a narrow editor-only project bridge, never a Marketplace patch or runtime dependency on editor modules.

- `GenerateDialogueWithAI(UADSDialogue*)`: uses dialogue description, participant tags, voice-config personalities and AI player-response mode; previews screenplay text before graph import. Check the preview and participant identities before import.
- `GenerateTTSAudioForNode`, `GenerateTTSAudioForAllNodes`, `GenerateTTSForDialogues`: remote speech generation; existing sound assignments are skipped unless force regeneration is selected.
- `GenerateAnimationsForAllNodes`, `GenerateFacialAnimationForDialogues`: sequential audio-driven facial animation; require SoundWave assets and the installed MetaHuman build capability.
- `GenerateARKitAnimationForNode`: remaps an existing facial sequence to ARKit curves. DAZ/Female compatibility needs actual skeleton/curve inspection; MetaHuman generation alone does not establish it.

Inspect settings presence without printing credentials. A request to inspect tools or create this skill does not itself request billable remote generation. When generation is requested, identify the concrete dialogue/nodes, provider and output paths, then perform the authorized work with one small fixture first. Do not batch-regenerate unrelated voices.

Bound discovery to relevant headers/toolsets and a small asset set. Record generated paths, graph compilation, one real playback sample, failures and elapsed time. Mark unavailable providers or untested facial rigs PENDING rather than claiming validation from source alone.

For the time audit and known source/version differences, see `D:/Projects UE5/NoShellForWinter/Docs/Migration/ACFUTime_20261007.md`.
