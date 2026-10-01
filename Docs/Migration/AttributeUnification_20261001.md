# Unificación visual de los doce atributos — 2026-10-01

Proyecto destino: `D:/Projects UE5/NoShellForWinter`. El proyecto de origen permaneció en solo lectura. Estado: editor, PIE, cook, paquete y ejecutable de práctica comprobados. La apertura visual de la pantalla ACF de subida de nivel queda `PENDING`.

## Datos y presentación

- Los cinco primarios (`Strength`, `Intelligence`, `Endurance`, `Constitution`, `Dexterity`) viven en el conjunto ACF y ahora aparecen primero en el altar.
- Los siete secundarios (`Willpower`, `Offensive`, `Defensive`, `Faith`, `Cunning`, `Celerity`, `Charisma`) viven en `ProjectDoctrineAttributeSet` y aparecen después. El orden W/S coincide con el orden visible.
- El altar conserva el widget convertido `/Game/_Game/Widgets/InnerDoctrineAltar/Global/WBP_ProjectInnerDoctrineExchangeRowsGlobal` y su apariencia. Lee el nivel GAS actual de los primarios y la instantánea Doctrine de los secundarios. Muestra el efecto configurado, el método `Y > Training`, y los hitos de nivel 5 y 10 o su ausencia explícita. Los primarios son de consulta en el altar: su rango se obtiene por práctica, sin compra Doctrine.
- La lista ACF compartida `/Game/FullSample/Integrations/SharedAssets/Attributes/ACF_Attributes_WB` y las instancias `/Game/FullSample/Integrations/SharedAssets/ACF_LevelUpSubMenu_WB` y `/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_PlayerAttributes_WB` tienen las doce referencias GAS. La captura Status aportada por el usuario y la verificación PIE mostraron los doce valores.
- `Faith` y `Celerity` usaban `Warning()` como color de atributo. La práctica habilitada no representa una advertencia ni un coste DXP; ahora ambos usan un acento neutro, y todas las etiquetas `Practice` y los bordes de icono reciben el mismo estilo neutro.

## Efectos y práctica auditados

| Atributo | Efecto configurado o límite actual | Entrenamiento |
|---|---|---|
| Strength | Curva ACF hacia `MeleeDamage` | Acondicionamiento físico |
| Intelligence | Bono de ganzúa del proyecto; sin otra conversión directa ACF configurada | Leer un libro |
| Endurance | Curvas ACF de Stamina máxima y regeneración; reduce el máximo de sudor | Resistencia |
| Constitution | Curva ACF de salud máxima; reduce el máximo de Pain | Acondicionamiento suave |
| Dexterity | Rango primario entrenable; sin conversión directa o pasiva configurada | Equilibrio |
| Willpower | Máximos de necesidades/sensaciones y resistencia a Curse; nivel 5 `Second Breath`, 10 `Unbroken Mind` | Meditar |
| Offensive | Daño no mágico; nivel 10 `Clean Finish` | Formas de ataque |
| Defensive | Reduce daño después de armadura; nivel 5 `Guard Recovery`, 10 `Pain Spike`/`Tactical Retreat` | Guardia |
| Faith | Daño/defensa de hechizos y resistencia a Curse; nivel 5 `Quiet Mind`, 10 `Sanctified Rest` | Rezar |
| Cunning | Ganzúas, forcejeos y resistencia a Curse; nivel 5 `Steady Hands`, 10 `Clean Getaway` | Practicar cerradura |
| Celerity | Rango GAS entrenable y guardable; la pasiva de locomoción está desactivada | Desplazamientos |
| Charisma | Progreso social y resistencia a Curse en acompañantes; nivel 10 `Private Social Access` | Ensayar conversación |

Fuentes: `Config/DefaultPlugins.ini`, `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Progression/ProjectPracticeSettings.cpp`, `InnerDoctrine/ProjectInnerDoctrineSettings.cpp`, `InnerDoctrine/ProjectInnerDoctrineComponent.cpp` y `InnerDoctrine/ProjectInnerDoctrineExchangeMenuWidget.cpp`. Las explicaciones del altar describen las conexiones presentes; no asignan un beneficio inventado a Dexterity o Celerity.

## Gates de esta fase

| Gate | Evidencia | Estado |
|---|---|---|
| Blueprint | Los tres widgets ACF modificados compilaron y se guardaron mediante Unreal MCP | PASS |
| PIE Status | Doce filas GAS visibles y valores primarios 13/24/14/18/10 en la captura del usuario y en PIE | PASS |
| PIE altar | Doce filas; primarios arriba, secundarios abajo; W/S sigue ese orden, detalles y niveles GAS visibles | PASS |
| Editor C++ | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; UBT `Result: Succeeded`, recibo Daz reparado | PASS |
| PIE color y orden | Faith y Celerity sin naranja; doce filas, primarios arriba y detalles del altar visibles tras la última compilación | PASS |
| Pantalla ACF de subida de nivel | Blueprint compartido y sus dos instancias: 12 referencias GAS leídas por Unreal MCP, compiladas y guardadas | PASS configuración; visual PIE `PENDING` |
| Game | `Tools/Migration/Build-NoShellForWinterGame58.ps1`; UBT `Result: Succeeded`, recibo Daz reparado | PASS |
| Cook/stage/IoStore/paquete | `Saved/Analysis/PracticeProgression/Package_20261001_132527/uat.log`, UAT exit 0; `WarningReport/warnings.md` sin novedades respecto a su línea base | PASS |
| Ejecutable empaquetado | `Saved/Analysis/PracticeProgression/PackagedQA_20261001_133021/receipt.json`: `passed=true`, exit 0, práctica y calendario en ejecución | PASS funcional; visual empaquetado del altar `PENDING` |
| Invariantes protegidos recientes | `Saved/Migration/Evidence/ProtectedInvariantVerification_AttributeUnification_Recent20261001.json`: 7642 hashes de 2026-09-30, cero diferencias; excluye `HUB.umap`, ya modificado al iniciar esta tarea | PASS |

La verificación contra la línea base histórica de fase 0 (`Saved/Migration/Evidence/ProtectedInvariantVerification_AttributeUnification_20261001.json`) falla porque ACFU, Daz y los activos autoritativos evolucionaron durante la migración anterior. Se compararon con la instantánea reciente de 2026-09-30: cero diferencias. El plugin Daz también coincidió con fase 0. Código, widgets y esta documentación: commit `d20cbc56`; esta nota de cierre se registra en un commit posterior. Los cambios ajenos ya presentes en `HUB.umap`, `Content/_Game/Chest` y otros archivos de migración se mantuvieron fuera de esta integración.
