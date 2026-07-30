# NoShellForWinter

NoShellForWinter is an Unreal Engine 5.8 action RPG with survival systems, a dark fantasy tone, and retained adult content used as a secondary social mechanic. It is one continuous game with a predominantly neutral presentation: there is no separate SFW/NSFW selector and no gameplay-content opt-in.

Adult social interactions are unlocked at Charisma level 10. Intimacy remains mechanically separate from Curse, DXP, combat power, recruitment, and survival progression. `-StreamerSafe` is the presentation override for broadcasts and public capture; it suppresses adult presentation without creating a second game mode.

## Current Target

- Unreal Engine 5.8.
- ACF Ultimate 4.3.5 is installed in the target and must not be overwritten with legacy plugin files.
- Current DazToUnreal, `/Game/FullSample/Player.Player`, Frederick, Multiple, Female, and Male assets are protected migration invariants.
- The current player assignment uses Female.

## Project-Owned Plugins

The migration relies on these project-owned plugins:

- `EFCharacterCreation`
- `EFCharacterCreationDazBridge`
- `EFClothingMorph`
- `EFProcedural`
- `EFLevelFlow`
- `EFCharacterCreationACFUBridge`
- `EFProjectSystems`

## Input Contract

Preserve these gameplay/debug bindings while migrating:

- `O`: free camera
- `Period`: character creator
- `L`: debug menu
- `Comma`: full Needs & Status HUD
- `N`: custom walk
- `C`: crawl
- `Y`: actions, emotes, interactions
- `J`: Chronicle
- `H`: conditional status debug
- `T`, `Plus`, `Minus`: exact source interactions

## Systems

- `Inner Doctrine`: seven combat, survival, exploration, and social attributes using DXP.
- `Curse`: supernatural pressure with resistances, decay, the temporary `Cursed` state, and recovery penalties.
- `Guard Recovery`: a bounded defensive absorption mechanic with no absolute invulnerability.
- `Chronicle`: narrative records and enemy dialogue. Runtime barks use neutral substitutions 90 percent of the time and the protected original Chronicle payload 10 percent of the time.
- Social presentation: Intimacy and Fap are secondary interactions unlocked at Charisma level 10. Intimacy requires verified adult characters, consent, life, consciousness, non-hostility, no active combat, and an allowed location.
- Defeat presentation: a real minigame loss makes one authoritative 10-percent roll for the retained mature defeat vignette; the other 90 percent respawns directly without an animation. This outcome is independent of Charisma and Intimacy consent.
- Streamer Safe: `-StreamerSafe` suppresses all adult presentation while leaving the underlying single-game content intact.

## UE 5.8 Evidence

Migration evidence lives under `Docs/Migration/`. The current closeout state is documented in:

- `Docs/Migration/08_QA_Matrix.md`
- `Docs/Migration/09_Known_Issues.md`
- `Docs/Migration/10_Final_Closeout.md`
- `Docs/Migration/Evidence/`

Unverified gates must stay marked as `PENDING`; scope exclusions do not become technical `PASS` results.

## Local Editor Context

The project is configured for the native UE 5.8 MCP server at `http://127.0.0.1:8000/mcp`. When Unreal Editor is open with this project loaded, validate the connection with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .agents\skills\noshellforwinter-unreal-mcp\scripts\Test-UnrealMcp.ps1
```

If the endpoint is unavailable, treat live-editor conclusions as `PENDING` and fall back only to filesystem evidence.

## Repository Hygiene

- Treat the legacy private build as read-only reference material; it is not part of this public tree.
- Do not directly modify Marketplace or Engine plugins.
- Do not bulk-copy `Content`, `Config`, `Plugins`, `Saved`, `Intermediate`, or `Binaries`.
- Do not overwrite `/Game/FullSample` or `Player` wholesale.
- Keep generated folders such as `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`, and Python caches out of Git.
- Binary Unreal assets are tracked with Git LFS according to `.gitattributes`.
