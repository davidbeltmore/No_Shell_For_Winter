# Performance58: gameplay natural de la dungeon

Fecha: 2026-07-31  
Proyecto: `D:\Projects UE5\NoShellForWinter`  
Mapa: `/Game/Procedural/Maps/DungeonGeneration`  
Motor: Unreal Engine 5.8  
Equipo: Ryzen 7 5800X, RTX 5070 Ti, 48 GB RAM, DX12/SM6, 1920x1080

## Resultado

La prueba anterior de aceptación no representaba gameplay natural: retiraba la
población PCG y creaba ocho enemigos del benchmark. Se añadió un perfil separado,
`DungeonNaturalGameplay58`, que conserva el GameMode, Player, cámara, HUD, IA y
enemigos que la dungeon genera por sí misma.

El resultado cuantitativo no aprueba todavía el objetivo estricto de 60 FPS
estables. El promedio es superior a 60 FPS, pero el 1% low falla y el p99 queda
ligeramente por encima del límite:

| Métrica, 59 s de caminata medida | Resultado | Objetivo | Estado |
|---|---:|---:|---|
| Promedio | 72.010 FPS | >=60 FPS | PASS |
| Mediana | 73.259 FPS | >=60 FPS | PASS |
| 1% low | 48.657 FPS | >=55 FPS | FAIL |
| p99 | 18.246 ms | <=18.2 ms | FAIL por 0.046 ms |
| Hitches >50 ms | 1 | 0 | FAIL |
| Hitches >100 ms | 0 | 0 | PASS |
| Game Thread promedio | 13.878 ms | informativo | LIMITANTE |
| Render Thread promedio | 4.802 ms | informativo | margen amplio |
| GPU promedio | 2.310 ms | informativo | no limitante |

La GPU no es el cuello de botella en este preset. El Game Thread consume casi
todo el presupuesto de 16.667 ms y alcanza 16.254 ms en p95 con 29 enemigos
naturales activos. Reducir todavía más Lumen, sombras o resolución no resolvería
la causa principal observada en esta ejecución.

## Contrato del nuevo perfil

`DungeonNaturalGameplay58` cumple lo siguiente:

- no genera, destruye, oculta, suspende ni simplifica enemigos;
- no ejecuta ataques, esquivas, daño o targeting sintéticos;
- no activa `ProjectPerformanceBudgetSubsystem`;
- no cambia ticks de IA, animación, movimiento o DirtyPawn;
- no reemplaza la cámara del Player;
- no ejecuta `TryLoad()` de clases enemigas desde la telemetría;
- conserva el preload que forma parte del producto, pero omite el preload dirigido
  exclusivamente al benchmark;
- espera PCG y navegación, construye una ruta válida y mueve el Pawn mediante su
  interfaz real de movimiento;
- registra por separado `DungeonBootstrapPreReady` y `NaturalTraversal`;
- incluye frame, mapa, posición, velocidad, distancia, punto de ruta, población y
  clasificación GT/RT/GPU en las muestras.

El runner acepta:

```text
python Tools/Performance/perf58.py run --profile natural --mode standalone --preset Performance58 --duration 60 --timeout 150
```

El perfil natural no acepta `--enemies`; `gate` permanece reservado al escenario
de aceptación de ocho enemigos para no debilitar ese contrato.

## Corrida cuantitativa natural

Artefacto principal:
`Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/summary.json`.

Integridad del escenario:

- 29 enemigos naturales al inicio, pico y final;
- cero enemigos creados por el benchmark;
- 164 actores, 30 pawns, 61 SkeletalMeshComponents y 40 NiagaraComponents;
- cero enemigos sin controlador, ocultos, sin colisión o con tick/LOD forzado;
- cámara nativa del Player;
- ruta de navegación de 3 puntos;
- 28,324.844 uu recorridas y 9 inversiones de ruta;
- cero frames sintéticos;
- cero advertencias de texture-pool over-budget durante la medición;
- cero compilaciones de shaders o Niagara durante la medición.

### Hitch durante la caminata

Hubo exactamente un hitch >50 ms:

| Campo | Valor |
|---|---:|
| Frame | 3477 |
| Tiempo del perfil | 37.051 s |
| Frame | 71.428 ms |
| Game Thread | 71.026 ms |
| Render Thread | 4.698 ms |
| GPU | 2.279 ms |
| Async loading | activo |
| Enemigos | 29, sin cambio |
| Actor/Pawn/Niagara | 164 / 30 / 40, sin cambio |
| Velocidad del Player | 500 cm/s |

El timestamp coincide exactamente con:

```text
[2026.07.31-19.17.39:370][477]LogStreaming: Display: FlushAsyncLoading(737): 1 QueuedPackages, 0 AsyncPackages
```

Conclusión: fue una resolución de paquete de primer uso forzada en el Game
Thread. No fue GPU, spawn, Niagara ni un cambio de población. El nombre exacto
del paquete no aparece en el log y queda `PENDING`; afirmarlo sin la única traza
`loadtime` permitida sería especulativo.

### Carga inicial percibida

`DungeonBootstrapPreReady` conserva los frames que normalmente desaparecerían
detrás del warmup:

| Métrica bootstrap | Resultado |
|---|---:|
| Muestras | 863 |
| p99 | 51.351 ms |
| Hitches >50 ms | 9 |
| Hitches >100 ms | 7 |
| Hitches >500 ms | 4 |
| Hitches con async loading | 7 de 9 |

Causas observadas:

1. Viaje/carga del mapa: pico percibido de 11.909 s. Es una transición de carga,
   no un frame jugable normal, pero explica el bloqueo inicial que ve el jugador.
2. Paquetes y PostLoad: `ProcessLoadedPackages` tomó 111.54 ms; hubo flushes con
   73 y 36 paquetes asíncronos y esperas de static meshes.
3. Niagara de antorcha Calysto: primera compilación runtime de
   `/Game/Calysto/Dungeon/Particle/FXS_LowPolyTorch` en 175.960 ms.
4. Generación PCG y construcción concentrada de los 29 enemigos: pico medido de
   1,288.317 ms cuando ya estaban presentes 164 actores, 30 pawns y 40 Niagara.
5. La sanitización de EFProcedural puede revisar hasta 98 candidatos por pawn y
   volver a evaluar tras construir navegación. Con 29 enemigos el techo teórico
   es 5,684 evaluaciones en el burst (`98 * 29 * 2`).
6. El navmesh sólo tomó 10 ms; no es una prioridad frente a carga, Niagara o el
   burst de actores.

La telemetría project-owned de alto nivel fue despreciable: Dirty Pawn acumuló
0.039 ms y Survival Status 0.156 ms en 71 observaciones. Esto no demuestra el
costo interno total de cada plugin, pero descarta esos probes como causa del
hitch y no justifica tocar DirtyPawn.

## PIE visible con gameplay normal

Ante la solicitud de ver la prueba, se realizó una segunda validación separada.
Esta pasada no activó ningún perfil de benchmark ni su autopilot:

- Unreal Editor 5.8 abierto visiblemente;
- mapa cargado mediante MCP nativo y confirmado como `DungeonGeneration`;
- PIE normal `PlayMode_InViewPort`, no Simulate;
- spawn estándar del GameMode, sin override de transform;
- entrada física W/D sobre el viewport enfocado durante 30.569 s;
- cero teleports, cero `AddMovementInput` directo desde la herramienta y cero
  input sintético de combate;
- PIE detenido limpiamente; el Editor quedó abierto en la dungeon.

La dungeon creó 17 enemigos por su flujo PCG natural:

| Clase | Cantidad |
|---|---:|
| `ACFMageEnemyBPFemale` | 8 |
| `ACFMMEnemyBPFemale` | 1 |
| `ACFMMEnemyBPMale` | 3 |
| `ACFRangedEnemyBPFemale` | 1 |
| `ACFRangedEnemyBPMale` | 4 |

Las capturas de antes y después demuestran un desplazamiento real desde el centro
hasta una esquina ocupada por actores naturales. En la ventana exacta de
input, 19:37:45.985Z a 19:38:16.744Z, el log contiene:

- cero errores, ensures o fatals;
- cero `FlushAsyncLoading`;
- cero compilaciones de shader/Niagara;
- cero texture-pool over-budget;
- 15 warnings de ACF/ARS/SequenceEvaluator durante combate natural.

Dos segundos después del input, mientras el combate natural continuaba, apareció:

```text
[2026.07.31-19.38.19:350][884]LogStreaming: Display: FlushAsyncLoading(1170): 1 QueuedPackages, 0 AsyncPackages
[2026.07.31-19.38.19:370][884]LogTemp: Error: Invalid ExpToGiveOnDeathByCurrentLevel Curve!
```

Esto identifica otra ruta real de primer uso asociada al flujo de muerte/XP. El
frame-time y el paquete exacto de este segundo flush quedan `PENDING`; no se
tocará ACF para resolverlo.

La validación visible también reprodujo en bootstrap:

- dos flushes con 18 y 22 paquetes asíncronos;
- compilación runtime de `FXS_LowPolyTorch` en 199.085 ms;
- navegación en 10 ms.

No se usan las muestras de CPU/RAM del proceso host como métricas de aceptación;
sólo sirven como contexto, porque el Editor y MCP añaden carga ajena al juego.

## Diagnóstico y orden seguro de reparación

Prioridad propuesta, sin tocar ACF, Calysto internamente, DirtyPawn o TattooShop:

1. Capturar una sola traza visible de hasta 45 s con `loadtime` sobre el primer
   `FlushAsyncLoading`, únicamente si se necesita conocer el paquete exacto.
2. Incorporar ese paquete a la precarga project-owned del producto, no a una
   precarga exclusiva del benchmark, y validar que el hitch desaparezca.
3. Preparar `FXS_LowPolyTorch` antes de que PCG materialice las antorchas, desde
   código/configuración project-owned o una opción nativa expuesta por Calysto;
   no editar el asset/plugin Calysto.
4. Optimizar la sanitización project-owned de EFProcedural: cachear resultados
   válidos, evitar la segunda búsqueda de 98 candidatos cuando el pawn ya está
   correctamente colocado y, si es necesario, repartir sólo ese trabajo de
   saneamiento entre frames. No alterar IA, controladores o movimiento ACF.
5. Auditar el contrato project-owned que suministra la curva de XP. El error
   `Invalid ExpToGiveOnDeathByCurrentLevel Curve!` debe resolverse mediante datos
   o configuración del proyecto si existe una vía nativa; ACF permanece intacto.
6. Mantener `Performance58` como baseline visual. Con GPU a 2.31 ms, seguir
   bajando texturas, Lumen o resolución antes de corregir el Game Thread tendría
   poco retorno en esta máquina.

Si una traza demuestra que el costo sostenido pertenece a IA, animación o ACF,
el resultado será `BLOCKED_BY_SAFETY`; no se reducirá tick ni comportamiento para
fabricar un PASS.

## Validación de implementación

| Gate | Resultado | Evidencia |
|---|---|---|
| MCP live preflight | PASS | endpoint `http://127.0.0.1:8000/mcp`, protocolo 2025-11-25 |
| Tests Python runner | PASS, 6/6 | `Tools/Performance/tests/test_perf58.py` |
| `py_compile` runner | PASS | `Tools/Performance/perf58.py` |
| Build Editor Development | PASS | único retry tras cerrar el Editor que bloqueaba DLLs; 4.39 s |
| Automatización C++ | PASS, 14/14 | `Saved/Automation/Performance58/Automation/NaturalGameplay58Contract/Report/index.json` |
| Escenario Standalone natural | PASS de integridad; FAIL de rendimiento estricto | `summary.json` |
| PIE visible natural | PASS con hallazgos | `visible_pie_evidence.json` |
| Rehash ACFU | PASS, 5,043/5,043 | evidencia protegida |
| Rehash DazToUnreal | PASS, 213/213 | evidencia protegida |
| Delta protegido nuevo | PASS_NO_NEW_DELTA | 74 mismatches históricos idénticos; delta 0 |
| Cook/package Test | PENDING | no ejecutado en esta pasada |
| Shipping/paridad de configuración | PENDING | no ejecutado en esta pasada |

El primer intento del build compiló el código, pero el linker encontró DLLs del
target abiertas por el propio Editor. Se cerró de forma limpia y se hizo el único
retry causal permitido. No hubo retry de métricas, repetición de preset ni traza.

## Evidencia

- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/summary.json`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/samples.csv`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/bootstrap_samples.csv`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/events.jsonl`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/system_metrics.csv`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/visual_check.png`
- `Saved/Automation/Performance58/DungeonNaturalGameplay58_20260731T191640Z/runtime.log`
- `Saved/Automation/Performance58/VisibleNaturalPIE_20260731T192956Z/visible_pie_evidence.json`
- `Saved/Automation/Performance58/VisibleNaturalPIE_20260731T192956Z/before_walk.png`
- `Saved/Automation/Performance58/VisibleNaturalPIE_20260731T192956Z/after_walk.png`
- `Saved/Automation/Performance58/VisibleNaturalPIE_20260731T192956Z/host_process_samples.csv`
- `Saved/Automation/Performance58/VisibleNaturalPIE_20260731T192956Z/runtime.log`
- `Saved/Migration/Evidence/Performance58_NaturalGameplay_ProtectedInvariantVerification.json`

La corrida visible dejó el Editor abierto y PIE detenido. No se guardó ningún
asset ni se modificaron ACF, Calysto, DirtyPawn o TattooShop.
