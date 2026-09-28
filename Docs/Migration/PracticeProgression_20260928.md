# Progresión por práctica y entrenamiento — UE 5.8

Estado: **PRÁCTICA Y HUD VALIDADOS EN PIE Y PAQUETE**. La migración general conserva incidencias heredadas pendientes; no declara cero advertencias.
Destino: D:/Projects UE5/NoShellForWinter. LustAsDeadlySin permanece en solo lectura.
Base: ca254270; matemáticas/extensiones: e18a7966; integración de XP, guardado y entrenamiento: 40645f03; HUD/cámara: 04fbf585; carga inicial sin slot: cfa03050; activación/config/assets: 1ef4e63a.

## Decisiones aprobadas

- Doce atributos por práctica; puntos del nivel nativo exclusivamente para habilidades.
- Un ASC y un contador XP/nivel ACF. Fracciones y exposición no son monedas gastables.
- Conservar todo lo aprendido al morir, reaparecer, guardar/cargar o cambiar de mapa.
- Personajes nuevos, respaldando partidas anteriores; no convertir DXP/Meta.
- Cada actividad tiene duración/método propios. Se descarta la duración universal de 15 minutos.
- Alcance de validación: HUB y StorySelection. Por petición del usuario se excluyen pruebas dentro de Calysto; sus incidencias específicas quedan aplazadas al reemplazo por Dungeon Architect.
- Calendario existente: día de 30 minutos reales, x1/x2/x4 y pausa, con sus restricciones actuales.
- Revisión del usuario: conservar nombre del ejercicio y mostrar a su lado el atributo; overlay mínimo abajo a la derecha con tiempo de sesión y barra entre nivel actual/siguiente. Blanco, fuente fina, sombra/borde negro leve, sin modal ni minijuegos. Cámara libre durante práctica.

## Implementación

Código propio: EFProjectSystemsGameplay/Progression y Training/ProjectPracticeComponent.
El componente conserva fracciones double; los rangos aprendidos viven en la base GAS. Doctrine consulta el mismo ASC, también en NPC. Bonus temporales de equipo no conceden hitos permanentes. Los eventos actualizan pasivas, necesidades y UI. Se conserva proporción en necesidades, valor absoluto en sensaciones y máximo Curse=100.

El observador del nivel nativo deja Perks en cero y concede SkillPoints una vez por nivel, incluso con llamadas directas a AddExp. Combate nativo paga las muertes; Doctrine no paga combate/entrenamiento otra vez. Hits válidos generan práctica Offensive, deduplicada por receptor/frame. ACFDamageHandlerComponent::HandleStatReachedZero ya evita duplicar la misma muerte mediante bIsAlive. No se afirma deduplicación general de llamadas arbitrarias a AddExp ni seguridad multijugador.

ProjectProgressionState es un snapshot ALS, no otro backend. Conserva metadata nativa, bases GAS, fracciones, libros y habilidades. Reconstruye compras nativas y comprueba habilidades/efectos infinitos. No transfiere salud de un cadáver al reemplazo. Congela captura antes de teardown y permite restaurar metadata con destino pausado, sin simular práctica. Volver al mapa de menú configurado descarta el personaje en memoria; Continue restaura desde ALS. ResetForNewCharacter y NotifyNewGameStarted invalidan también las capturas del mundo saliente.

DT_ProjectSerializableAttributes y DT_ProjectAttributesSetByCallers conservan las cinco filas originales y añaden siete Doctrine. Registro del set antes de inicialización también en NPC, porque la tabla ACF es global. Directorio de datos propio incluido en cook.

Los dos GE de curvas validados se conectan a StartingEffects de cada instancia antes de inicialización, sin modificar Player/CDO/Marketplace. ACF 4.3.5 olvidaba los handles antiguos al reinicializar sin retirar sus efectos: el listener propio conserva la nueva instancia y elimina las anteriores de la misma clase de StartingEffects. Conteos PIE antes/después: [1,0,1]; cero corresponde al efecto instantáneo.

## Actividades

| Actividad | Atributo | Calendario/método |
|---|---|---|
| Acondicionamiento físico | Strength | Base 3 h, capacidad hasta 9 h según fuerza aprendida |
| Resistencia | Endurance | Base 3 h, misma capacidad física |
| Leer | Intelligence | Metadata ConfigureBook hasta 48 h; avance parcial persistente |
| Meditar | Willpower | Continua |
| Rezar | Faith | Continua |
| Practicar cerradura | Cunning | Cerradura real a menos de 300 cm; 0,25–3 h según dificultad, reducida por Cunning |
| Ensayar conversación | Charisma | Continua |
| Equilibrio | Dexterity | Base 1 h, crece con capacidad |
| Acondicionamiento suave | Constitution | Base 2 h, crece con capacidad |
| Formas de ataque, guardia, desplazamientos | Offensive, Defensive, Celerity | Base 1,5 h cada una, crece con capacidad |

Se reutilizan animaciones existentes: arrodillarse/sentarse, estirarse, hablar y guardia. No son doce animaciones nuevas especializadas. Lectura dispone de libro inicial abstracto y API de metadata; no se ha creado un catálogo de objetos libro ni props físicos de libro/ganzúa.

Progreso durante el tiempo realmente practicado, sin premio de finalización explotable. Cancelar conserva aprendizaje/exposición. Hambre, sed o sueño <=15% impiden/interrumpen actividad; daño también cancela. Alejarse o usar realmente la cerradura interrumpe práctica. Los contratos de sesiones/entrada permanecen extensibles, pero el adaptador rechaza el minijuego antiguo y no presenta desafíos de teclas todavía.

ProjectPracticeWidget reemplaza el panel modal durante práctica: ejercicio · atributo, horas/minutos de sesión, y progreso aprendido hacia el siguiente rango. Roboto Light blanco con borde/sombra leve; sin fondo, botones ni captura de foco. La barra no mide finalización del libro/sesión. Cámara de la animación: WASD/ratón, Q/E altura, O alterna cámara; Y/Esc detienen. El HUD general conserva diseño y muestra XP/SKILLS. Validación visual/input PIE: PASS; teclado W real y O/Y por entrada nativa del controlador.

3 h son 3 min 45 s reales a x1 o 56,25 s a x4. 9 h son 11 min 15 s o 2 min 48,75 s. Libro de 48 h: 60 min a x1 o 15 min a x4, repartibles en sesiones y sujetos a necesidades.

## Matemática configurable

Exposición E por atributo, semivida H=3 h:
`eficiencia(E)=0.0001+0.9999*2^(-E/H)`.

Integral analítica: no depende de FPS, acelerar o cancelar/reanudar. Piso 0,01%, nunca cero por abuso. No se regala 0,1 XP por tick. Tasa inicial 25 unidades/h; primer rango 100, crecimiento de coste 8% por rango; XP nativa=10% del aprendizaje integrado.

Recuperación analítica `dE/dh=-2/(1+E/12)`. Tras 3 h continuas: eficiencia 50,005%, descanso completo 1,6875 h (~2,11 min reales x1). Cambiar de ejercicio del mismo atributo no borra exposición. Otros atributos pueden recuperarse mientras se practica uno; no hay todavía fatiga corporal compartida. Ajustes en ProjectPracticeSettings; balance prolongado con jugadores PENDING.

## Gates y evidencia

Raíz: Saved/Analysis/PracticeProgression/.

| Gate | Evidencia | Estado |
|---|---|---|
| Editor | editor_build33.log, wrapper oficial y recibo Daz reparado | PASS |
| Automation | Automation_20260928_155743/index.json: 22 pruebas, cero fallos/avisos, incluye barra normalizada, paridad de curvas y bono temporal | PASS build33 |
| Blueprint | Player/BP_TSChar compilados en memoria por runners, sin resave | PASS |
| Jugador real | Visual_20260928_155223/receipt.json: 42 comprobaciones, HUD mínimo/cámara, un ASC, práctica, efectos, compra/respec, x4/pausa/cancelación, libro, necesidades y ALS save | PASS build31, salida 0 |
| Carga/travel pausado/personaje nuevo | Reload_20260928_131532/receipt.json: 21 checks, salida 0 | PASS build22 |
| Derrota/respawn | Respawn_20260928_130431/receipt.json, vivo y progreso conservado, salida 0 | PASS build21 |
| Visual | Visual/reading_x400007.png y minimal_practice_before_input00000.png; ejercicio/atributo/tiempo, barra blanca de rango, sin panel central. Paquete: PackagedQA_20260928_160740/UserData/Saved/Screenshots/Windows/practice_x400000.png y practice_paused00000.png | PASS PIE y paquete |
| Doce actividades | Activities/receipt.json: inician por Y, animación/progreso, continua/libro, cerraduras y límite 3–9 h | Assertions PASS; fallo al cerrar editor, ver abajo |
| Game | game_build33.log, interfaz final, wrapper oficial y recibo Daz reparado | PASS |
| Cook/stage/IoStore/paquete | Package_20260928_155926/uat.log; HUB, StorySelection y MenuMap; 6214 paquetes, 0 errores/37 advertencias en resumen de cook; UAT salida 0 | PASS con avisos heredados |
| Ejecutable renderizado | PackagedQA_20260928_160740/receipt.json; configuración normal bEnabled=True, x1/x2/x4, pausa exacta/reanudación de calendario y aprendizaje, salida 0 | PASS |
| Invariantes | protected_after_package33.json: 7900 hashes sin diferencias; código cfa03050, config/assets posteriormente registrados en 1ef4e63a | PASS |

Se investigó un fallo intermitente al cerrar el editor (Slate/FTextLayout). ALS ejecuta callbacks de guardado en un worker: las capturas/restauraciones propias ahora se despachan al game thread. Se evita además llamar dos veces a StopEmote, que cancelaba su recuperación diferida. Control ALS sin práctica y Visual/Reload/Respawn build21 terminaron con código 0; no se atribuye el crash a una única causa demostrada. Un ensayo anterior registró un ensure de Female BoneVisibilityStates; no reapareció en la secuencia build21. No se parchea Engine ni se reduce severidad.

El fallo de cierre reapareció tras completar las doce actividades con el panel modal anterior: activities_run24.log, salida -1073741819, activities_shutdown_dump.txt. Assertions guardadas pero no se promociona el cierre a PASS. PENDING de aislamiento con UI mínima y paquete. Las etiquetas del stack de Engine son parciales (exports, sin PDB completos); no demuestran por sí solas un culpable del código de práctica.

## Partidas

### Reanudación tras corte de luz / UI mínima

- Visual build31 completó 42 comprobaciones funcionales: HUD pasivo único, sin panel anterior, cámara W sin movimiento del alumno, O alterna cámara, Y cancela, x4/pausa, necesidades y guardado completo. Entrada mediante `PlayerController::InputKey`; no se presenta como teclado físico. La captura `Visual/minimal_practice_before_input00000.png` del ensayo anterior sí acompañó una comprobación W con teclado real.
- Captura revisada `Visual/reading_x400007.png`: ejercicio · atributo, horas/minutos de la sesión, rango 24→25 y barra blanca inferior derecha, sin fondo modal. Los demás HUD conservan su diseño.
- El cierre del primer ensayo build31 falló. Inspección de `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/EditorUtilities/EditorPythonExecuter.cpp` muestra que `SetKeepPythonScriptAlive(false)` debe permitir el siguiente tick, que destruye la notificación y difiere QUIT_EDITOR. El runner llamaba además `quit_editor()` en el mismo tick, omitiendo esa limpieza. Se retira esa llamada, sin parche de Engine. Repetición `Visual_20260928_155223`: 42 checks y cierre normal, código 0. Esto verifica la corrección de secuencia del runner; no afirma haber descartado cualquier otro fallo de Slate.
- Automation build31: 22 tests correctos, uno con aviso al intentar cargar ProjectIntimacy.sav inexistente tras archivar partidas. Se añade `DoesSaveGameExist` al cargador propio, conservando la creación inicial y carga de archivos existentes. Editor/Game build33 y Automation_20260928_155743: PASS, 22 pruebas sin avisos.
- `protected_after_ui31.json`: 7900 archivos protegidos verificados, cero diferencias.

Saved/Backups/ProgressionV2_20260928_104559/manifest.json: 141 archivos respaldados/verificados SHA256 antes del cambio. Runners con Progression-SaveGuard: conservan salidas QA y restauran originales; evidencia en cada SaveGuard.json. No recupera retroactivamente el incidente histórico de ProjectIntimacy.sav del piloto.

Archivado de partidas activas: PASS. Se verificaron los 141 respaldos y se trasladaron seis slots antiguos a Saved/Backups/ProgressionV2_20260928_104559/ArchivedActiveSlots, byte por byte. Evidencia archive_receipt.json en ese respaldo y archive_old_saves.log/archive_safety.json en Analysis. Se conserva CharacterCreationPresets.sav y los backups históricos de equipo. Sin conversión DXP/Meta ni sustitución por fixtures. DefaultGame.ini activa bEnabled=True para personajes nuevos.

## Matriz de incidencias heredadas

No constituye cierre de las 44 advertencias del piloto.

| Origen/causa | Corrección y contrato conservado | Estado |
|---|---|---|
| Nueve RowName huérfanos | GE propios equivalentes enlazados por instancia, sin duplicación | PASS paridad/PIE |
| DataTables de tags None y ACFJ inexistentes | Retirar entradas inválidas; mantener Configuration/TurnAbilityTags real de ACF 4.3.5 | PASS cook33, sin esos avisos |
| Cinco tags serializados sin registrar | Registrar nombres exactos de ReactiveWidget, LadderComponent, DamageOverTime y Compass/SampleQuestTags. Sin redirects inventados, incluida grafía Poisioned | PASS carga/cook33; ramas específicas PENDING |
| Widgets ambiguos | Fijar InnerStateGlobal y ExchangeMenuGlobal elegidos por baseline ProgressionPilot/visual.log | Nueva comparación PENDING |
| GameplayCueNotifyPaths ausente | Declarar /Game conservando todo el alcance previo, sin afirmar optimización | PASS configuración/cook; colisión de cues separada abajo |
| Consulta AssetRegistry en cook | Stack del piloto señala MetaHumanCoreTech/MetaHumanPerformance | Proveedor PENDING; no editar Engine |
| Imports AscentLevelingSystem/AscentGASIntegration; SetFontSize; Daz sin versión; ini/redirects ACF | Assets/config protegidos, sin redirects o resaves arbitrarios | Proveedor PENDING |
| Colisión Hit/Reaction cues y DefaultSlot duplicado | Hay que preservar feedback y rutas de montages; no eliminar para quitar aviso | PENDING |
| LevelBlock array vacío de Calysto | Fuera del alcance por instrucción del usuario; reemplazo previsto por Dungeon Architect | APLAZADO, sin pruebas de dungeon |
| Escalera/Arrow sin versión | Pruebas específicas de escalada/arco | PENDING |
| r.Shadow.Virtual.Enable | Proyecto impone 0 sobre scalability inferior; resultado visual preservado | Consolidación PENDING |
| HTTP free_tier_config | Servicio del plugin de asistencia; no es la progresión runtime | Proveedor/disponibilidad PENDING |
| Quest sample sin actor resuelto | No desactivar misiones para conseguir PASS; aparece en algunas sesiones HUB | PENDING |
| DirtyPawn ausente en paquete anterior | Cooker recibe explícitamente el wrapper real; cargado por BP_TSChar y visible en captura del ejecutable | PASS paquete/render33 |
| PCGEx CachedPins y registro de herramientas editor | Cook muestra mismatch ArrayProperty/StructProperty en subgraph de PCGExtendedToolkit; cierre UECPCore avisa al desregistrar herramientas de otros propietarios | Proveedores PENDING; no modificar Marketplace |

Runners: Run-PracticeProgression58.ps1 (Automation/Visual/Reload/Respawn), Package-PracticeProgression58.ps1, Validate-PracticePackaged58.ps1. Compilar mediante wrappers oficiales; ningún editor durante exclusión UBT temporal de Daz.

El ejecutable aún registra `Can't Start the quest` de la misión sample de HUB; la prueba de práctica no lo oculta ni certifica esa misión. Las capturas de paquete incluyen el contador verde CsvProfiler del harness, que no pertenece al HUD de producción. El resumen de 37 advertencias del cook no incluye los avisos adicionales de cierre del plugin editor UECPCore. No se hicieron pruebas runtime dentro de la dungeon.
