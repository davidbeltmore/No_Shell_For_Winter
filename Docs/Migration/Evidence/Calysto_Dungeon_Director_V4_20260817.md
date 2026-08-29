# Dungeon Director V4 — implementación y evidencia

## Estado

`IN_PROGRESS`. V3 permanece como autoridad runtime hasta que V4 supere todos los gates de cutover.

Esta evidencia nueva no modifica ni reemplaza los receipts históricos Phase 1, Phase 2, Spawner Expansion, Tattoo/Calysto Repair o Dungeon Director V3. Los supersede únicamente cuando el cutover V4 quede validado.

## Baseline recuperable

- Workspace: `D:/Projects UE5/NoShellForWinter`.
- Git HEAD inicial: `3ccce4e96457e0fd7980d7606d6823cce1e62eeb`.
- Editor UE 5.8 conectado por `unreal-mcp`; PIE no estaba activo.
- Los seis paquetes Calysto consultados estaban `dirty=false` en el editor.
- Baseline protegido: `Saved/Migration/CalystoDungeonDirectorV4/Baseline/CalystoProtected_20260817_190214.json`.
- SHA-256 inicial de `BP_MassiveDungeon`: `47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B`.
- Política V3 respaldada byte-exact en `Saved/Migration/CalystoDungeonDirectorV4/RetiredPolicyAssets/DA_CalystoDungeonDirectorPolicy_V3.uasset`.
- SHA-256 V3 origen/respaldo: `9824B1EFC3EF8B24D5C33DDF0813B0EC999B3C9F5331BDB8E48D771120868D3A`.

El worktree inicial ya contenía cambios no relacionados y assets protegidos modificados en disco. Se preservan como estado del usuario; V4 no debe restaurarlos, resalvarlos ni atribuirlos a esta fase.

## Reglas de protección

- Nunca editar o guardar `BP_MassiveDungeon`, PCG graphs o DataAssets bajo `/Game/Calysto`.
- Nunca modificar plugins Marketplace/Engine, ACFU o DazToUnreal.
- Toda integración se implementa en `EFProcedural`, `EFProjectSystems` o contenido `/Game/_Game` project-owned.
- Un solo `GenerateLocal`; V4 reutiliza los transient clones, anchors, navegación, watchdog y viaje existentes.
- V3 no se elimina ni se retira de cook/config hasta completar build, Blueprint compile, PIE, visual QA, cook y paquetes Development/Shipping.

## Gates

| Gate | Estado | Evidencia |
|---|---|---|
| Baseline Calysto y backup V3 | PASS | rutas y hashes anteriores |
| Núcleo probabilístico V4 | IN_PROGRESS | pendiente de build/automation |
| DataAsset V4 create-only | PENDING | no creado antes de compilar la clase nativa |
| Integración PCG/materialización | PENDING | no se activa antes de validar el contrato V4 |
| Companions/roster/revival | IN_PROGRESS | pendiente de build y PIE ACF |
| Native automation | PENDING | pendiente |
| PIE/visual QA | PENDING | pendiente |
| Cook/Development package | PENDING | pendiente |
| Shipping package | PENDING | pendiente |
| Cutover y retiro V3 | PENDING | prohibido mientras exista cualquier gate pendiente |

