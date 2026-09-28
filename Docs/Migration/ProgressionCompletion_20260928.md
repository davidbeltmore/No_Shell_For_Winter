# Cierre de integración de práctica — continuación 2026-09-28

Destino: `D:/Projects UE5/NoShellForWinter`; origen solo lectura.
Continúa [PracticeProgression_20260928.md](PracticeProgression_20260928.md), base `5cc95ec4`.
Código de esta continuación: `812f8f0e`; runners de validación: `9c91b1c9`; cierre de compatibilidad del paquete: `5cd26b89`.
No se prueban Calysto ni la dungeon. HUB y StorySelection son el alcance aprobado.

## Cambios complementarios

- `FProjectAttributeSynergies` consulta los valores efectivos del ASC existente. Constitution reduce de forma lineal el dolor de golpes calificados hasta 25%; Endurance reduce el sudor generado por actividad hasta 30%. No cambian los umbrales, el máximo de dolor de Willpower, la recuperación, los materiales ni la aplicación manual de sudor.
- DirtyPawnRuntime ofrece un resolver opcional de la ganancia de sudor; EFProjectSystemsGameplay registra la fórmula y la retira al descargar el módulo. El valor efectivo se consulta al generar sudor, sin almacenamiento duplicado ni dependencia de la presencia de un componente visual en el personaje.
- Intelligence aporta `floor(min(5, 0.05 * Intelligence))` como apoyo a Cunning en la interacción real y duración de práctica de cerraduras. Ajustes en ProjectPracticeSettings; rangos efectivos acotados por MaximumAttributeRank. El equipo no se convierte en aprendizaje permanente. Desactivar la progresión deja multiplicadores neutros.
- DefaultEngine mantiene `r.Shadow.Virtual.Enable=0`. Se retiran tres escrituras redundantes de DefaultScalability sin cambiar el resultado.
- Los dos assets `ACF_DefaultHit_GC` y `ACF_DefaultReaction_GC` heredan `ACFHitEffectGameplayCue`, con el mismo tag, defaults y eventos sin override. ProjectGameplayCueManager registra Hit de forma determinista y conserva Reaction cargable por su ruta pública. No se elimina feedback ni se inventa otro tag.
- Packaging conserva explícitamente `/Game/FullSample/Blueprints/GAS/Cues`. El primer paquete de esta continuación (`Package_20260928_172905`) terminó con UAT 0, pero la inspección del AssetRegistry cocinado detectó que el alias Reaction había quedado fuera al filtrar su registro. Ese paquete no certifica compatibilidad y se reemplaza; el runner ahora exige ambos nombres en el registro cocinado antes de ejecutar el juego. Evidencia de rechazo correcto: `Saved/Analysis/ProgressionRemaining/cooked_cue_negative_gate.json`.
- El inicio automático del controlador sample pasa por `ProjectQuestStartComponent`. Una misión con objetivos iniciales en otro mapa queda en espera y vuelve a comprobar disponibilidad cada segundo de simulación; al aparecer los actores y sus QuestTargetComponent en el mundo correcto se llama al StartQuest nativo. No se cargan mapas ajenos ni se sustituyen los objetivos. Las misiones que cargan su propia DataLayer conservan ese flujo nativo. Los objetivos posteriores pueden pertenecer a otros mapas.

La inspección encontró que `AQS_IntroductionToACF` referencia `L_Gameplay:PersistentLevel.ACFGuy_BP_C_0` y `ACF_Vendor_BP_C_0`; estos actores no pertenecen a HUB. Evidencia: `Saved/Analysis/ProgressionRemaining/asset_defaults.json`. El adaptador se limita a la llamada automática existente; no sustituye el sistema AQS.

## Validación de esta continuación

Raíz de evidencia: `Saved/Analysis/PracticeProgression/`. Registro consolidado: [Evidence/ProgressionCompletion_20260928.json](Evidence/ProgressionCompletion_20260928.json).

| Gate | Evidencia | Estado |
|---|---|---|
| Editor | editor_build40.log; wrapper oficial, recibo Daz reparado | PASS |
| Automation | Automation_20260928_171523/index.json: 26 pruebas, 0 fallos/avisos | PASS |
| Archivos protegidos | protected_after_package41.json: 7900 hashes sin diferencias, snapshot 5cd26b89 | PASS, después del paquete |
| Doce actividades / esfuerzo / pausa | Activities_20260928_171657/receipt.json: 81 comprobaciones, salida normal 0 | PASS |
| PIE visual / cámara / guardado | Visual_20260928_171955/receipt.json: 46 comprobaciones, salida normal 0 | PASS |
| Captura revisada | Visual/minimal_practice_before_input00004.png: barra blanca inferior derecha, nombre/atributo/tiempo, sin modal | PASS |
| Carga / cambio de mapa / personaje nuevo | Reload_20260928_172229/receipt.json: 22 comprobaciones, salida normal 0; HUB → StorySelection, viaje desde pausa, personaje nuevo sin herencia | PASS |
| Game | game_build40.log; Development, recibo Daz reparado | PASS |
| Cook | Package_20260928_173428/uat.log: 6214 paquetes; resumen 0 errores / 35 advertencias heredadas; Hit y Reaction presentes en AssetRegistry cocinado | PASS |
| Stage / IoStore / package | Package_20260928_173428/uat.log: UAT 0, BUILD SUCCESSFUL | PASS |
| Ejecutable renderizado | PackagedQA_20260928_174009/receipt.json: salida 0, x1/x2/x4, pausa exacta/reanudación de reloj y aprendizaje, cero conflictos de cue/errores de inicio de misión, alias incluido | PASS |
| Visual del ejecutable | PackagedQA_20260928_174009/UserData/Saved/Screenshots/Windows/practice_x400000.png y practice_paused00000.png revisadas | PASS |

Las pruebas incorporan efectos efectivos y temporales de atributos, deduplicación del cue conservando sus eventos nativos, disponibilidad de objetivos de misión y cálculo del esfuerzo independiente de la partición del tiempo. Los resultados anteriores no certifican los cambios nuevos.

El fallo anterior del harness al cerrar PIE se resolvió retirando la llamada inmediata redundante a `quit_editor`: `-ExecutePythonScript` debe limpiar su notificación Slate asíncrona antes de diferir el cierre. Activities39 y Activities40 terminaron con salida 0; el harness no marca PASS por las assertions si falla el proceso. Visual39 descubrió además una variable local `key` que ocultaba el helper de entrada; se corrigió en el runner y Visual40 completó las pruebas W/O/Y con `PlayerController::InputKey` (no se presenta como teclado físico).

Los widgets de Needs y Doctrine coinciden con las clases fijadas por el baseline. `Saved/Analysis/ProgressionRemaining/legacy_compatibility.json` prueba las seis secuencias Climb mediante descompresión al inicio/medio/final, huesos y transformaciones finitos, y Arrow mediante geometría/bounds válidos (164 vértices). No se resavó ninguno de esos assets. Esto certifica carga/descompresión; no sustituye una prueba completa de escalada/arco.

SaveGuard verificó restauración byte a byte en las cuatro sesiones (Automation, Activities, Visual y Reload). El ejecutable usó UserDir aislado. HUB conserva el hash previo `1F7DF72F77516F8F96B197AF1CD660106A7D145DAD6E6D047DCDEEB018FFC5C3`; los cambios ajenos de Chest/limpieza permanecen sin incluir en los commits. La tabla propia AttributesSetByCallers fue regenerada por el runner y se validaron sus filas/atributos contra la exportación esperada. El contador verde CsvProfiler de las capturas pertenece al harness, no a la interfaz de producción.

## Sudor por esfuerzo físico

Petición adicional: la actividad genérica no debe generar sudor. DirtyPawn conserva el sudor como resultado visual/fisiológico y añade `PhysicalEffort` transitorio (0–1), sin otro medidor ni penalización de entrenamiento. Un resolver opcional enlaza las actividades del proyecto con el componente canónico de DirtyPawn.

| Actividad | Intensidad |
|---|---:|
| Correr en suelo por encima de 450 cm/s | 1 |
| Ascenso de un salto iniciado por el personaje | 0,7 |
| Physical conditioning / Endurance practice | 1 |
| Attack forms / Guard practice / Footwork | 0,65 |
| Gentle conditioning | 0,45 |
| Balance practice | 0,35 |
| Leer / meditar / rezar / practicar cerraduras / hablar | 0 |

Una caída pasiva, volar, caminar y mover la cámara no se clasifican como ejercicio. Se retira la inferencia de rodar basada únicamente en mantener Alt: las acciones futuras deben declarar su esfuerzo explícitamente, sin confundir teclas con acciones ejecutadas. Combate estacionario y acciones físicas adicionales no tienen todavía una clasificación específica.

El esfuerzo se acerca a la intensidad a 0,1 unidades/segundo al subir y 0,05 al bajar. Solo con actividad física se integra `max(0, esfuerzo − 0,25) / 0,75` para generar sudor, multiplicado por la tasa de sudor y el bono efectivo de Endurance. Se integra exactamente el tramo lineal y la meseta, evitando que cambiar x1/x2/x4 cambie la cantidad para una duración simulada idéntica. Delta cero congela el estado. No se reinicia el esfuerzo al cambiar de ejercicio ni al lavar el sudor; descansar sí lo recupera.

Valores ajustables: `PhysicalEffortIntensity` por definición, `EffortRiseSeconds=10`, `EffortRecoverySeconds=20`, `SweatEffortThreshold=0.25`. Son segundos de simulación; con día de 30 minutos, 10 s equivalen a 8 minutos del calendario. A intensidad 1 y sin Endurance, los primeros 20 segundos generan 13,75 puntos de sudor; Endurance 100 los reduce a 9,625. Se conservan lavado, persistencia visual, máximos y adiciones manuales/debug. `SweatIntimacyGainPerSecond` queda como propiedad heredada de compatibilidad sin activación genérica: no se atribuye esfuerzo a intimidad ni a cualquier emote solo por estar activo.

## Límites de cierre

Las incidencias protegidas de ACF/Daz/Engine y del servicio externo se mantienen separadas de las correcciones del proyecto. No se suprimen logs ni se rebaja su severidad. Los avisos de Calysto quedan aplazados por instrucción del usuario. La ausencia de versión de animaciones de escalera/Arrow no justifica resaves para esconder el aviso: cook/carga y compatibilidad funcional son gates distintos.

| Pendiente heredado | Límite de esta entrega |
|---|---|
| ACF: imports antiguos, pin SetFontSize, redirects/ini y DefaultSlot duplicado | PENDING proveedor; no se modifica Marketplace ni se alteran slots sin validar sus montages |
| Daz: assets sin versión de Engine | PENDING proveedor; hashes protegidos conservados |
| MorphPhysics/Blink: constraints/morphs ausentes en las mallas actuales | PENDING compatibilidad; avisos previos a este cambio, sin alterar las mallas protegidas |
| MetaHumanCoreTech: consulta AssetRegistry durante cook | PENDING Engine/proveedor |
| Smoke tests de Engine al arrancar: `Condition failed` | PENDING Engine/localización; ya presentes en Visual build33, distintos de las 26 pruebas del proyecto |
| PCGExtendedToolkit: CachedPins ArrayProperty/StructProperty | PENDING proveedor; no forma parte del entrenamiento |
| UECPCore al desregistrar herramientas; free_tier_config HTTP | PENDING proveedor/servicio; no se ocultan mensajes |
| Escalada/arco completos y misión sample con NPC de L_Gameplay | PENDING escenarios específicos; carga de assets y disponibilidad de objetivos sí probadas |
| Balance prolongado de progresión/descansos | PENDING pruebas con jugadores; parámetros editables y matemáticas verificadas |

No se reabre la dungeon de Calysto para cerrar estos puntos. El cambio de integración a Dungeon Architect sigue fuera de esta entrega.

Diagnóstico de los mensajes de arranque: `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4376` ejecuta `RunSmokeTests`; `Core/Public/Misc/LowLevelTestAdapter.h:130` emite `Condition failed` desde CHECK. `Core/Tests/Experimental/UnifiedError/UnifiedErrorTests.cpp:479–533` compara mensajes localizados contra literales ingleses; el log muestra `Error vacío` y `Error con int -7` en español inmediatamente antes de los errores. La localización es una causa probable, no una reproducción aislada certificada. Se mantienen idioma y Engine intactos. Por ello, «26 pruebas sin fallos» se refiere al reporte exportado del conjunto del proyecto y no significa «log de Editor sin errores».
