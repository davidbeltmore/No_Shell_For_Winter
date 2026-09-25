# Piloto de progresión unificada — 24/09/2026

## Alcance

Continuación de ProgressionUnification_ACF_20260924.md. Este trabajo añade un piloto optativo, no activa la migración del jugador ni elimina DXP/Meta. No modifica layouts, widgets, Player, plugins Marketplace ni el proyecto fuente. La primera captura PIE provocó un autoguardado incidental, descrito abajo.

## Causa de las advertencias

La inspección por commandlet de UE 5.8 encontró nueve referencias huérfanas en FScalableFloat: cuatro en ACF_AttDependencies_GE y cinco en ACF_StatInit_GE. Las curvas principales AttributeCurve sí tienen tabla. Hay nombres como EnduranceToMaxStamina en campos auxiliares que no corresponden al atributo del modificador; no son una especificación fiable para reconstruir curvas.

ScalableFloat.cpp, EvaluateCurveAtLevel, devuelve Value cuando existe RowName sin CurveTable ni RegistryType válido. La corrección conservadora elimina exclusivamente ese RowName y conserva Value, captura de atributos, curva principal, operación y duración. No introduce balance nuevo.

Assets propios:

- /Game/_Game/Data/Progression/GE_ProjectAttDependencies_GE
- /Game/_Game/Data/Progression/GE_ProjectStatInit_GE

Son copias derivadas. Todavía no sustituyen los StartingEffects del jugador o enemigos: las advertencias de los originales seguirán existiendo hasta conectar las copias mediante una integración revisada. No se ha silenciado globalmente LogAbilitySystem.

Scripts: Inspect-ProgressionCurves58.py y Create-ProgressionCurveAdapters58.py. El segundo compila y guarda solamente las dos copias propias. Tras una actualización ACF se debe comparar con el origen y repetir la prueba numérica; no asumir que una copia se actualiza automáticamente.

## Piloto C++

Progression/ProjectDoctrineAttributeSet añade siete atributos GAS replicables. No se autoañade al ASC del jugador.

FProjectProgressionBridge no almacena XP, niveles o monedas. Purchase usa AssignPerkToAttribute del componente ACF existente. La lista permitida incluye cinco atributos principales y siete Doctrine; rechaza cantidades no positivas, atributos desconocidos, falta de puntos, ausencia del set, falta de autoridad y compra por encima de 100. El límite 100 es provisional del piloto, no una nueva decisión de balance.

GrantExperience valida curvas, estado y cantidades antes de llamar AddExp. Limita el avance al máximo solicitado y descarta el exceso al alcanzar ese máximo. Esta política sólo opera al usar el puente; no protege las llamadas directas existentes a AddExp ni resuelve por sí misma recompensas duplicadas.

Las compras usan el valor base, no el valor efectivo con equipo. La integración futura debe conservar esa distinción al evaluar hitos permanentes.

## Pruebas reproducibles

1. Build-NoShellForWinterEditor58.ps1.
2. Launch-NoShellForWinterEditor58.ps1 -PythonScript <ruta absoluta>/Create-ProgressionCurveAdapters58.py.
3. Run-ProgressionPilot58.ps1.
4. Build-NoShellForWinterGame58.ps1.
5. Package-ProgressionPilot58.ps1.
6. Run-ProgressionVisual58.ps1.

Package-ProgressionPilot58.ps1 incorpora dependencias y reglas generales del proyecto; seleccionar Entry no garantiza aislar el cook del HUB. No confundir este paquete de ensayo con el contrato completo de Package-NoShellForWinter58.ps1.

## Resultados confirmados hasta la prueba nativa

- Editor Development: PASS, build4.log.
- Game Development: PASS, game_build.log, 206 acciones, 302,35 segundos; descriptor restaurado y recibo Daz verificado.
- Automation: 18 PASS, 0 warnings de prueba, 0 fallos, 0 sin ejecutar. Recibo Automation_20260924_221635/index.json.
- Las 68 comparaciones de magnitud originales/copia pasan. Las advertencias esperadas de los originales se reconocen exclusivamente dentro de esa prueba; las copias no contienen las referencias huérfanas.
- Blueprint compile de las dos copias: ejecutado por el generador; ambas clases persistidas se cargaron en un proceso nuevo durante Automation.

## Resultado de cook, paquete y PIE

- Cook: PASS con advertencias. 5.304 paquetes, cero errores, 44 advertencias. El primer cook completo está en package_launch2.log; la ruta de staging se corrigió después.
- Stage, IoStore, package y archive: PASS. package_launch3.log, UAT ExitCode=0. Artefacto: Saved/Analysis/ProgressionPilot/Package_20260924_222737/Archive/Windows.
- Arranque del ejecutable: Entry carga, la clase ProjectDoctrineAttributeSet está registrada y el proceso termina con código 0. **No es un smoke libre de errores**: el GameMode habitual intenta iniciar una misión cuyo actor no está en Entry. AQSQuestObjective.cpp:145 emite Can't Start the quest cuando una referencia de actor no está resuelta. Además falta en este paquete de ensayo M_DirtyPawn_DAZSkinWrapper, aunque el asset existe en Content. El test usó NullRHI: no prueba renderizado del ejecutable.
- PIE: el pawn real BP_TSChar abrió el HUD y el menú Doctrine. Visual/receipt.json registra ambos y las capturas. Inspección visual: el HUD conserva estilo, barras, etiquetas y tarjetas; el menú conserva tarjetas, marcos y tipografía. El viewport estrecho del editor recorta texto a la derecha y solapa el pie del menú. Es una referencia de la UI actual, no una certificación de paridad visual de una migración.
- DXP/Meta siguen visibles porque no se activó el backend unificado. La compra validada fue en los objetos de prueba, no pulsando una tarjeta del menú real.
- Guardado real AscentSaveSystem a disco, conversión V1, travel/respawn con progresión unificada, habilidades restauradas, multiplayer y paridad del ejecutable: PENDING.

### Advertencias adicionales a revisar antes de producción

cook_warnings.txt conserva el resumen. Destacan imports antiguos /Script/AscentLevelingSystem en ACF_RewardAction_BP y /Script/AscentGASIntegration en ACFCutDamageType; gameplay tags antiguos no registrados; y acceso a un array vacío en LevelBlock durante construcción. El cook no demuestra que las ramas afectadas funcionen. Se requiere rastrear si son dependencias activas y reemplazar rutas activas mediante código/assets propios, sin editar Marketplace.

El resolver del menú detecta dos WBP compatibles y elige WBP_ProjectInnerDoctrineExchangeMenuGlobal_C por prioridad. Antes de conectar compras conviene fijar la clase actual explícitamente en EFProjectUISettings, para que añadir otro WBP no cambie la selección. No se cambió esa configuración durante el piloto.

**Conclusión del piloto:** queda demostrada la compatibilidad de atributos Doctrine con compra y serialización nativas, con build de editor/juego y paquete real. No queda certificada una integración activada ni se han eliminado todos los riesgos de producción.

Las pruebas nuevas se ejecutan en mundos temporales registrados, sin utilizar SaveGameToSlot ni modificar partidas:

- NativePurchase: XP nativa, nivel, punto, compra Willpower, compras de los doce atributos y rechazos sin gasto.
- ExperienceGuards: XP negativa, curva ausente y recompensa grande que alcanza el límite configurado.
- NativeSaveRoundTrip: tabla extendida temporal, OnComponentSaved, archivo SaveGame en memoria, nuevo personaje, OnComponentLoaded y segunda carga sin acumulación.
- CurveAdapterParity: comparación de los 17 modificadores de ambos efectos, en cuatro rangos.

La tabla de serialización del test se inyecta por reflexión sólo dentro del banco de pruebas. Producción necesita una tabla propia persistente configurada y cocinada; no debe copiar esta inyección de campos privados.

La prueba de archivo en memoria demuestra el contrato de serialización del componente. No certifica selección de componentes por AscentSaveSystem, carga desde disco, orden de respawn/travel, equipo, habilidades ni conversión de partidas V1.

## Condiciones para activar la unificación

- Conectar el set al ASC existente antes de inicializar/cargar, con registro idempotente y sin segundo ASC.
- Un único proveedor de rangos para Doctrine y su UI. Retirar la autoridad de AttributeLevels; evitar sincronizar indefinidamente dos fuentes.
- Migración de partida versionada, copia de seguridad y marcador de aplicación única; decidir conversión de DXP/Meta y progreso al morir antes de tocar partidas reales.
- Centralizar recompensas: auditar muerte nativa, NotifyDamageDealt, entrenamiento, misiones, afinidades y recompensas por hit. El puente actual no deduplica eventos.
- Mantener SkillPoints nativos diferenciados de Perks, con asignación por nivel explícita y persistida, salvo decisión de diseño contraria.
- Hitos 5/10 ligados a rango invertido; evitar activarlos de forma permanente con equipo.
- Sustituir StartingEffects de forma controlada y verificar que reinitializar no duplica efectos.
- Integrar Willpower con necesidades sin alterar la regla actual: necesidades conservan proporción al cambiar máximo; sensaciones conservan valor absoluto; Curse sigue limitada a 100.
- Añadir después sinergias Constitution/Pain, Endurance/sudor e Intelligence+Cunning/lockpicking, con límites y curvas configurables y regresiones.
- Paridad visual de Comma, Inner Doctrine y atributos nativos, en PIE y ejecutable real.
- Repetir build, Blueprint compile, cook, empaquetado y carga de partida real. No extrapolar un cook mínimo al HUB completo.

## Evidencia

Logs y recibos de esta sesión: Saved/Analysis/ProgressionPilot. Las dos primeras ejecuciones del piloto fallaron por preparación incorrecta del mundo/componente GAS; se conservaron los logs y se corrigió el banco de pruebas. No fueron fallos de partidas reales.

invariants.json compara 403 archivos locales con la referencia del análisis anterior y registra la comparación de plugins con manifiestos históricos. ACF difiere del manifiesto de julio en numerosos archivos; esa comparación no permite atribuir esos cambios a esta sesión. Daz coincide con su manifiesto histórico. No sustituir esos manifiestos ni declarar PASS del ACF histórico.

### Incidente de autoguardado y protección incorporada

La comprobación final encontró un cambio de hash en Saved/SaveGames/ProjectIntimacy.sav después de PIE. ProjectIntimacySubsystem::Deinitialize llama SavePersistentState y éste usa SaveGameToSlot; abrir/cerrar el mundo puede reescribir esta partida aunque la captura no compre ni guarde explícitamente.

Hash previo: 2044bac138b1ec49d51faa6f0adfa3e98557a923735ef8cf127a8a15ca0f197c. Se buscaron copias existentes con ese hash, sin coincidencias. No se restauró una copia diferente ni se puede afirmar equivalencia del contenido de juego. El archivo actual se conserva; el cambio de bytes no demuestra por sí mismo pérdida de progreso.

Se añadió Progression-SaveGuard.ps1 a ambos runners de Automation y PIE. Copia los .sav antes del lanzamiento, conserva separadamente cualquier salida modificada/nueva, restaura los originales al terminar y verifica SHA256. Rechaza restaurar mientras siga abierto un editor. Test-ProgressionSaveGuard58.ps1 verificó restauración de un archivo modificado y aislamiento de uno generado, sin usar partidas reales. La protección evita repetir el problema; no recupera retroactivamente los bytes previos de las primeras pruebas.

Se repitió la captura PIE con el guard activo: detectó una nueva reescritura de ProjectIntimacy.sav, conservó la salida de QA y restauró todos los .sav a los hashes anteriores a esa repetición. Recibo duradero: Evidence/ProgressionPilot_20260924/guarded_pie_save_receipt.json. La referencia inicial sigue mostrando una única diferencia, la partida reescrita antes de incorporar la protección.
