# Inner State: contrato y calibración

Proyecto: `D:/Projects UE5/NoShellForWinter`, UE 5.8. Implementación propia en `EFProjectSystemsGameplay/Survival`. Valores editables: **Project Settings > EF Project Systems > Project Inner State**. Las necesidades antiguas siguen usando su configuración original; no hay una segunda fuente de Hunger/Thirst/Sleep/Pain.

## Tiempo y unidades

`horas_de_juego = segundos_de_simulación × 24 / DayLengthSeconds`.
El día actual dura 1800 segundos: una hora del juego equivale a 75 segundos a velocidad x1. Pausar detiene ambas escalas. La aceleración del mundo acelera también las necesidades; el control de tiempo rechaza veneno, falta de aire, inconsciencia y enfermedad crítica. El sueño bajo permite acelerar solamente durante descanso seguro. Los factores actuales son x1/x2/x4.

| Proceso sin otros modificadores | Tiempo del juego | Tiempo real a x1 |
|---|---:|---:|
| Hunger 100 → 0, 4 puntos/h | 25 h | 31 min 15 s |
| Thirst 100 → 0, 7.5 puntos/h | 13 h 20 min | 16 min 40 s |
| Sleep 100 → 0, 2.625 puntos/h | 38 h 6 min | 47 min 37 s |
| Descanso Sleep 0 → 100, neto 12.5 puntos/h | 8 h | 10 min; 2 min 30 s a x4 |
| Hygiene 100 → 20, pérdida base 1/h | 80 h | 100 min |
| Bathroom: límite entre evacuaciones | 48 h | 60 min |
| Sickness 25 → 100 sin tratamiento, +2/h | 37.5 h | 46 min 53 s |
| Sickness 100 → 0 con medicina, −6/h | 16 h 40 min | 20 min 50 s |
| Air 100 → 0, sin aire respirable | — | 45 s quieto / 30 s con esfuerzo máximo |
| Awareness 100 → 0, Air vacío | — | 20 s adicionales |
| Recuperar Air 0 → 100 respirando | — | 5 s |

Ejecutar `python Tools/InnerState/Calibrate58.py --day-seconds 1800` para recalcular. El script lee los valores por defecto; los overrides de Config y estados activos se verifican en el editor. Los tiempos son calibración de juego, no afirmaciones clínicas.

## Composición estable

Las entradas lentas se evalúan una vez por segundo. La fisiología aguda usa pasos fijos de 0.1 s. Cada influencia tiene `Source`, `Family`, duración y unidad explícita. Fuentes distintas suman sus aportes; una familia no vacía sustituye su miembro previo, evitando multiplicar beneficios con consumos repetidos. No se crea un temporizador por estado. La UI agrupa sus actualizaciones a 5 Hz mientras es visible.

Para los objetivos suavizados: `nuevo = objetivo + (actual − objetivo) × 2^(−tiempo/semivida)`. Stress sube con semivida de 2 h y se recupera con 3 h, o 1 h al dormir. Composure reacciona al miedo con semivida de 3 s y se recupera hacia seguridad/determinación en 20 s. Ninguna muerte de un enemigo agrega culpa o estrés.

Stress recibe cargas graduadas de hambre/sed/sueño severos, dolor o enfermedad, repetición prolongada, suciedad, urgencia y terror. La severidad empieza por debajo de 25% para las necesidades primarias; no penaliza continuamente al jugador sano. La repetición tiene 4 h de gracia y se disipa al parar.

Stimulation usa `dW/dh = Q − ln(2)/48 × W`, integrado analíticamente. `Q` es la suma de aportes, limitada a ±1.5 puntos/h. No se suman penalizaciones multiplicativas recursivas. Las cargas máximas por hora son estrés −0.6, higiene −0.2, soledad −0.25, locura −0.25 y enfermedad −0.2. La soledad empieza tras 72 h y alcanza su máximo tras otras 72 h. Ejercicio y compañía aportan +0.25/h durante 12 h; descanso +0.2/h durante 8 h. Hay ventanas de recompensa para impedir su acumulación artificial. Estar acompañado evita soledad; las APIs permiten notificar conversaciones significativas futuras.

Depressed requiere W < −40 durante 48 h; desaparece tras W > −25 durante 24 h. Reduce la práctica 10% y aumenta las ganancias de Madness 25%. Bienestar >40 mejora gradualmente la práctica hasta 5% y reduce las ganancias de Madness hasta 10%. Composure modifica recuperación de stamina hasta ±5%; estrés severo la reduce hasta 10%.

## Awareness y Sensation

Sensation representa −100 placer, 0 neutral y +100 dolor. Internamente conserva el nombre `Pain` por compatibilidad de assets. Las lecturas antiguas de Pain devuelven solamente daño positivo; un analgésico no fabrica placer y el placer no absorbe golpes. El HUD normaliza al máximo dinámico de Pain. El placer vuelve a neutral a 10 puntos/h.

Awareness tiene una capacidad sostenible `100 − min(80, suma_cargas)`. Pesos máximos: dolor 40, sueño 20, enfermedad 20, salud baja 30, alcohol 40 y esfuerzo con sueño 20. La capacidad baja a 2 puntos/s; recupera hasta 4/s cuando las causas lo permiten. Las pérdidas agudas suspenden recuperación mientras actúan: asfixia 5/s, dolor ≥95 mantenido 5 s: 2/s; alcohol extremo y efectos de sedación/exhaustion también pueden agotar Awareness.

Los golpes con `ProjectConcussiveDamageType` reducen Awareness según daño/maxHP (factor 2, cabeza ×1.5). `BP_ConcussiveDamage` es la clase de contenido para asignar a futuras armas contundentes; las armas existentes conservan sus clases y sus impactos ya pueden afectar consciencia mediante dolor y salud baja. No se convierte toda herida de espada en conmoción.

Awareness 0 usa el puente de derrota existente para bloquear al personaje inconsciente. Recuperación: al menos 10 s, Awareness ≥30, Air >20 y ausencia de pérdidas agudas. Despertar no regala salud ni elimina dolor. Air 0 + Awareness 0 durante asfixia lleva salud a cero y dispara la muerte real de ACF.

## Higiene, baño y enfermedad

Hygiene pierde 1/h más suciedad por exposición/esfuerzo, con presupuesto extra renovable de 12 puntos/día. Por debajo de 20 durante 24 h agrega Madness a 0.05/h. La suciedad aumenta el riesgo **cuando hay contaminación**, con riesgo máximo base 0.005/h y probabilidad `1−exp(−riesgo×horas)`. No se enferma automáticamente por alcanzar un número de días.

Un baño restaura 100 y limpia los materiales de Dirty Pawn. El paño suma 25 hasta 75, sin bajar un valor superior. `Y > Actions > Care` ofrece descanso, baño en agua no contaminada con la cabeza fuera y evacuación. Evacuar en el campo cuesta 5 Hygiene; una interacción sanitaria puede llamar `RelieveBathroom(true)` sin esa pérdida. Al cumplirse 48 h sucede un accidente con −35 Hygiene y el reloj reinicia; no se acumula una penalización cada frame.

Sick inicia Sickness en 25. En 100 se activa daño de 0.1% maxHP/s y pérdida de 0.1 Awareness/s. Al tratarlo, esas pérdidas se reducen proporcionalmente entre 100 y 80 y se desactivan en 80. Una medicina dura 24 h y no se acumula al consumir varias inmediatamente.

## Poison: deuda de daño

Cada exposición guarda puntos, velocidad, toxina, instante de activación y máximo de vida de referencia. `deudaHP = puntos/100 × maxHP_al_exponerse`. Las pociones de vida jamás modifican estos datos. Una nueva comida venenosa siempre añade una nueva exposición; 100 + 100 = 200. Se rechaza infinito/NaN computacional, pero no existe un máximo de juego.

| Tipo | Incubación | Consumo de deuda |
|---|---:|---:|
| Rápido | 3 s | 1 punto/s |
| Normal | 10 s | 0.5 puntos/s |
| Lento | 30 s | 0.25 puntos/s |

Una deuda total ≥100 activa inmediatamente las dosis pendientes. No aplica todo el daño de golpe. Dosis del mismo tipo comparten su velocidad; tipos mezclados comparten un límite total de 1 punto/s, proporcional a sus velocidades. Así 200 de veneno rápido son 200% de vida de referencia a lo largo de 200 s, y no 2%/s por acumular dos dosis. Cada incubación permanece independiente al añadir exposiciones menores.

25 rápido dura 3 + 25 = 28 s sin tratamiento. 100 rápido con maxHP=200 guarda 200 HP de deuda. Tras recibir 198 HP y curar 50 HP, quedan 2 HP de deuda: sobrevivirás con 50 HP si no hay otras pérdidas. Cambiar maxHP después no recalcula exposiciones anteriores. Antídoto anula deuda pendiente, sin devolver daño pasado. El estado BP de veneno existente conserva su ruta pública y usa este cálculo; su DOT original no corre en paralelo.

## Contenido y extensión

`/Game/_Game/InnerState/Items`: Antidote, Medicine, Washcloth, HealthPotion (25%), ComfortTea, Analgesic y RottenRation (25 rápido). Se consumen a través del inventario ACF. `Pickups/BP_SurvivalSupplies` agrupa suministros en un pickup colocable; no cambia el mapa HUB del usuario ni regala suministros automáticamente. Los iconos/meshes de inventario son recursos existentes provisionales, editables por item.

`ProjectInnerStateExposureVolume` funciona en cualquier mapa: aire disponible, contaminación, objetivo de Composure y dosis venenosa al entrar. Cada volumen retira solamente sus fuentes al salir/destruirse. Las zonas, NPCs y futuros eventos usan las APIs del componente; no necesitan un nuevo tick propio para cada modificador.

El HUD mantiene seis filas primarias y siete cuadros. Los seis primeros contienen Stress/Stimulation/Hygiene/Bathroom/Air/Composure. El séptimo muestra simultáneamente Sickness/Poison/Curse descubiertos y activos, con iconos y cifras pequeñas. Poison abrevia cifras muy grandes; el tooltip conserva la deuda exacta. Los atributos de Doctrine permanecen disponibles en sus otros menús.

Persistencia: estado SaveGame del componente y necesidades legacy incluidos en `ProjectProgressionState`, junto a los relojes relativos e incubaciones. Las partidas antiguas reciben valores neutrales. Se conserva la prueba de serialización, el roundtrip en disco y la restauración del snapshot de viaje del personaje real. Las pruebas de mapa/carga completa se registran separadamente en el documento de implementación.
