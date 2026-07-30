# HUD Theme Rework — UE 5.8

## Estado

Este documento registra el rework de color del HUD propio de
`NoShellForWinter`. La implementación y sus gates finales todavía están en
curso. Ningún gate de build, Blueprint, PIE, visual, cook o paquete debe
interpretarse como aprobado hasta que aparezca aquí con evidencia final.

Proyecto fuente, estrictamente de solo lectura:

```text
D:\Projects UE5\LustAsDeadlySin
```

Proyecto target, único proyecto autorizado para escritura:

```text
D:\Projects UE5\NoShellForWinter
```

## Objetivo y alcance

El objetivo es que la interfaz propia del proyecto comparta un contrato de
color único y pueda cambiar de tema sin parpadeos, flashes de la paleta
anterior ni reconstrucciones visibles. Los cinco perfiles requeridos son:

- `Red`
- `Blue`
- `Purple`
- `Green`
- `Black`

El modo `Auto` resuelve el perfil desde la identidad del personaje:

| Identidad | Perfil resuelto |
|---|---|
| Male | `Blue` |
| Female | `Purple` |
| Unknown / no resuelta | `Black` |

El alcance incluye el HUD de Needs & Status abierto con `Comma`, altar e Inner
Doctrine, Story Selection, menú `Y` de acciones/emotes/interacciones, menú de
debug `L`, sistema de días, Chronicle, Inner State, Attributes y demás UI propia
de los plugins EF o del proyecto. También se incluye el chrome propio de
Defeat/Struggle identificado durante el inventario.

ACF Ultimate, su HUD y sus recursos Marketplace quedan fuera del rework. El
trabajo no autoriza modificar ACFU, DazToUnreal, Engine Content ni el proyecto
fuente.

## Contrato de no-layout

Este rework cambia exclusivamente el sistema visual de color y sus recursos.
Debe conservar:

- jerarquía y estructura actuales de cada widget;
- anchors, offsets, padding, tamaños y alineación;
- navegación, foco, animaciones y timings;
- bindings, delegates, inputs y contratos Blueprint públicos;
- contenido, textos y lógica gameplay.

No se permite resolver el problema con una capa superpuesta, con un tint global
encima de la textura morada ni reemplazando brushes por un `WhiteBrush`.

## Evidencia del video

Fuente analizada:

```text
D:\VIDEOS\2026-07-25 22-07-48.mp4
SHA-256: b2ec14e60a91138c9c6331ff3cd11b9e669f78d463bb3731ea1f0f72cddc7e4f
H.264, 1920x1080, 60 fps, 1702 frames, 28.367 s
```

El análisis fue de solo lectura y quedó materializado en:

```text
Saved/HudThemeRework/VideoAnalysis/video_analysis_report.md
Saved/HudThemeRework/VideoAnalysis/analysis_summary.json
Saved/HudThemeRework/VideoAnalysis/detected_runs.json
Saved/HudThemeRework/VideoAnalysis/frame_metrics.csv
Saved/HudThemeRework/VideoAnalysis/metadata.json
```

Evidencia visual principal:

```text
Saved/HudThemeRework/VideoAnalysis/evidence/flicker_inner_state_5.083_vs_5.100.png
Saved/HudThemeRework/VideoAnalysis/evidence/chronicle_toggle_purple_flash_5.800_to_5.950.png
Saved/HudThemeRework/VideoAnalysis/evidence/palette_mismatch_composite.png
Saved/HudThemeRework/VideoAnalysis/evidence/hud_state_timeline.png
```

Hallazgos registrados:

- `INNER STATE` alterna entre una capa roja/granate y su base negra. El cambio
  más claro ocurre entre los frames 305 y 306, separados por 16.7 ms.
- Se detectaron 192 cambios rojo/negro, con un periodo mediano de `0.200 s`
  (`5 Hz`).
- `CHRONICLE` vuelve a mostrar una fila morada durante creación,
  expand/collapse y otras transiciones antes de recuperar el estilo rojo.
- `INNER DOCTRINE` mezcla shell granate con tabs morados.
- Los tags `ENEMY` de Chronicle permanecen magenta.
- `DAY 1` combina cuerpo negro, borde violeta/azul, acento dorado y barras
  blancas, fuera de una variante coherente.

El video no demuestra todavía el cambio entre los cinco temas, la resolución
por identidad, el selector en `L`, Story Selection, el menú `Y`, cook ni
ejecución empaquetada. Esos resultados permanecen `PENDING`.

## Causa raíz confirmada

La inspección de la implementación anterior de
`UEFProjectDynamicThemeSubsystem` confirmó la causa exacta del parpadeo:

1. Un refresco periódico con intervalo de `0.2 s` recorría los
   `UUserWidget` vivos.
2. El recorrido reestilizaba indiscriminadamente imágenes, bordes y otros
   controles.
3. Para simular el color seleccionado sustituía brushes existentes por un
   `WhiteBrush` y aplicaba un tint.
4. Los propios `NativeConstruct`, `Refresh` y estados/transiciones de los
   widgets volvían a asignar el brush o color original, incluido el arte
   morado/rosado horneado.
5. En el siguiente pulso de `0.2 s` el subsistema imponía de nuevo el
   reemplazo.

La alternancia entre esas dos fuentes de verdad coincide con la cadencia de
`5 Hz` medida en el video. El problema no era un efecto de iluminación ni
movimiento de cámara: era una disputa periódica entre el retheme global y el
refresco nativo de cada widget.

## Arquitectura del rework

La nueva arquitectura es event-driven y mantiene una sola resolución de tema
por revisión:

```text
Identidad o selección manual
        -> resolución Auto/preset
        -> perfil semántico completo
        -> revisión de tema
        -> widgets registrados + recursos nativos del mismo preset
```

Principios:

- No hay polling ni retheme periódico.
- Un cambio se emite solo cuando el perfil resuelto realmente cambia.
- Cada widget propio se registra al construirse y recibe la revisión vigente
  después de completar su `NativeConstruct`.
- Los refrescos posteriores vuelven a resolver la textura del preset activo;
  no restauran silenciosamente el recurso horneado original.
- Paneles, outlines, haze, títulos, textos, badges, estados positivos/negativos
  y acentos usan roles semánticos del perfil, no constantes moradas por
  sistema.
- Los recursos de los cinco presets se precargan para que el primer cambio no
  introduzca hitch ni un frame con la paleta anterior.
- Si un recurso no tiene variante nativa, se conserva el original y se aplica
  únicamente el rol semántico autorizado.
- Los brushes que sí resuelven una variante nativa conservan su alfa y no
  reciben una segunda colorización cromática.

La implementación reside en código propio del proyecto:

```text
Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectUITheme.h
Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectUITheme.cpp
Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectUISettings.h
Plugins/EFProjectSystems/Source/EFProjectSystemsUI/EFProjectDynamicThemeSubsystem.h
Plugins/EFProjectSystems/Source/EFProjectSystemsUI/EFProjectDynamicThemeSubsystem.cpp
Plugins/EFProjectSystems/Source/EFProjectSystemsUI/EFProjectThemedUserWidget.h
Plugins/EFProjectSystems/Source/EFProjectSystemsUI/EFProjectThemedUserWidget.cpp
```

## Perfiles de color

Cada preset es un perfil completo, no un único color de acento. Los roles
incluyen como mínimo:

```text
PanelFill, PanelFillDeep, SectionFill
Outline, OutlineDim, Haze
TitleText, PrimaryText, SecondaryText, MutedText
Accent, AccentSoft, AccentMuted
Warning, BadgeFill, BadgeText
Positive, Negative
```

`Purple` es el perfil por defecto del HUD. La selección manual tiene prioridad
mientras esté activa. Al volver a `Auto`, el tema se resuelve nuevamente desde
la identidad sin reconstruir la interfaz.

## Inventario de texturas

La clasificación live/read-only está guardada en:

```text
Saved/HudThemeRework/UIAssetAudit/ui_texture_classification.json
Saved/HudThemeRework/UIAssetAudit/ui_texture_classification.md
```

| Familia | Auditadas | Color baked | Neutral / mask-candidate |
|---|---:|---:|---:|
| Attributes | 13 | 1 | 12 |
| Chronicle | 7 | 7 | 0 |
| Inner State | 11 | 4 | 7 |
| Inner Doctrine / Altar | 23 | 8 | 15 |
| Action Menu (`Y`) | 11 | 11 | 0 |
| Defeat / Struggle | 9 | 7 | 2 |
| **Total** | **74** | **38** | **36** |

Las 38 texturas con color horneado requieren cinco variantes nativas:

```text
38 texturas x 5 perfiles = 190 variantes planeadas
```

Las 36 candidatas neutrales no se duplican por defecto. La etiqueta
`neutral_mask_candidate` no equivale a una máscara validada: source, alfa,
sRGB y compresión deben verificarse antes de cambiar cualquier setting.

## Ruta y pipeline de recursos nativos

Destino único dentro del target:

```text
/Game/_Game/Textures/UI/Themes/<Theme>/<ruta original relativa a /Game>/<AssetName>
```

Ejemplo reversible:

```text
/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame
  ->
/Game/_Game/Textures/UI/Themes/Blue/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame
```

Los PNG y manifiestos intermedios permanecen bajo
`Saved/HudThemeRework`; no se escriben directamente en `Content`.

Herramientas:

```text
Tools/HudTheme/HudThemePipeline.json
Tools/HudTheme/hud_theme_common.py
Tools/HudTheme/Export-HudThemeSources.py
Tools/HudTheme/Generate-HudThemeVariants.py
Tools/HudTheme/Validate-HudThemeVariants.py
Tools/HudTheme/Import-HudThemeVariants.py
Tools/HudTheme/Run-HudThemeOfflinePipeline.ps1
Tools/HudTheme/README.md
```

El pipeline conserva dimensiones, alfa, silueta y luminancia; sustituye el
croma mediante un perfil explícito por tema. La importación se limita a
`/Game/_Game/Textures/UI/Themes`, preserva los settings del `Texture2D`
original, usa metadata de ownership y rechaza sobrescribir recursos no
administrados por el pipeline.

La exportación, generación de las 190 variantes, revisión visual e importación
real permanecen `PENDING`.

## Cobertura funcional prevista

| Sistema | Contrato de tema | Gate actual |
|---|---|---|
| Needs & Status (`Comma`) | Todos los paneles, estados, barras, tags y textos resuelven el mismo preset | `PENDING` |
| Inner State | Sin alternancia a 5 Hz; refresco de datos no toca el tema | `PENDING` |
| Chronicle (`J`) | Shell, filas, badges, tags y expand/collapse atómicos | `PENDING` |
| Inner Doctrine / Attributes | Cards, tabs, iconos, estados y fullscreen coherentes | `PENDING` |
| Altar | Mismo preset antes, durante y después de abrir/cerrar | `PENDING` |
| Action/Emote/Interaction (`Y`) | Chrome e iconos baked resuelven variante nativa | `PENDING` |
| Gameplay Debug (`L`) | Selector Auto/Red/Blue/Purple/Green/Black sin cerrar el menú | `PENDING` |
| Day system | Morning/Afternoon/Night usan roles del preset | `PENDING` |
| Story Selection | Chrome propio incluido sin cambiar estructura | `PENDING` |
| Defeat/Struggle | Paneles, flechas, rings y pulsos usan pack nativo | `PENDING` |
| Character Creation y demás EF/custom | Registro event-driven y roles semánticos | `PENDING` |
| HUD ACFU/Marketplace | Fuera de alcance; debe quedar inalterado | `PENDING` |

## Prueba desde el menú `L`

El menú de gameplay debug incorpora el apartado:

```text
Appearance
  -> HUD Theme
     -> Auto
     -> Red
     -> Blue
     -> Purple
     -> Green
     -> Black
```

La opción activa debe mostrarse como seleccionada. Cambiar de opción debe
mantener abierto el menú para comparar perfiles, actualizar todos los widgets
visibles en la misma revisión y no mostrar durante un frame la textura del
tema anterior. `Auto` debe permitir verificar la asignación Male=`Blue` y
Female=`Purple`.

También existe el contrato de consola:

```text
EF.UI.ThemePreset Auto|Red|Blue|Purple|Green|Black
```

La existencia en código no sustituye la prueba visible; navegación, foco,
selección y propagación siguen `PENDING` hasta PIE.

## Baseline protegido

El snapshot inicial se registró antes de completar el rework:

```text
Saved/HudThemeRework/Baseline_ProtectedInvariants.json
Baseline de referencia:
Docs/Migration/Evidence/Phase0_Target_Invariant_Hashes.json
Resultado inicial: FAIL
Mismatches iniciales: 74
```

El `FAIL` es preexistente y no puede atribuirse a este rework sin una
comparación de delta. El baseline contiene:

- ACFU 4.3.5: 5043 archivos, 0 mismatches en el snapshot.
- DazToUnreal 5.8.0.491 de Engine: 213 archivos, 0 mismatches en el snapshot.
- `Content/DazToUnreal`: 70 mismatches y un conteo actual de 190 frente a 189.
- Los assets autoritativos `Player`, `Female`, `Multiple` y `Male` ya diferían
  del baseline de referencia.

Al finalizar se debe volver a calcular el mismo conjunto y comparar el delta
exacto. El criterio del rework es no agregar mismatches fuera del árbol nuevo
de temas autorizado. Frederick y la asignación visible de Female requieren
comprobación runtime separada.

## Matriz de gates

| Gate | Evidencia requerida | Estado |
|---|---|---|
| Video forense | Reporte, métricas y capturas bajo `Saved/HudThemeRework/VideoAnalysis` | `RECORDED` |
| Inventario de UI | Clasificación exacta 74 = 38 baked + 36 neutral | `RECORDED` |
| C++/UHT | Build cold de UE 5.8 sin errores del rework | `PENDING` |
| Automation tests | Perfiles, parsing, Auto/identidad, path reversible y revisiones | `PENDING` |
| Export de texturas | Manifest de 38 sources con hashes y settings | `PENDING` |
| Generación/validación | 190 PNG; alfa, dimensiones, luminancia, perfil y rutas válidos | `PENDING` |
| Importación | 190 `Texture2D` administrados bajo el destino único | `PENDING` |
| Blueprint compile | Todos los WBP propios afectados compilan sin error | `PENDING` |
| PIE funcional | `Comma`, `J`, `Y`, `L`, altar, Story Selection y day system | `PENDING` |
| Visual QA | Cinco perfiles; transitions/hover/selected/expanded/collapsed sin flash | `PENDING` |
| Prueba de flicker | Inner State estable; no alternancia periódica a 5 Hz | `PENDING` |
| Auto por identidad | Male=`Blue`, Female=`Purple`, fallback=`Black` | `PENDING` |
| Cook | Cook target UE 5.8 completo con los cinco packs | `PENDING` |
| Package | Build empaquetado inicia y conserva cambios seamless | `PENDING` |
| Invariantes finales | Re-hash y delta contra los 74 mismatches preexistentes | `PENDING` |
| Fuente read-only | HEAD/status/hash final idénticos al snapshot inicial | `PENDING` |

## Criterio de cierre

El rework solo puede declararse completo cuando todos los gates marcados
`PENDING` tengan evidencia reproducible. En particular, un build correcto no
equivale a éxito visual, y una prueba en PIE no reemplaza cook ni validación
empaquetada.
