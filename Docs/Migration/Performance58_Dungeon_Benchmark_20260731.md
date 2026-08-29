# Performance58: benchmark UE 5.8 y dungeon a 60 FPS

Fecha: 2026-07-31  
Target: `D:\Projects UE5\NoShellForWinter`  
Fuente UE 5.7: no modificada ni abierta por este trabajo.

## Resultado

La suite propia `Performance58` está implementada y compila en UE 5.8. El
escenario smoke final completa el recorrido NavMesh determinista, ocho enemigos
ACF, combate mediante inputs reales, HUD y DirtyPawn sin activar presupuestos de
gameplay.

El objetivo de 60 FPS queda `BLOCKED_BY_SAFETY`, no `PASS`. El smoke final
midió 30,40 FPS de promedio, 33,17 de mediana, 2,99 FPS de 1% low, p99 de
58,03 ms y tres hitches superiores a 100 ms. El cuello observado es Render
Thread (30,14 ms promedio); GPU fue 7,90 ms. Durante el combate también ocurrió
un `ensure` dentro de `UACFAnimInstance::UpdateStateData` y 46 intentos del mage
de reproducir un montage `None`. No se modificó ACFU, IA, animación ni ticks de
DirtyPawn para ocultar estos fallos.

Evidencia principal:

- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/summary.json`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/samples.csv`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/events.jsonl`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/system_metrics.csv`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/visual_check.png`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T083808Z/runtime.log`

## Reparación focalizada del montage de Mage

Autorizada posteriormente por el usuario y aplicada sin tocar el plugin ACFU.
La causa era un único valor faltante: la instancia
`ACFAreaDamageSpellBP_C_0` de `AS_ACF_Mage` tenía `AnimMontage=None`. Las
instancias equivalentes de `AS_ACF_Player` y `AS_ACF_Simple_Player` ya usaban
`/Game/FullSample/Animations/MageAnims/AreaBoost_AM`.

Se asignó ese mismo montage a la instancia de Mage mediante el commandlet
guardado `Tools/Migration/Repair-MageAreaDamageMontage58.py`. No se cambió el
Blueprint de la habilidad Marketplace, el AnimBP, las clases Male/Female, IA,
ticks, movimiento ni la configuración de coste/daño del hechizo.

Validación:

- `PASS`: reapertura en un proceso UE 5.8 independiente confirmó exactamente
  una instancia y el montage serializado `AreaBoost_AM`.
- `PASS`: tests Python 3/3 y automatización
  `Project.RuntimePerformance` 13/13.
- `PASS`: builds incrementales Game y Editor Development posteriores a la
  reparación (`Result: Succeeded`).
- Smoke post-reparación:
  `Saved/Automation/Performance58/DungeonSmoke58_20260731T090153Z`.
  Cargó ambos Mage, produjo 35 eventos reales de daño, cero errores
  Blueprint/fatal y cero mensajes `failed to play montage None` (antes 46).
- El smoke completo no es `PASS`: terminó con 7/8 enemigos activos y continúa
  bajo el gate de FPS. El `ensure` de `UACFAnimInstance::UpdateStateData`
  también persiste y queda separado de esta reparación.
- Evidencia de cambio:
  `Saved/Migration/Performance58_MageMontageRepair.json`.
- Rehash posterior:
  `Saved/Migration/Evidence/Performance58_MageMontage_ProtectedInvariantVerification.json`.
  ACFU 5.043/5.043 y DazToUnreal 213/213 continúan `PASS`; los 74 deltas
  históricos de Target Daz son idénticos a la evidencia pre-reparación
  (cero añadidos y cero eliminados).

## Implementación

- Perfiles nuevos: `DungeonSmoke58`, `DungeonAcceptance58` y
  `DungeonFullStackDiagnostic58`.
- Los IDs heredados `DungeonCombatStable`, `DungeonGameplayReal` y
  `DungeonFullStackOverload` se normalizan hacia los perfiles 5.8.
- Request Blueprint con perfil, duración, seed, ocho enemigos, preset, commit y
  salida automática.
- Seed de aceptación `42`.
- Escenario proporcional 30/60/30 en aceptación y 7,5/15/7,5 en smoke:
  `TraversalStreaming`, `CombatEight`, `CombatDirtyPawnHud`.
- Ruta real resuelta mediante Navigation System. No existe fallback sinusoidal
  en los perfiles 5.8.
- Mezcla exacta: tres melee, tres ranged y dos mage; cuatro Male y cuatro
  Female.
- Ataque y esquiva se envían por el `PlayerController` usando
  `FInputKeyEventArgs::CreateSimulated`, de modo que atraviesan el mapping
  Enhanced Input/ACF real. No se inyecta daño.
- La telemetría escucha tanto el componente project-owned como
  `UACFDamageHandlerComponent::OnDamageReceived`.
- El gate comprueba controladores, AnimInstance/mesh tick, visibilidad,
  colisión, daño ACF, desplazamiento, HUD y carga DirtyPawn.
- `ProjectPerformanceBudgetSubsystem` y todos sus reductores de tick,
  animación, movimiento, culling y VFX permanecen deshabilitados.
- Presets persistentes `Balanced58` y `Performance58` en configuración del
  proyecto. `Balanced58` conserva Texture 3 y aplica resolución dinámica
  80–100% con objetivo de 16,67 ms.
- VSync queda configurable para producto, pero se fuerza a cero sólo durante
  la medición. No se usa `t.MaxFPS` ni frame cap.
- Cada artefacto registra CPU, GPU, driver, RAM, resolución, RHI, build, commit,
  seed, preset, snapshot de CVars y hash.

## Runner y límites

`Tools/Performance/perf58.py` ofrece:

- `smoke`: máximo 30 s medidos y timeout 120 s.
- `run`: una única ejecución smoke, acceptance o diagnostic.
- `gate`: exactamente dos summaries o exactamente dos ejecuciones empaquetadas;
  ambas deben aprobar.
- `compare`: exige el mismo hash de CVars y rechaza un empaquetado más de 5%
  peor que Standalone.

El runner rechaza metadatos incompletos, preset alterado, resolución/RHI
incorrectos, escenario incompleto, cualquier segmento bajo el gate, enemigos
suprimidos, budgeting activo, trabajo sintético, texture-pool over-budget,
errores runtime y compilación de shaders durante la medición. Una traza se
limita a 45 s.

## Gates ejecutados

| Gate | Estado | Evidencia |
|---|---|---|
| Preflight Unreal MCP, máximo 5 min | `PENDING` | Endpoint `http://127.0.0.1:8000/mcp` no disponible; Editor inicialmente cerrado |
| Tests Python del runner | `PASS` | 3/3, 0,077 s |
| Tests C++ `Project.RuntimePerformance` | `PASS` | 13/13; `Saved/Logs/Performance58Automation.log` |
| Build Editor Development | `PASS` | UBT `Result: Succeeded` |
| Build Game Development | `PASS` | UBT `Result: Succeeded` |
| Build Test | `PENDING` | La distribución instalada responde `Targets cannot be built in the Test configuration with this engine distribution` |
| Smoke Standalone DX12/SM6, 1920×1080 | `BLOCKED_BY_SAFETY` | Escenario completo; métricas bajo gate y errores ACF runtime |
| Dos gates empaquetados Test | `PENDING` | No se ejecutan después de un smoke fallido |
| Traza culpable de 45 s | `PENDING` | No se gasta mientras el error ACF/Render Thread ya sea reproducible |
| Blueprint compile global | `PENDING` | No disponible mediante MCP en esta sesión |
| PIE interactivo/visual adicional | `PENDING` | MCP/Editor vivo no disponible |
| Cook/package Test | `PENDING` | Test no soportado por esta distribución y smoke no aceptado |
| Shipping build/cook/package/runtime | `PENDING` | No se promueve antes de aprobar Test/acceptance |
| Contrato completo de inputs | `PENDING` | El smoke ejercitó ataque/esquiva; el contrato global O/Period/L/Comma/N/C/Y/J/H/T/Plus/Minus no se ejecutó |

## Invariantes protegidos

Rehash: `Saved/Migration/Evidence/Performance58_ProtectedInvariantVerification.json`.

- ACFU 4.3.5: `PASS`, 5.043 archivos, cero mismatches.
- DazToUnreal 5.8.0.491: `PASS`, 213 archivos, cero mismatches.
- Female:
  `425EAB06C1DEE4BFDAEA7890D419FE60EA2443F472004168EC5AC4C98F3B9CFC`.
- Multiple:
  `3FC4E53877C2EA61A766761E12095FA5F9AF48993A2ECE6CE131CFCCA6BF9583`.
- Male:
  `E4C507062363E81F6EBF00053741FB5F6A66F3650CCBF2DBC8530DEA175EA8F6`.
- Player:
  `E7EDE80A927A34004014F497141097AC64597AD826C9EE85B0077EE7EA33891D`;
  conserva exactamente el hash documentado antes de esta tarea, aunque sigue
  `FAIL` contra el baseline Phase 0.
- Frederick: `PENDING_RUNTIME_IDENTITY`; no existe un package resoluble con ese
  nombre, igual que en la evidencia anterior.

No se modificó ningún asset o plugin protegido.

## Próximo lote seguro

Antes de intentar otro preset o gate deben resolverse, sin editar Marketplace:

1. identificar desde contenido project-owned por qué el Render Thread de la
   dungeon consume ~30 ms (draw calls/PCG instancing/occlusion/HLOD);
2. reproducir y escalar el `ensure` de `UACFAnimInstance` sin cambiar su
   frecuencia de actualización;
3. estabilizar la permanencia de ocho enemigos durante el escenario sin
   suprimir daño, IA, animación ni combate real;
4. sólo entonces ejecutar un smoke nuevo, seguido de exactamente dos gates
   empaquetados si el smoke alcanza el objetivo.
