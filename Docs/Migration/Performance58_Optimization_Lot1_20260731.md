# Performance58: lote de optimización gráfica y de hitches

Fecha: 2026-07-31  
Proyecto: `D:\Projects UE5\NoShellForWinter`  
Plataforma de medición: Ryzen 7 5800X, RTX 5070 Ti, 1920x1080, DX12/SM6, UE 5.8

## Estado ejecutivo

El primer lote mejoró de forma material el rendimiento real de la dungeon:

- promedio: 61,02 -> 85,28 FPS;
- mediana: 64,78 -> 80,23 FPS;
- 1% low: 6,86 -> 21,12 FPS;
- p99: 33,03 -> 21,82 ms;
- hitches mayores de 100 ms: 2 -> 1;
- Render Thread promedio: 14,49 -> 4,95 ms;
- GPU promedio: 4,62 -> 2,42 ms.

La carga sostenida ya supera 60 FPS de promedio y mediana en los tres
segmentos. El gate estricto sigue `BLOCKED_BY_SAFETY`, no `PASS`: queda un
hitch de 374,93 ms en Game Thread al construir e inicializar simultáneamente
los ocho enemigos, además de que el 1% low y p99 aún no cumplen el contrato.
No se alteró IA, animación, percepción, movimiento, combate ni ticks para
fabricar un resultado favorable.

## Alcance y exclusiones

Este lote no modificó:

- Ascent Combat Framework Ultimate ni sus plugins;
- Calysto, sus plugins o sus Blueprints;
- DirtyPawnSystem;
- TattooShop;
- Player, Female, Frederick, Multiple o Male;
- frecuencia o comportamiento de IA, animaciones, combate o DirtyPawn.

La reparación anterior del montage de Mage es un cambio project-owned,
separado y autorizado expresamente. No se editó ACFU para realizarla.

## Cambios aplicados

### Preset de producto `Performance58`

`Performance58` quedó como preset inicial del producto, no sólo del Editor:

- resolución base 85%, resolución dinámica 80-100% y presupuesto de 16,6667 ms;
- Texture 3, AA/TSR 2, View 1, Shadow 0, GI 1, Reflection 1;
- PostProcess 1, Effects 1, Foliage 1, Shading 1 y Landscape 1;
- Lumen GI/reflections, ray tracing, Virtual Shadow Maps, cascadas y
  volumetric fog desactivados;
- Nanite e instance culling conservados;
- VSync continúa siendo configurable para producto, pero se desactiva durante
  las mediciones y no se usa `t.MaxFPS`.

Los cambios viven en `DefaultEngine.ini`, `DefaultGame.ini`,
`DefaultGameUserSettings.ini`, `DefaultScalability.ini` y
`DefaultPlugins.ini`, por lo que forman parte de builds cocinados.

### Streaming, shaders y precarga

- PSO precaching y creación de shaders al cargar habilitados.
- Texture pool aumentado de 1000 a 4096 MB, limitado a VRAM.
- Transferencias de texturas amortizadas, máximo de ocho texturas por frame.
- Precarga asíncrona project-owned de 40 activos de combate, incluidas las 16
  clases de enemigos utilizadas por Performance58.
- El benchmark espera a que termine su precarga dirigida antes del warmup.
- La carga inicial de clases de dungeon de `EFProcedural` pasó de bootstrap
  síncrono a `RequestAsyncLoad`, conservando la misma lógica de spawn.

`ProjectPerformanceBudgetSubsystem` sólo mantiene la precarga. Su budgeting
de gameplay continúa deshabilitado, igual que todos los mecanismos de
reducción de tick, IA, animación, movimiento, culling de enemigos y VFX.

### Experimento descartado por seguridad

Se probó el Async Loading Thread de UE. El primer smoke terminó antes de medir
con un assert de Game Thread en `MaterialInterface.cpp`, desde una ruta de
post-load de CommonUI ejecutada en `FAsyncLoadingThread`.

El experimento se revirtió inmediatamente:

- `s.AsyncLoadingThreadEnabled=False`;
- `s.AsyncPostLoadEnabled=False`.

No se parcheó ACF, CommonUI ni otro plugin para forzar compatibilidad. El
streaming en background y la precarga dirigida permanecen habilitados.

Evidencia del intento descartado:
`Saved/Automation/Performance58/Runner/DungeonSmoke58_Performance58_20260731T094024Z/runtime.log`.

## Resultados comparables

Ambas ejecuciones usan `DungeonSmoke58`, seed 42, 1920x1080 y el mismo equipo.
La ejecución final usa hash efectivo de CVars `81946d5d`.

| Métrica | Baseline | Lote 1 | Cambio |
|---|---:|---:|---:|
| Promedio FPS | 61,02 | 85,28 | +24,25 |
| Mediana FPS | 64,78 | 80,23 | +15,45 |
| 1% low FPS | 6,86 | 21,12 | +14,26 |
| p99 frame time | 33,03 ms | 21,82 ms | -11,21 ms |
| Hitches >100 ms | 2 | 1 | -1 |
| Game Thread promedio | 14,04 ms | 11,68 ms | -2,37 ms |
| Render Thread promedio | 14,49 ms | 4,95 ms | -9,55 ms |
| GPU promedio | 4,62 ms | 2,42 ms | -2,19 ms |

Baseline:
`Saved/Automation/Performance58/DungeonSmoke58_20260731T092257Z/summary.json`.

Resultado:
`Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/summary.json`.

### Métricas por segmento del lote 1

| Segmento | Promedio | Mediana | 1% low | p99 | Hitches >100 ms |
|---|---:|---:|---:|---:|---:|
| Recorrido y streaming | 142,55 FPS | 145,61 FPS | 78,18 FPS | 9,02 ms | 0 |
| Combate, 8 enemigos | 72,25 FPS | 76,34 FPS | 14,40 FPS | 23,98 ms | 1 |
| Combate + DirtyPawn + HUD | 61,80 FPS | 64,92 FPS | 23,41 FPS | 24,04 ms | 0 |

El escenario terminó correctamente con:

- ocho enemigos activos y ocho controladores;
- percepción ACF normal y ningún targeting directo del benchmark;
- cero enemigos ocultos, suspendidos, sin colisión o con LOD/tick forzado;
- 36 eventos reales de daño, 20 ataques y cinco esquivas;
- HUD activo y seis cargas representativas de DirtyPawn;
- cero errores Blueprint/runtime críticos durante el smoke;
- cero avisos de texture pool over-budget;
- cero compilaciones de shader durante la ventana medida;
- cero montages `None` de Mage en esta ejecución.

## Hitch restante y límite de seguridad

Las dos peores muestras del segmento de combate fueron:

- 91,87 ms de frame y 90,07 ms de Game Thread al comenzar el cambio de fase;
- 374,93 ms de frame y 372,27 ms de Game Thread cuando aparecen los ocho
  enemigos.

El patrón concentra el único hitch >100 ms en construcción e inicialización
simultánea de actores. No es un límite de GPU. Ocultar, suspender, simplificar
o escalonar artificialmente los enemigos sólo dentro del benchmark cambiaría
el escenario y no sería una optimización válida. Modificar la inicialización
interna de ACF también queda fuera del alcance autorizado. Por ello se detiene
este lote como `BLOCKED_BY_SAFETY`.

## Validaciones y evidencia

| Gate | Estado | Evidencia |
|---|---|---|
| Preflight Unreal MCP | `PENDING` | Editor cerrado; no hubo estado live disponible |
| Unitarias del runner | `PASS` | 3/3 |
| Build Editor Development | `PASS` | `Saved/Automation/Performance58/Build/Editor_Development_PerformanceLot1.stdout.log` |
| Build Game Development | `PASS` | `Saved/Automation/Performance58/Build/Game_Development_PerformanceLot1.stdout.log` |
| Automation `Project.RuntimePerformance` | `PASS` | 13/13, `Saved/Automation/Performance58/Automation/RuntimePerformanceLot1Retry/Report/index.json` |
| Smoke: integridad del escenario | `PASS` | `DungeonSmoke58_20260731T094238Z/summary.json` |
| Smoke: gate de rendimiento | `BLOCKED_BY_SAFETY` | 1% low 21,12; p99 21,82 ms; un hitch >100 ms |
| QA visual de captura | `PASS` limitado | Dungeon legible, sin corrupción visual |
| Compilación Blueprint live | `PENDING` | Requiere Editor/MCP |
| PIE interactivo y visual QA completo | `PENDING` | Requiere Editor/MCP |
| Cook/package Test | `PENDING` | No se promueve un smoke que falla el gate estricto |
| Shipping, runtime empaquetado y paridad | `PENDING` | Depende del gate Test |
| Contrato completo de inputs | `PENDING` | No ejecutado en este lote |

La automatización todavía registra en startup un `ensure` preexistente de
`UAUTGameUserSettings` al consultar Shadow 0. Las 13 pruebas terminan en
`PASS`, pero el ensure no se clasifica como resuelto porque pertenece al
entorno ACF excluido del lote.

Artefactos finales:

- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/summary.json`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/samples.csv`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/events.jsonl`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/system_metrics.csv`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/visual_check.png`
- `Saved/Automation/Performance58/DungeonSmoke58_20260731T094238Z/runtime.log`

## Invariantes protegidas

Rehash:
`Saved/Migration/Evidence/Performance58_OptimizationLot1_ProtectedInvariantVerification.json`.

- ACFU: 5043 archivos, `PASS`;
- plugin DazToUnreal: 213 archivos, `PASS`;
- los 74 mismatches históricos de activos Daz/autoritativos son idénticos a
  la evidencia anterior al lote;
- comparación de registros: cero deltas nuevos;
- estado de este lote: `PASS_NO_NEW_DELTA`;
- identidad runtime de Frederick: `PENDING`.

## Siguiente pase seguro

Queda pendiente un pase live, exclusivamente mediante opciones nativas de los
Blueprints de Calysto y sin modificar su código:

- desactivar `Cast Shadows` en Point Lights puramente decorativas;
- probar `Wall Light Tile Distance` de 2 a 4;
- reducir 25-40% el radio de atenuación de luces decorativas;
- aplicar los niveles nativos de scalability de Niagara.

Estas propuestas no están aplicadas. Requieren Unreal MCP y una comparación
visual antes/después; cualquier conclusión live continúa `PENDING`.
