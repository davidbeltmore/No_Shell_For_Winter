---
name: noshellforwinter-unreal-mcp
description: Use the native Unreal Engine 5.8 MCP server to ground NoShellForWinter work in live editor context. Use for Unreal code, config, assets, Blueprints, maps, actors, materials, data assets/tables, skeletal or static meshes, PIE, runtime state, editor automation, validation, or any claim that depends on loaded UE state rather than files alone.
---

# NoShellForWinter Unreal MCP

Use the project-scoped `unreal-mcp` Streamable HTTP server at `http://127.0.0.1:8000/mcp`. The server exists only while the UE 5.8 Editor is running with this project loaded.

## Context preflight

1. Confirm the workspace is `D:\Projects UE5\NoShellForWinter`, never the read-only source project.
2. Prefer the MCP dependency named `unreal-mcp`. If MCP tools are not exposed in the current session, run `scripts/Test-UnrealMcp.ps1` to distinguish an unopened Editor from a configuration failure.
3. Start read-only: list available MCP tools, call `list_toolsets`, and describe only the toolset relevant to the task.
4. Inspect the smallest useful live slice. Typical toolsets include asset, actor, blueprint, scene, object, data asset/table, material, skeletal mesh, static mesh, texture, and programmatic editor operations.
5. Correlate live results with project-owned files and exact `/Game/...` paths. Treat filesystem evidence and loaded-editor evidence as separate gates.

## Tool-search contract

- UE tool search is enabled. Expect the native MCP surface to expose `list_toolsets`, `describe_toolset`, and `call_tool` rather than registering every editor operation as a top-level tool.
- Call `list_toolsets` once per fresh editor connection.
- Call `describe_toolset` only for the relevant toolset before using `call_tool`.
- Re-discover after an Editor restart because MCP sessions and loaded assets are ephemeral.

## Safety and evidence

- Do not use MCP to modify Marketplace or Engine plugins, ACFU, DazToUnreal, or the source project.
- Do not save, compile, run PIE, import, delete, rename, or move assets during a context preflight.
- Require task authorization before any mutating MCP call; then validate Blueprint compile/load errors and preserve public asset paths.
- Record the server URL, toolset/tool, object or asset path, result, and relevant log or screenshot in migration evidence when a formal gate depends on MCP.
- Mark the live gate `PENDING` when the Editor or server is unavailable. A filesystem fallback is not equivalent to live validation.

## Connection recovery

- Confirm `ModelContextProtocol`, `EditorToolset`, and `MCPClientToolset` remain enabled in `NoShellForWinter.uproject`.
- Confirm `Config/DefaultEditorPerProjectUserSettings.ini` keeps `bAutoStartServer=True`, port `8000`, path `/mcp`, and tool search enabled.
- Confirm both `.codex/config.toml` and `.vscode/mcp.json` target `http://127.0.0.1:8000/mcp`.
- Restart the Unreal Editor after plugin or MCP settings changes. Restart Codex/VS Code after client configuration or skill metadata changes.
- Check `Saved/Logs/NoShellForWinter.log` for `LogModelContextProtocol` when the TCP endpoint does not open.

Run the connection probe with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .agents\skills\noshellforwinter-unreal-mcp\scripts\Test-UnrealMcp.ps1
```
