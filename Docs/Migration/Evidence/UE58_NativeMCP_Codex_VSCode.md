# UE 5.8 Native MCP - Codex and VS Code

Date: 2026-07-14

## Scope

Project-scoped connection of the UE 5.8 native `ModelContextProtocol` server to Codex and Visual Studio Code. This is a tooling/configuration gate only; it is not a gameplay migration, cook, or packaged-build validation.

## Configuration

- Target project: `NoShellForWinter.uproject`
- Engine: `D:\Unreal Engine 5\Library\UE_5.8`
- Native plugins enabled in target `.uproject`: `ModelContextProtocol`, `MCPClientToolset`, `EditorToolset`
- Server defaults: `Config/DefaultEditorPerProjectUserSettings.ini`
- Codex client: `.codex/config.toml`
- VS Code client: `.vscode/mcp.json`
- Default Codex workflow: `.agents/skills/noshellforwinter-unreal-mcp/SKILL.md`
- Durable activation rule: `AGENTS.md`
- Endpoint: `http://127.0.0.1:8000/mcp`

No Marketplace or Engine plugin was modified. The read-only source project was not accessed or changed.

## Evidence

| Gate | Result | Evidence |
|---|---|---|
| Unreal Editor target loaded | PASS | Process title `NoShellForWinter - Unreal Editor`; executable `UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe` |
| Native MCP autostart | PASS | `Saved/Logs/NoShellForWinter.log`: `Starting MCP server on port 8000` without a command-line MCP start flag |
| Protocol negotiation | PASS | Probe negotiated `2025-11-25` and received `Mcp-Session-Id` |
| Native tool discovery | PASS | `tools/list` returned `list_toolsets`, `describe_toolset`, and `call_tool` |
| Live editor context | PASS | `list_toolsets` returned EditorApp, Logs, Actor, Asset, Blueprint, DataAsset, DataTable, Material, Object, Scene, SkeletalMesh, StaticMesh, Texture, Programmatic, and related toolsets |
| Codex registration | PASS | `codex mcp get unreal-mcp` reports enabled Streamable HTTP endpoint at `127.0.0.1:8000/mcp` |
| VS Code registration | PASS | `.vscode/mcp.json` parses as JSON and uses Epic's native VS Code MCP schema (`servers`, `type=http`, `url`) |
| Repo skill validation | PASS | `quick_validate.py .agents/skills/noshellforwinter-unreal-mcp` |

## Reproduction

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .agents\skills\noshellforwinter-unreal-mcp\scripts\Test-UnrealMcp.ps1
cmd /c codex.cmd mcp get unreal-mcp
```

The Unreal Editor must be open for the HTTP probe to pass. After an Editor restart, MCP clients establish a new session automatically.

## Out-of-scope validation

- Blueprint compile: N/A; no Blueprint or asset mutation was performed.
- PIE and visual QA: N/A; the connection probe is read-only.
- Cook and packaged build: N/A; the MCP plugin is editor tooling for this workflow and no shipping claim is made.
