# Performance58 — optimización natural Lot 2 (2026-07-31)

## Resultado ejecutivo

Este lote mejora de forma material el rendimiento medio del gameplay natural, pero no declara un PASS total de fluidez para cohortes grandes.

- Comparación de cohortes similares: 29 enemigos / 164 actores antes frente a 27 enemigos / 152 actores después.
- Promedio: `72.010 -> 196.523 FPS` (`+172.9%`).
- Mediana: `73.259 -> 244.559 FPS` (`+233.8%`).
- Game Thread promedio: `13.878 -> 3.441 ms` (`-75.2%`).
- GPU promedio: `2.310 -> 2.126 ms` (`-8.0%`).
- El objetivo de “50% más” se supera en promedio y mediana, con la salvedad de que una dungeon procedural no produce una población idéntica entre runs.
- El tail todavía falla con 27 enemigos naturales: `1% low=39.083 FPS` y `p99=20.184 ms`.
- Estado de fluidez estable con cohortes grandes: `BLOCKED_BY_SAFETY`. La GPU no es el límite y no se tocarán IA, animaciones, ACF, DirtyPawn ni TattooShop para fabricar un PASS.

## Restricciones preservadas

- No se modificó código ni contenido interno de Ascent Combat Framework, Calysto, DazToUnreal, DirtyPawnSystem o TattooShop.
- La única modificación de Calysto sigue siendo una opción nativa expuesta en `BP_MassiveDungeon`: `wall Light Tile Distance=10`.
- No se redujeron ticks, percepción, controladores, animaciones, movimiento o combate.
- `ProjectPerformanceBudgetSubsystem` permanece desactivado y no aplica culling, LOD forzado, URO, suspensión o presupuestos de IA.
- El perfil natural no crea enemigos del benchmark.

## Cambios de este lote

### Inicialización de nivel enemiga

La dungeon real creó 74 enemigos en un frame en el run `DungeonNaturalGameplay58_20260731T211747Z`. El sistema project-owned ejecutó 74 inicializaciones y 444 logs en el mismo frame; el tramo duró aproximadamente `3.047 s`.

Se sustituyó ese fan-out por una inicialización en dos fases:

1. El roll de nivel conserva `SetTimerForNextTick`, el orden FIFO y el consumo de RNG del contrato anterior.
2. Sincronización, scaling, target point y target info se finalizan con un máximo configurable de un enemigo por frame.

`MaxEnemyInitializationsPerFrame=1` no cambia la frecuencia de IA o animación. VisualVariation espera sólo cuando el nivel aún no fue asignado, sin consumir sus reintentos. Si Dungeon Curse sobrescribe el nivel entre preparación y finalización, el finalizador utiliza el nivel ya asignado y no vuelve a sortearlo.

Los logs por canal bajaron a `VeryVerbose`, los logs por enemigo a `Verbose`, y queda un único resumen de cohorte a nivel `Log`.

Evidencia runtime:

- Run con 11 enemigos: `scheduled=11 completed=11 finalize_attempts=11 peak_queue=11 frames=13`.
- Run con 27 enemigos: `scheduled=27 completed=27 finalize_attempts=27 peak_queue=27 frames=29`.
- Cero warnings de preparación o finalización de nivel.

### Shaders y PSO

- Se conserva `r.PSOPrecaching=1` y la precarga selectiva project-owned de sistemas Niagara mediante `PrecacheAssetPSOs()`.
- Se retiró `r.CreateShadersOnLoad=1`.
- Causa concreta: durante cook NullRHI, UE 5.8 intentaba crear un shader `SF_WorkGraphComputeNode` (`frequency=11`) y abortaba en `FShaderMapResource_InlineCode::InitRHI`.
- El run final registra `r.CreateShadersOnLoad=0`, hash de CVars `10eb5634`, y no registra compilación de shaders durante la ventana medida.

No se habilitó GC incremental: UE 5.8 todavía lo marca experimental y no se asumió seguridad para plugins binarios. Se conservan únicamente parallel GC y clustering soportados.

## Gameplay natural final

Artefacto principal:

`Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T220508Z/summary.json`

Condiciones:

- UE 5.8 Development Standalone (`UnrealEditor -game -DisablePython`).
- DX12 / SM6, 1920×1080, RTX 5070 Ti, Ryzen 7 5800X.
- Seed `42`, preset `Performance58`, hash `10eb5634`.
- 10 s warmup + 59.006 s medidos, timeout total 150 s.
- Recorrido de navegación real: 8 puntos, `5350.03 cm`, una reversa.
- 27 enemigos generados por la dungeon; cero enemigos solicitados o creados por el benchmark.
- 152 actores, 28 pawns y 22 componentes Niagara pico.

| Métrica | Objetivo | Resultado | Estado |
|---|---:|---:|---|
| Promedio | ≥60 FPS | 196.523 FPS | PASS |
| Mediana | ≥60 FPS | 244.559 FPS | PASS |
| 1% low | ≥55 FPS | 39.083 FPS | FAIL |
| p99 | ≤18.2 ms | 20.184 ms | FAIL |
| Frames >100 ms | 0 | 0 | PASS |
| Hitches >50 ms | informativo | 1 (56.476 ms) | WARN |
| Game Thread promedio | informativo | 3.441 ms | PASS |
| GPU promedio | informativo | 2.126 ms | PASS |

Validación de seguridad del escenario:

- `scenario_pass=true`.
- `benchmark_spawned_enemy_count_peak=0`.
- `runtime_enemy_without_controller_count_peak=0`.
- `hidden_runtime_enemy_count_peak=0`.
- `enemy_mesh_tick_disabled_count_peak=0`.
- `enemy_mesh_forced_lod_count_peak=0`.
- `enemy_mesh_update_rate_optimization_count_peak=0`.
- `async_loading_sample_count=0` durante la medición.
- Captura visual: `visual_check.png` dentro del directorio del run.

El único hitch >50 ms ocurrió con 27 enemigos activos: frame `2611`, `56.476 ms`, Game Thread `20.710 ms`, Render Thread `14.156 ms`, GPU `2.618 ms`, sin async loading. La ventana del log contiene actividad de `UACFStatisticExecutionCalculation` y un cambio de captura/cursor. No se modifica ACF para ocultarlo.

## Escalado observado por población natural

| Run | Enemigos | Actores | Promedio | 1% low | p99 | >100 ms |
|---|---:|---:|---:|---:|---:|---:|
| Baseline `T191640Z` | 29 | 164 | 72.010 | 48.657 | 18.246 ms | 0 |
| Optimizado `T214005Z` | 11 | 90 | 201.082 | 60.532 | 11.737 ms | 0 |
| Final `T220508Z` | 27 | 152 | 196.523 | 39.083 | 20.184 ms | 0 |
| Stress natural `T211747Z` | 74 | 283 | 181.266 | 23.839 | 33.601 ms | 1 |

El run de 11 enemigos cumple todos los umbrales duros. Los runs de 27 y 74 muestran que el tail escala con la carga CPU de gameplay, no con GPU. Reducir más sombras, Lumen, ray tracing o resolución interna no resolverá ese límite.

## Shipping

Primer intento:

- Build Shipping: PASS.
- Cook: FAIL después de identificar `r.CreateShadersOnLoad=1` y `SF_WorkGraphComputeNode` bajo NullRHI.

Único reintento después de corregir la causa:

- Build: PASS (up to date).
- Cook: PASS.
- Stage: PASS.
- Pak + IoStore: PASS.
- Package + archive: PASS.
- Tiempo total: `163.79 s`.
- Salida: `Saved/Automation/Performance58/PackagedShippingLot3Retry_20260731/Archive/Windows/NoShellForWinter.exe`.
- Archive: 27 archivos, aproximadamente `2.421 GiB`.

El launcher Shipping arrancó y creó configuración/save runtime bajo `%LOCALAPPDATA%/NoShellForWinter/Saved`. La suite Performance58 está deliberadamente compilada fuera con `UE_BUILD_SHIPPING`; por eso el intento de benchmark no generó summary ni auto-quit y alcanzó el timeout de 150 s. Estado del benchmark Shipping: `PENDING_BY_DESIGN`, no PASS. Build/cook/package/launch sí son PASS. Recorrido visual manual Shipping: `PENDING`.

La configuración `Test` no existe en esta distribución binaria de UE 5.8. El gate Test queda `BLOCKED_ENGINE_DISTRIBUTION`; no se gastaron 30 minutos en un cook que UBT no puede producir.

## Validaciones

- Editor Development build posterior al cambio: PASS, 39.3 s.
- Game Shipping build: PASS, 677.3 s en el primer intento.
- Automation `Project.RuntimePerformance`: 14/14 PASS en `Saved/Automation/Performance58/Lot3UnitTestsPostSafety`.
- Tests Python del runner: 7/7 PASS (ejecución previa del mismo lote de runner).
- Gameplay natural real: PASS de escenario y evidencia visual.
- Shipping build/cook/stage/package/archive: PASS.
- Rehash: ACFU 5043/5043 PASS; DazToUnreal plugin 213/213 PASS.
- Las 74 diferencias históricas de assets protegidos coinciden exactamente con el snapshot anterior: `PASS_NO_NEW_DELTA`.
- Evidencia: `Saved/Migration/Evidence/Performance58_OptimizationLot3_ProtectedInvariantVerification.json`.
- Blueprint compile global, contrato completo de inputs y recorrido visual manual del Shipping final: `PENDING`.

## Decisión de seguridad

No se aplican más reducciones gráficas en este lote porque la GPU tiene margen muy amplio. Para conseguir 1% low ≥55 FPS con 27–74 enemigos habría que perfilar o cambiar el trabajo de IA/estadísticas/animación/DirtyPawn, reducir población o aplicar budgets de actores. Esas opciones afectan sistemas expresamente protegidos.

Estado final para cohortes grandes: `BLOCKED_BY_SAFETY`.

