---
name: acfu-installation-audit
description: Verify the installed ACF Ultimate version using Epic Launcher manifests, the effective Unreal plugin descriptor and binary BuildIds. Use for conflicting version claims, update investigations or ACFU compatibility checks; this audit does not install updates.
---

# ACFU installation audit

Use `noshellforwinter-unreal-mcp` first when making claims about the loaded project.
Use `skill-creator` when extending this workflow. Installed Marketplace plugins and
the source game remain read-only.

Run the bundled read-only script with a 30-second outer deadline:

```powershell
python .agents/skills/acfu-installation-audit/scripts/audit_installation.py --project . --engine "D:/Unreal Engine 5/Library/UE_5.8"
```

It writes selected public installation metadata to
`Saved/InstallationAudit/acfu.json`. Never dump Epic authentication or account files.
Report separately:

- Plugin `VersionName`/`Version`: semantic release, authoritative for the files installed.
- Epic plugin manifest `AppVersionString` and descriptor `EngineVersion`: distribution
  build/engine compatibility, not the ACFU semantic version.
- Engine `Build.version` and Launcher engine manifest: installed engine release.
- Effective project plugin override, if present; report duplicates as ambiguous.
- Plugin and engine `UnrealEditor.modules` BuildId: matching metadata is a binary
  compatibility check, not a substitute for successful editor loading or gameplay.

Do not infer an update from contradictory notes. Compare an existing protected hash
baseline or Git snapshot. A fresh audit proves what is installed now, not when it
changed or whether a newer release is available.

For Epic UI inspection, use the available computer-use skill and inspect Unreal
Engine > Library and the selected engine's installed plugins. Identify the exact
engine slot before interpreting update controls. If desktop access is unavailable,
state that GUI/update availability is PENDING; local manifests remain valid evidence.
Do not describe filesystem inspection as having viewed the Launcher.

Update investigation is read-only until the user authorizes an update. Before an
authorized update, preserve project/owned Blueprint changes and a plugin hash
baseline, close the target editor and use Epic's supported installation mechanism.
Never copy an older game's plugin over the target or edit Marketplace source to fix
integration logic. Re-audit after installation, then run the affected bounded build,
Blueprint, PIE and packaged gates. Refresh stale version notes with dated evidence.

Official references: [Epic installation guide](https://dev.epicgames.com/documentation/unreal-engine/install-unreal-engine)
and [working with plugins](https://dev.epicgames.com/documentation/unreal-engine/working-with-plugins-in-unreal-engine).
