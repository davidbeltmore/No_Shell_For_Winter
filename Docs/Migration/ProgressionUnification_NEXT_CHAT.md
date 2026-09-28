# Texto para copiar al siguiente chat

> Actualización 2026-09-28: este texto describe el piloto histórico. Para continuar, usar [PracticeProgression_20260928.md](PracticeProgression_20260928.md), sus decisiones aprobadas y evidencia. Los atributos ahora se aprenden por práctica, los puntos de nivel son exclusivamente para habilidades y las partidas antiguas están respaldadas/archivadas. No volver a activar compra de atributos ni convertir DXP/Meta. Calysto queda fuera de las pruebas por petición del usuario.

Trabaja en D:\Projects UE5\NoShellForWinter (UE 5.8, ACF Ultimate instalado). LustAsDeadlySin es el origen de sólo lectura. Lee AGENTS.md y aplica noshellforwinter-unreal-mcp y survival-needs-sensations-workbench.

Quiero continuar la unificación de experiencia, nivel, puntos, atributos y habilidades sobre ACF, sin cambiar el diseño visual del HUD Inner State, Inner Doctrine ni los atributos nativos. Doctrine debe ser un conjunto adicional en el mismo ASC; no quiero otro ASC, otro nivel ni dos economías sincronizadas. Las necesidades mantienen su simulación y sus reglas; no se convierten en atributos comprables.

Lee primero:
1. Docs/Migration/ProgressionUnification_ACF_20260924.md.
2. Docs/Migration/ProgressionPilot_20260924.md.
3. Docs/Migration/Evidence/ProgressionPilot_20260924.json.

Ya hay código de piloto en Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Progression:
- UProjectDoctrineAttributeSet: Willpower, Offensive, Defensive, Faith, Cunning, Celerity y Charisma.
- FProjectProgressionBridge: compras mediante AssignPerkToAttribute y XP mediante AddExp del componente ACF existente, con validaciones previas.
No está activado en el jugador, no sustituye AttributeLevels de Doctrine y no cambia DXP/Meta del menú.

Pruebas confirmadas: 18 Automation PASS (14 existentes y 4 del piloto), build Editor y Game Development, cook de 5.304 paquetes con 0 errores/44 advertencias, stage/IoStore/package/archive correctos. La compra de Willpower, las compras de los doce atributos y el guardado/carga nativo del componente se probaron con objetos de prueba. La prueba de guardado usa un archivo SaveGame en memoria y una tabla transitoria ampliada; no certifica una partida real de AscentSaveSystem.

Las advertencias de curvas están diagnosticadas: nueve RowName huérfanos en coeficientes/sumandos FScalableFloat. Las curvas principales sí existen. Hay copias propias GE_ProjectAttDependencies_GE y GE_ProjectStatInit_GE en /Game/_Game/Data/Progression; eliminan sólo esos RowName. Pasan 68 comparaciones de magnitud respecto de los originales. No están conectadas aún a StartingEffects, por lo que el juego sigue usando los originales. No edites Marketplace ni inventes curvas para esas referencias.

El ejecutable del piloto arranca y registra ProjectDoctrineAttributeSet, pero el smoke con Entry/NullRHI tiene un error de quest por actor objetivo no resuelto y falta el material DirtyPawn M_DirtyPawn_DAZSkinWrapper en ese paquete de ensayo. No declares validación completa del ejecutable. Las capturas PIE actuales están en Saved/Analysis/ProgressionPilot/Visual: HUD abierto y menú abierto; el viewport estrecho recorta textos del menú. No se verificó paridad de una UI unificada.

Continúa con una integración vertical real de Willpower antes de activar los siete:
- Registro idempotente del set en el ASC existente, antes de inicialización y carga.
- Una sola fuente de rangos: getters de Doctrine consultan GAS; eventos actualizan pasivas/Needs/UI. Conservar porcentaje en necesidades y valor absoluto en sensaciones al cambiar máximos; Curse sigue en 100.
- Compra desde la tarjeta real con el saldo nativo, sin duplicar gasto ni conceder hitos permanentes por bonificaciones de equipo.
- Tabla propia de serialización ampliada/configurada/cocinada y prueba real de guardar, cerrar, cargar, respawn y travel.
- Reemplazo controlado de StartingEffects por las copias corregidas, sin duplicar efectos al reinicializar.
- Cubrir llamadas directas nativas a AddExp: el puente del piloto no intercepta recompensas de muerte ni deduplica hits, misiones o entrenamiento. Tampoco constituye seguridad completa de RPC.
- SkillPoints/Perks deben depender del mismo nivel ACF. La propuesta conserva dos tipos de puntos nativos; no es una decisión de balance aprobada. Validar compra, requisito, respec, guardado y reaplicación de habilidades.
- Diseñar migración V1 de DXP/Meta con copia y marcador de aplicación única. No se decidió conversión de DXP ni permanencia del progreso al morir; no convertir partidas reales con una regla inventada.

Después, extender los mismos contratos a los otros seis atributos y diseñar sinergias configurables Constitution/Pain, Endurance/sudor e Intelligence+Cunning/lockpicking. Mantén widgets y estilo; sólo cambia etiquetas necesarias al activar el backend.

También forma parte obligatoria del trabajo corregir todos los errores y advertencias detectados, preservando la lógica, los contratos Blueprint, el balance y el aspecto actuales. No basta con enumerarlos o conseguir que el cook termine. Investiga la causa, aplica una solución compatible y verifica las rutas afectadas antes de darlas por resueltas.

El cook registrado emitió 44 advertencias; varias son repeticiones o contexto del mismo problema. Lee el listado exacto en Docs/Migration/Evidence/ProgressionPilot_20260924/cook_warnings.txt y contrástalo con los logs completos del informe:

| Cantidad | Advertencia que debes resolver |
| ---: | --- |
| 2 | Imports antiguos: /Script/AscentLevelingSystem en ACF_RewardAction_BP y /Script/AscentGASIntegration en ACFCutDamageType. Verificar recompensas y daño reales. |
| 5 | Gameplay Tags no registrados: Ability.Reactive.Dodge, Ability.Reactive.Parry, Actions.LadderClimb.Exit, RPG.StatusEffect.Poisioned y Marker.Basic. Identificar productores/consumidores y equivalencias antes de registrar o redirigir etiquetas. |
| 1 | Colisión de GameplayCue.ACF.HitEffectCue entre ACF_DefaultReaction_GC y ACF_DefaultHit_GC. Preservar el feedback de golpes y las reacciones necesarias; no eliminar uno sin probar ambos comportamientos. |
| 2 | DataTables no encontradas: ACFJ_AbilityTag de TurnCombat y una referencia vacía. Identificar quién las solicita y si la función correspondiente debe estar activa. |
| 8 | Cuatro accesos a índice 0 de un array vacío en LevelBlock: cuatro avisos y cuatro líneas de contexto de LevelBlock_Traversable_C. Corregir la precondición sin anular la construcción prevista. |
| 2 | Avisos repetidos de DefaultSlot duplicado en ACF_Humanoid_ABP. Verificar montages y rutas de animación antes de modificar la integración. |
| 1 | Pin obsoleto In Font Size en SetFontSize de ANS_MapMarker_WBP. Conservar tamaño y aspecto de los marcadores. |
| 1 | Consulta al Asset Registry durante carga/guardado con bIncludeOnlyOnDiskAssets=false. Identificar el llamador y corregir la dependencia que introduce variación entre cooks. |
| 1 | GameplayCueNotifyPaths sin configurar: búsqueda en todo /Game. Definir rutas completas y comprobar que no desaparezcan cues necesarios. |
| 12 | Assets sin versión de motor registrada: materiales Daz, animaciones de escalera y Arrow. Comprobar compatibilidad; no hacer resaves masivos ni modificar assets Marketplace para ocultar el aviso. |
| 7 | Configuración antigua de ACF: BaseAscentCombatFramework.ini deprecado y seis redirects MatchSubstring. Conservar la resolución de rutas antiguas y evitar redirects duplicados o cambios incompatibles. |
| 1 | Conflicto de prioridad de r.Shadow.Virtual.Enable. Mantener el resultado visual actual y aclarar qué configuración debe controlarlo. |
| 1 | Timeout HTTP de una consulta externa de configuración. Identificar su plugin y tratar la disponibilidad externa; no retirar funciones ni desactivar el plugin arbitrariamente. |

Estas 44 advertencias son distintas de las nueve referencias huérfanas de curvas. Resolver también esas curvas en las rutas activas mediante las copias propias ya validadas, sin aplicar efectos dos veces.

Incluye los problemas encontrados fuera del resumen del cook: misión con actor objetivo no resuelto en el smoke Entry, material DirtyPawn ausente del paquete de ensayo, selección ambigua entre dos WBP del menú, recortes/solapamientos observados en el viewport estrecho y autoguardado incidental de ProjectIntimacy.sav. Distingue fallos del banco de pruebas de fallos del juego: usa un escenario de prueba apropiado sin desactivar misiones o sistemas reales para conseguir un PASS. Verifica la presencia y el funcionamiento del material en un ejecutable con renderizado.

Para conservar la UI, fija la clase actualmente elegida en EFProjectUISettings tras verificarla y compara capturas a igual resolución. Si corriges adaptación a tamaños de viewport, conserva diseño, tipografía, marcos, iconos, navegación y comportamiento.

No suprimas categorías de log, no bajes la severidad de los mensajes y no desactives funcionalidades para aparentar cero advertencias. No añadas redirects, tags o assets ficticios sin comprobar su semántica. Usa adaptadores, configuración, clases y assets propios: no modifiques directamente Engine, Marketplace ni el proyecto origen. Si una corrección depende del proveedor o de un servicio externo y no existe una solución compatible dentro de estas restricciones, documenta la evidencia y déjala explícitamente PENDING; no declares que quedó arreglada.

Mantén una matriz de cada incidencia con origen, causa, cambio aplicado, comportamiento que debe preservarse y prueba de regresión. Repite build, Blueprint compile, PIE, guardado/carga, cook y ejecución empaquetada; revisa también los errores/advertencias nuevos. Sólo cierra una incidencia cuando su causa esté resuelta y las funciones afectadas sigan funcionando. No asumas que una rama funciona porque el cook pasa.

Comandos reproducibles están en el informe: Build-NoShellForWinterEditor58.ps1, Run-ProgressionPilot58.ps1, Build-NoShellForWinterGame58.ps1, Package-ProgressionPilot58.ps1 y Run-ProgressionVisual58.ps1. Usa Launch-NoShellForWinterEditor58.ps1 para los scripts Python. Los runners requieren cerrar sesiones de editor/commandlet. Respeta los recibos de Daz y no ejecutes el editor durante la exclusión temporal del build de juego.

Atención a partidas: la primera captura PIE autoguardó ProjectIntimacy.sav. Su hash anterior está en el informe; no se encontró copia idéntica y no se restauró otra partida arbitraria. No afirmar que todas las partidas quedaron byte-idénticas. Los runners ahora usan Progression-SaveGuard.ps1 para respaldar/restaurar .sav y conservar las salidas de QA; úsalo también en nuevas pruebas. No reutilices copias antiguas sin comparar contenido/hash.

No reviertas cambios ajenos en HUB, Chest ni documentación de limpieza. Revisa git status antes de actuar. Conserva los hashes protegidos; no escribas al proyecto origen. Haz cambios pequeños y valida build, Blueprint compile, PIE, capturas, guardado, cook y paquete. Marca lo no comprobado PENDING. Aprovecha el piloto existente: no reinicies la investigación desde cero ni presentes sus pruebas aisladas como una migración terminada.
