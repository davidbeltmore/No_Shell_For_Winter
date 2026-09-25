# Texto para copiar al siguiente chat

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

Investiga las advertencias relacionadas de cook: imports /Script/AscentLevelingSystem en ACF_RewardAction_BP y /Script/AscentGASIntegration en ACFCutDamageType. No asumas que una rama funciona porque el cook pasa. Fija explícitamente la clase de menú actual en EFProjectUISettings cuando conectes la UI; ahora el resolver elige entre dos WBP.

Comandos reproducibles están en el informe: Build-NoShellForWinterEditor58.ps1, Run-ProgressionPilot58.ps1, Build-NoShellForWinterGame58.ps1, Package-ProgressionPilot58.ps1 y Run-ProgressionVisual58.ps1. Usa Launch-NoShellForWinterEditor58.ps1 para los scripts Python. Los runners requieren cerrar sesiones de editor/commandlet. Respeta los recibos de Daz y no ejecutes el editor durante la exclusión temporal del build de juego.

Atención a partidas: la primera captura PIE autoguardó ProjectIntimacy.sav. Su hash anterior está en el informe; no se encontró copia idéntica y no se restauró otra partida arbitraria. No afirmar que todas las partidas quedaron byte-idénticas. Los runners ahora usan Progression-SaveGuard.ps1 para respaldar/restaurar .sav y conservar las salidas de QA; úsalo también en nuevas pruebas. No reutilices copias antiguas sin comparar contenido/hash.

No reviertas cambios ajenos en HUB, Chest ni documentación de limpieza. Revisa git status antes de actuar. Conserva los hashes protegidos; no escribas al proyecto origen. Haz cambios pequeños y valida build, Blueprint compile, PIE, capturas, guardado, cook y paquete. Marca lo no comprobado PENDING. Aprovecha el piloto existente: no reinicies la investigación desde cero ni presentes sus pruebas aisladas como una migración terminada.
