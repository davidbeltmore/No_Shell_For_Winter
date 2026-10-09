# Installed ACFU identity — 2026-10-09

Target: `D:/Projects UE5/NoShellForWinter`; engine:
`D:/Unreal Engine 5/Library/UE_5.8`. Starting code snapshot:
`1f156079` (user HUB changes preserved before repair).

| Evidence | Measured value |
| --- | --- |
| `Engine/Build/Build.version` | Unreal 5.8.3, changelist 58210709 |
| Epic engine manifest | `5.8.3-58210709+++UE5+Release-5.8-Windows`, complete install |
| `ACFUAsce5ab7c1439afbV5/AscentCombatFramework.uplugin` | VersionName `4.4.2`, Version `442`, EngineVersion `5.8.0` |
| Epic ACFU manifest | `5.8.0-56448143+++UE5+Dev-Marketplace-Windows`, complete install, target engine `UE_5.8` |
| Engine and plugin `UnrealEditor.modules` | Both BuildId `55116800`; no missing plugin editor modules |
| Project duplicate override | None |
| Protected baseline comparison with previous NPC task | 7,643 protected files; zero differences |

The old 4.3.5 project note was stale. Epic's 5.8.0 plugin manifest value identifies
the distribution's engine/build, not ACFU's semantic release. There is no measured
plugin update between the earlier NPC task and this repair.

The reusable script in `.agents/skills/acfu-installation-audit` completed in
0.442 seconds. Full selected metadata: `Saved/InstallationAudit/acfu.json`.
Hash evidence: `Saved/NPCLogic/Baseline/before.json` and
`preexisting-plugin-drift.json`. Installed plugin files were not changed.

Desktop access was attempted through the computer-use native pipe, which was
unavailable (pipe not found). Launcher GUI inspection and newer-release availability
are **PENDING**. No update was requested or installed by this audit.

For a future update investigation, inspect the intended engine slot in Epic's
Unreal Engine Library and its installed plugins. Read the actual available controls;
do not assume an update exists from a stale document. Preserve the current files
and project changes before an authorized update. See the
[official Epic installation guide](https://dev.epicgames.com/documentation/unreal-engine/install-unreal-engine)
and [plugin documentation](https://dev.epicgames.com/documentation/unreal-engine/working-with-plugins-in-unreal-engine).
