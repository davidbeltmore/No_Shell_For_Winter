# Calysto V4 - Theme Enable guard

## Estado

`PASS` para el contrato solicitado. En Calysto Dungeon Director V4, `Theme` es la unica autoridad de activacion por categoria. El `Enabled` heredado de `Style` queda oculto, normalizado e inerte para evitar dos fuentes de verdad.

El retiro global de V3 y una validacion Shipping completa permanecen `PENDING`; no forman parte de este cambio. V4 sigue siendo la autoridad configurada y validada para esta ruta.

## Diagnostico

- El HUD no causaba generacion: `GetCurrentFloor()` solo consulta el estado del subsystem.
- La configuracion observada desactivaba `Enemies` solamente en `Standard Style`.
- `Compact` y `Branching` seguian teniendo peso de seleccion combinado de `0.50`, por lo que podian generar enemigos.
- Tambien existian dos rutas que podian reanimar una categoria tras un veto: pity de Food/Chest y `WinterChance` de Enemy.

## Contrato implementado

- Cada Theme (`Default`, `Forge`, `Shrine`) contiene un unico `Enabled` visible por categoria.
- Las siete categorias generadas estan activadas por defecto en cada Theme: Enemy, NPC, Food, Chest, LooseLoot, Clothing y SpecialEvent.
- Decoration y Lighting permanecen desactivadas porque no son categorias generadas por este director.
- Desactivar una categoria en el Theme seleccionado fuerza a cero OpportunityChance, pity, WinterChance, presencia, intentos, objetivos y directivas.
- Un escaneo final fail-closed rechaza cualquier directiva de una categoria desactivada. La unica excepcion es la proyeccion de un companion persistente ya activo, identificada por `StableCompanionId`; no es una generacion nueva.
- El antiguo `Style.Category.bEnabled` se conserva solo para compatibilidad binaria/serializada: no aparece en Details, no afecta runtime, no bloquea combinaciones y no cambia el hash canonico de politica.

## Implementacion

- Autoridad Theme y veto fail-closed: `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDungeonDirectorMathV4.cpp`.
- Validacion y hash canonico sin dependencia del flag legado de Style: `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDungeonDirectorPolicyV4.cpp`.
- Contrato/documentacion del schema: `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDungeonTypesV4.h`.
- Details customization: `Plugins/EFProcedural/Source/EFProceduralEditor/Private/Calysto/EFCalystoCategoryProfileV4Customization.cpp`.
- Regresion automatizada: `NoShellForWinter.CalystoDungeon.V4.Policy.DisabledCategoryFailClosed` en `Plugins/EFProcedural/Source/EFProceduralEditor/Private/Tests/EFCalystoDungeonV4PolicyTests.cpp`.
- Politica project-owned: `/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy`.

## Evidencia

Snapshot Git de referencia: `3ccce4e96457e0fd7980d7606d6823cce1e62eeb`. El worktree ya contenia cambios no relacionados y se preservaron.

| Gate | Estado | Evidencia |
|---|---|---|
| Build Editor Development | PASS | build frio con `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; receipts Daz habilitados |
| Build Game Development | PASS | `Tools/Migration/Build-NoShellForWinterGame58.ps1`; receipts Daz habilitados |
| Native Automation V4 | PASS | `Saved/Migration/CalystoDungeonDirectorV4/NativeAutomation_EnabledGuardFinalFull_20260821_235700/StrictSummary.json`: 23/23 strict, 0 fallos |
| Stress | PASS | incluido en la suite final: 100.000 intents/manifests y floors 1-1000 |
| PIE Zero | PASS | `Saved/Migration/CalystoDungeonDirectorV4/ExtremePIE_EnabledGuardZero_20260821_232300/StrictSummary.json`: cero actores en todas las categorias |
| PIE EnemyCap25 | PASS | `Saved/Migration/CalystoDungeonDirectorV4/ExtremePIE_EnabledGuardEnemyCap25_20260821_232500/StrictSummary.json`: exactamente 25 enemigos, resto cero |
| UI/Details | PASS | `Saved/Migration/CalystoV4EnabledGuard/20260821_223126/EditorUI_StyleExpanded.png`: Style ya no presenta `Enabled`; inspeccion MCP confirma flags Theme |
| Cook + package Development | PASS | `Saved/Migration/CalystoDungeonDirectorV4/PackageEnabledGuard_20260821_232700/UATNative.log`: 6.720 paquetes, 0 errores de cook |
| Contrato de paquete PreCutover | PASS | `Saved/Migration/CalystoDungeonDirectorV4/PackageEnabledGuard_20260821_232700/PackageValidationPreCutover.json` |
| Runtime empaquetado Zero | PASS | `Saved/Migration/CalystoDungeonDirectorV4/PackagedRuns/EnabledGuardZero_20260821_234200/RunnerReceipt.json`: exit 0 y cero actores |
| Assets protegidos | PASS | `Saved/Migration/CalystoV4EnabledGuard/20260821_223126/ProtectedFinal.json`: 13/13 hashes exactos, sin mismatch |
| Shipping / retiro total V3 | PENDING | existen simbolos V3 dormidos; no son autoridad runtime y retirarlos requiere una fase separada |

Las tres pruebas `SucceededWithWarnings` de la suite final corresponden exclusivamente al timeout ambiental de `https://www.google.com/generate_204`; no hubo errores funcionales ni findings bloqueantes.

## Integridad

- Politica V4 final: SHA-256 `95601D7B7BB6B20B495936CB5211427155A253A2CE1790FCDC6EF176FBCEC244`, 209775 bytes.
- `BP_MassiveDungeon` final: SHA-256 `8A293CBB542F3BCF2AEE27D7D4D0D73C0651B39075133627C80534BCF8DCF0F2`, 407681 bytes, identico al baseline de esta fase.
- No se modificaron assets bajo `/Game/Calysto`, ACFU, DazToUnreal ni plugins Marketplace/Engine.
