# Dungeon Director V3 — implementación, cutover y evidencia

Fecha de corte: `2026-08-14`  
Proyecto: `D:\Projects UE5\NoShellForWinter\NoShellForWinter.uproject`  
Git HEAD inspeccionado: `3ccce4e96457e0fd7980d7606d6823cce1e62eeb`  
Estado general: **FAIL/PENDING — V3 no puede declararse terminada**

Dungeon Director V3 está implementado y es la única autoridad de datos activa bajo
`/Game/_Game/Data/CalystoDungeon`. El cutover estático, los builds, la automatización
nativa y los cooks/packages nuevos de Development y Shipping están en PASS. Sin
embargo, el último PIE falla en Floor 5 porque Calysto no emite `PCGComplete` dentro
del timeout en ninguno de los dos intentos. Además quedan pendientes la matriz de
tamaños, el soak, la medición de rendimiento, la visual QA y el smoke runtime de los
paquetes. Por esas razones, ningún PASS parcial se interpreta como aceptación global.

## Supersesión y alcance

Este recibo supersede, para el contrato runtime y los gates activos del Dungeon
Director, a:

- `Docs/Migration/Evidence/Calysto_Dungeon_Master_Phase1_20260802.md`
- `Docs/Migration/Evidence/Calysto_Dungeon_Master_Phase2_20260802.md`
- `Docs/Migration/Evidence/Calysto_Spawner_Preset_Expansion_20260813.md`

Los documentos anteriores y
`Docs/Migration/Evidence/Tattoo_Chest_Calysto_Repair_20260811.md` se conservaron
sin modificar como evidencia histórica. No funcionan como fallback ni como
aceptación de V3.

El worktree completo continúa dirty y contiene numerosos cambios ajenos a esta
tarea. Este recibo no los atribuye a V3 ni intenta limpiarlos.

## Resultado implementado

### Autoridad V3 y retiro de V1/V2

El único archivo vivo bajo `Content/_Game/Data/CalystoDungeon` es:

`V3/DA_CalystoDungeonDirectorPolicy.uasset`

Datos verificados de la policy:

| Campo | Valor |
|---|---|
| Clase | `/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy` |
| Schema / generator | `3 / 3` |
| Policy ID | `CalystoDungeonDirectorV3` |
| SHA-256 | `A713B59800035E2215106E7CFCB800CA12BD0695132D2B29EF9DCC16222D2984` |
| Catálogo | 16 enemigos, 7 recursos y 2 temas |
| Validación nativa | PASS |

`Tools/Migration/Create-CalystoDungeonDirectorPolicyV3.py` conserva semántica
create-only: crea y guarda la policy únicamente cuando no existe. El recheck final
cargó el asset existente en modo `validate_existing_read_only`, no lo guardó, no
produjo mutaciones, mantuvo `dirty=false` antes/después y preservó el SHA-256.

Las tres DataTables V1 y las tres V2 fueron retiradas mediante AssetTools. Ya no
existen bajo `/Game`; el director no contiene fallback V1/V2 ni Core Redirects para
su semántica anterior. El script V2 y los row structs legacy también fueron
retirados.

El respaldo recuperable byte-exact se encuentra en:

`Saved/Migration/CalystoDungeonDirectorV3/RetiredPolicyAssets`

Su `manifest.json` verifica las seis copias contra los seis SHA-256 de origen. El
audit final encuentra cero tablas legacy vivas y cero ocurrencias activas de
`/V2`, `PolicyV2`, `PlanV2`, `THEME_V2` o `CALYSTO_PHASE2` en el alcance de
cutover. La evidencia histórica está excluida intencionalmente de ese scan.

### Núcleo probabilístico

La implementación nativa incluye:

- distribuciones `Min / Mode / Max / Concentration` con Beta-PERT determinista;
- presencia Bernoulli separada del conteo condicionado para ceros significativos;
- volatilidad que transforma la concentración de `8` a `2`, con `4` normal;
- progresión saturante, Run DNA, smooth floor noise, jitter y sesgos de intención;
- estilos Standard, Compact y Branching como sesgos, sin cantidades exactas;
- memoria GameInstance con pity, cooldowns, anti-streak, EMA de desempeño y hash de
  ecología;
- threat budget independiente, composición con caps/costos, rarity y backtracking
  limitado;
- dominios RNG separados para layout, tamaño, branching, anchors, presencia,
  amenaza, composición, temas, comida, cofres, loot, pacing y rarezas;
- tuple determinista con seed de run, piso, serial, versión, policy/ecology hashes,
  dominio, stable ID y draw index;
- `FloorIntent` previo y `RealizedFloorManifest` posterior, ambos hasheados;
- Replay exacto, Reroll con serial nuevo y commit de ecología únicamente en Advance.

Los límites fail-closed implementados son: tamaño `18..30`, Z `1`, enemigos
`0..25`, comida `0..8`, cofres `0..3`, actores iniciales `0..36`, candidate anchor
density `0.20..0.50` y side paths `0.30..0.70`. `SpawnerDensity=0` queda
prohibido: la densidad representa anchors candidatos, no enemigos.

### Integración Calysto por anchors

`AEFCalystoPopulationAnchor` es transitorio, invisible, sin tick, colisión,
replicación, daño ni efecto de navegación. El clon transitorio de
`DA_DemoSpawner` usa exclusivamente esta clase; ningún DataAsset vendor fue
guardado o parcheado.

Después del único `GenerateLocal`, el materializador:

1. recolecta los anchors y deriva stable IDs desde transforms cuantizados;
2. filtra entrada, salida, puertas, colisiones y puntos sin navegación;
3. completa shortages con una cuadrícula determinista proyectada a NavMesh;
4. asigna enemigos y recursos en dominios independientes;
5. usa `SpawnActorDeferred`, verifica conteos/costos y revierte si falla cerrado;
6. crea hashes de topología, población, recursos y manifest;
7. destruye todos los anchors;
8. habilita la puerta solo con PCG, ruta de navegación, manifest y población listos.

No se elimina población aleatoriamente después de BeginPlay. La composición llega
ya limitada al cap absoluto de 25 enemigos.

Durante PIE se detectó y corrigió una carrera del watchdog: el timer antiguo podía
invalidar un piso aproximadamente medio segundo después de `DoorEnabled`. El
subsystem ahora invalida el watchdog al confirmar readiness y abandona polling al
entrar en cualquier estado terminal. El fallo posterior de Floor 5 es distinto:
no hubo callback `PCGComplete` en ninguno de los dos intentos.

### API, adaptación y Dungeon Harness

Se conservaron New Run, New Run with Seed, Advance, Reroll, Replay, Travel,
snapshot y eventos de ready/completed/failure. Se añadieron:

- `SetNextFloorDirectorIntent`
- `ClearNextFloorDirectorIntent`
- `SubmitFloorOutcome`
- `GetResolvedFloorIntent`
- `GetRealizedFloorManifest`

Se retiraron `RequestRegenerateFloor`, `SetRunSeed`, forced presets, getters de
pesos V2 y row structs V1/V2. La intención pública acepta únicamente sesgos
normalizados de estilo, escala, branching, amenaza, recursos, tema y volatilidad;
el resolver mantiene la autoridad y los caps.

El Harness de `L` expone esos sesgos, las operaciones permitidas y el estado V3
con traits, budgets, probabilidades, conteos y hashes. Jump, sampling y overrides
exactos quedan restringidos a Development; Shipping los rechaza. EFProjectSystems
normaliza Combat, Survival, Resources, Pace y failures antes de entregarlos al
Director.

## Evidencia PASS

| Gate | Estado | Evidencia |
|---|---|---|
| Policy V3 existente y read-only | PASS | `CreateDirectorPolicyV3.json`; 0 saves, 0 mutations, SHA estable |
| Cutover estático V1/V2 | PASS | `FinalCutoverAudit_20260814.json`; único asset V3, backups exactos y scan legacy=0 |
| Versión del plugin | PASS | `EFProcedural.uplugin`: `Version=3`, `VersionName=3.0.0` |
| Build Editor UE 5.8 | PASS | `CalystoDungeonDirectorV3Build_Editor_20260814_075206.log` |
| Build Development UE 5.8 | PASS | `CalystoDungeonDirectorV3Build_Development_20260814_075206.log` |
| Build Shipping UE 5.8 | PASS | `CalystoDungeonDirectorV3Build_Shipping_20260814_075206.log` |
| Receipts Daz | PASS | DazToUnreal y EFCharacterCreationDazBridge habilitados en los tres receipts |
| Automatización nativa | PASS | 14/14, 0 warnings, 0 failures, 0 not-run |
| Package Development nuevo | PASS físico | full cook, 32/32 paquetes, 0 legacy y 0 diagnósticos bloqueantes |
| Package Shipping nuevo | PASS físico | full cook, 32/32 paquetes, 0 legacy y 0 diagnósticos bloqueantes |
| Assets Calysto protegidos | PASS | `ProtectedAssetsPostV3.json`: 13/13, 0 mismatches |
| Mutaciones/saves durante PIE | PASS | 0 / 0 |

La automatización nativa cubre validación de schema y clases, SHA-256 y hashes
canónicos, estados NaN/infinito/invertidos/duplicados, independencia de dominios,
un millón de seeds PCG sin colisión, 100.000 draws PERT/Bernoulli, progresión,
pity, anti-streak, rarity/cooldowns, Replay/Reroll, commit transaccional, caps y
casos exactos de 0/25 enemigos, 8 comidas y 3 cofres. También recorre Floors
1..1000 para los checks deterministas incluidos. Esto no sustituye los 100.000
intents/manifests completos ni el soak live requeridos.

### Paquetes cocinados

Development:

- archivo: `Saved/Migration/CalystoDungeonDirectorV3/Packages/Development_20260814_073426`;
- recibo: `PackageValidation_Development_20260814_073426.json`;
- manifest SHA-256:
  `0653276F84178B6FEB25C6AF2BF4FBEABA495E270519B04EB11ABFEC3471C1CB`.

Shipping:

- archivo: `Saved/Migration/CalystoDungeonDirectorV3/Packages/Shipping_20260814_073938`;
- recibo: `PackageValidation_Shipping_20260814_073938.json`;
- manifest SHA-256:
  `29B8C44A309783CD8100F27FA455BFD42BEEB6813360A5DB25312C9C0EF42B8E`.

Ambos recibos verifican que V3, anchor, catálogos seleccionados y temas estén en
el contenedor, y que las seis tablas V1/V2 no estén. Este es un PASS de cook y
contenido físico; no es todavía un PASS de gameplay ejecutado desde el paquete.

## PIE final — FAIL

Recibo autoritativo:

`Saved/Migration/CalystoDungeonDirectorV3/SeedReplayRerollAdvancePIE58_20260814_072520.json`

Log:

`Saved/Migration/Logs/CalystoDungeonDirectorV3PIE_20260814_072520.log`

Con seed fija `2026080201`, el test verificó:

- New Run aceptado desde el bootstrap inicial;
- Replay con intent y manifest exactos;
- Reroll del mismo piso con serial, intent, manifest y PCG seed diferentes;
- interacción con la puerta ACF real y readiness hasta Floors 2, 3 y 4;
- un Floor 4 válido con cero enemigos;
- cero mutaciones y cero saves de assets.

Al avanzar a Floor 5 / GenerationSerial 6, ambos intentos agotaron 30 segundos sin
`PCGComplete`. No hubo preparación de navegación ni path check porque el callback
PCG anterior nunca llegó. La recuperación fail-closed regresó correctamente al
HUB, pero el recibo termina con:

`UE58_CALYSTO_DUNGEON_DIRECTOR_V3_PIE_FAIL`

Por tanto, Floors 1..10, restart con la misma seed y jumps 25/50/100 no quedaron
aceptados. El retorno seguro al HUB pasa como comportamiento de recuperación; no
convierte el fallo de generación en PASS.

## Protección de invariantes

El baseline específico de Calysto y el rehash final coinciden para los 13 assets
protegidos. `BP_MassiveDungeon` conserva exactamente:

`47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B`

Los invariantes globales frente a Phase 0 permanecen en FAIL absoluto con 74
mismatches. El conjunto anterior y posterior a V3 tiene la misma cantidad y las
mismas firmas; ACFU y el plugin Daz pasan, mientras cuatro assets target Daz ya
fallaban en el baseline capturado. El dictamen correcto para esta tarea es
`PASS_NO_NEW_DELTA`, no PASS global. No se restauró ni sobrescribió ninguno de
esos assets.

## Gates pendientes

| Gate | Estado / motivo |
|---|---|
| Resolver el timeout de `PCGComplete` en Floor 5 | FAIL actual |
| Puerta real Floors 1..10 | PENDING; ejecución terminó en Floor 5 |
| Restart exacto con misma seed | PENDING |
| Development jumps 25, 50 y 100 | PENDING |
| Matriz 18..30, 20 seeds por tamaño | PENDING |
| Activación de tamaños 18..24, 27 y 29 | PENDING hasta aprobar su matriz |
| Validación de `{25,26,28,30}` ya authored | PENDING; la lista no equivale al gate |
| Escenarios PIE forzados 0/25 y extremos de comida/cofres | PENDING; 0 ocurrió naturalmente, no cubre toda la matriz |
| Outcomes thriving/neutral/struggling | PENDING en runtime |
| Anchor shortage y callback perdido forzados | PENDING; timeout/retorno HUB sí demostrado |
| 100.000 intents/manifests por bandas | PENDING |
| Soak live mínimo de 25 generaciones | PENDING |
| P95 Floor Ready, memoria, actores y residuos | PENDING |
| Visual QA | PENDING |
| Smoke runtime Development/Shipping Floors 1..10, 0 y 25 enemigos | PENDING |
| Restricciones Shipping ejecutadas en paquete | PENDING |
| Recheck MCP vivo final | PENDING; Editor cerrado y endpoint sin listener |
| Invariantes globales Phase 0 | FAIL absoluto preexistente; sin nuevo delta V3 |

La policy mantiene provisionalmente `ValidatedDungeonSizes={25,26,28,30}`. Hasta
que la matriz completa pase, esa lista debe tratarse como provisional. También
significa que el modal esperado de Floor 1 (`18..22`) no puede realizarse todavía
en toda su amplitud; activar tamaños menores sin evidencia violaría el propio gate
de seguridad.

## Dictamen

V3 reemplaza materialmente a V1/V2 y es la única autoridad de datos cocinada. Su
núcleo probabilístico, determinismo, límites, anchors, manifests, API, Harness,
builds y contenido de paquetes tienen evidencia positiva. No obstante, el corazón
runtime todavía exhibe un fallo reproducible de finalización PCG en Floor 5 y
faltan varios gates extensivos.

Estado de cierre: **FAIL/PENDING**. La siguiente intervención debe concentrarse en
instrumentar y resolver la ausencia de `PCGComplete` sin editar
`BP_MassiveDungeon`, los PCG graphs ni los DataAssets vendor; después deben
repetirse PIE 1..10 y todos los gates pendientes. V3 solo podrá marcarse completa
cuando cada gate exigido esté en PASS.
