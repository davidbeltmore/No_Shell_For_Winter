# Pipeline de texturas nativas para el HUD

Este pipeline crea cinco familias nativas para las 74 texturas de chrome del
HUD auditadas: `Red`, `Blue`, `Purple`, `Green` y `Black`. Incluye tanto el
arte cuyo morado/rosado estaba horneado como las antiguas candidatas a máscara,
porque la QA en PIE demostró que dejar estas últimas fuera permitía volver al
source morado durante cambios de tema. No aplica overlays. Cada PNG generado
es un recurso nativo que puede asignarse como `ResourceObject` de un brush.

La ruta de destino es reversible y conserva el nombre y la ruta originales
relativos a `/Game`:

```text
/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame
  ->
/Game/_Game/Textures/UI/Themes/Blue/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame
```

## Alcance

El escaneo se limita a contenido propio del proyecto:

- `/Game/_Game/Widgets`
- `/Game/_Game/Icons`
- `/Game/UI/ActionMenu`
- `/Game/UI/Emote/Textures` (ruta real actual del menú Y)
- `/Game/UI/Defeat/Struggle/Textures`
- duplicados de `/Game/UI/InnerDoctrine`, `InnerState` y `Altar`
- iconos de `/Game/UI/Survival`
- rutas custom opcionales de Story Selection y Chronicle

Después del escaneo, `native_texture_packages` actúa como una lista positiva
cerrada. Contiene exactamente las 74 texturas registradas en
`Saved/HudThemeRework/UIAssetAudit/ui_texture_classification.json`: 38
clasificadas originalmente como `baked` y 36 como
`neutral_mask_candidate`. La promoción de las 36 candidatas es deliberada:
garantiza cobertura nativa completa y elimina el fallback visual a recursos
morados. La lista sigue excluyendo retratos, capturas e iconografía ajena al
chrome del HUD.

No se escanea ni modifica ACFU, DazToUnreal, plugins Marketplace, Engine Content
ni el proyecto fuente `LustAsDeadlySin`.

## Contrato visual

El generador usa Pillow y un perfil explícito de tres tonos por tema. Para cada
pixel:

- conserva exactamente el ancho y alto;
- copia el canal alfa byte por byte;
- conserva exactamente la máscara/silueta de alfa;
- conserva exactamente el canal de luminancia `L` calculado por Pillow;
- sustituye únicamente el croma por el perfil Red, Blue, Purple, Green o Black;
- guarda un PNG RGBA determinista sin metadatos variables.

El tema `Black` usa grafito y plata, no negro plano, para conservar contraste y
legibilidad.

El tamaño fuente se obtiene del `IHDR` del PNG exportado y debe coincidir con
el tag `Dimensions` (que en UE llama a `GetImportedSize`). No se usa
`Blueprint_GetSizeX/Y`: para texturas virtualizadas ese API puede reflejar un
mip runtime provisional de `32×32` en vez del source art.

## Archivos

- `HudThemePipeline.json`: raíces, destino y cinco perfiles de color.
- `Export-HudThemeSources.py`: inventario/exportación read-only dentro de UE 5.8.
- `Generate-HudThemeVariants.py`: generación offline con Pillow.
- `Validate-HudThemeVariants.py`: validación offline exhaustiva.
- `Import-HudThemeVariants.py`: importación protegida dentro de UE 5.8.
- `DryRun-HudThemeImport.py`: entrada segura de command line para el dry run.
- `Run-HudThemeOfflinePipeline.ps1`: ejecuta solo generación y validación; nunca importa.

Todos los PNG y manifiestos intermedios se escriben bajo
`Saved/HudThemeRework`, no dentro de `Content`.

## Ejecución por fases

### 1. Exportar desde el editor target

Con `NoShellForWinter` abierto en UE 5.8 y sin PIE, ejecutar desde el Output Log:

```text
py "D:/Projects UE5/NoShellForWinter/Tools/HudTheme/Export-HudThemeSources.py"
```

El exportador solo lee `Texture2D` y escribe:

```text
Saved/HudThemeRework/SourceTextures/
Saved/HudThemeRework/HudThemeSourceManifest.json
```

Una raíz opcional sin assets queda registrada; una raíz requerida vacía hace
fallar la fase.

### 2. Generar y validar offline

En PowerShell:

```powershell
python -m pip install -r "Tools/HudTheme/requirements.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "Tools/HudTheme/Run-HudThemeOfflinePipeline.ps1"
```

O ejecutar las fases por separado:

```powershell
python "Tools/HudTheme/Generate-HudThemeVariants.py" --self-test
python "Tools/HudTheme/Generate-HudThemeVariants.py" --project-root "D:\Projects UE5\NoShellForWinter"
python "Tools/HudTheme/Validate-HudThemeVariants.py" --project-root "D:\Projects UE5\NoShellForWinter"
```

La validación debe producir `HudThemeVariantValidation.json` con `status:
PASS` y exactamente `74 x 5 = 370` variantes. Compara hashes, dimensiones,
modo RGBA, alfa, luminancia, silueta, AssetName, perfiles y ruta reversible de
todas las variantes.

### 3. Revisar antes de importar

Los resultados se encuentran en:

```text
Saved/HudThemeRework/Generated/<Theme>/<ruta original relativa a Game>.png
```

Conviene revisar al menos un frame, panel, divisor, icono y textura con alfa de
cada familia antes de crear `.uasset`.

### 4. Dry run de importación

Dentro del editor, ejecutar el wrapper de dry run, que no cambia assets:

```text
py "D:/Projects UE5/NoShellForWinter/Tools/HudTheme/DryRun-HudThemeImport.py"
```

`Import-HudThemeVariants.py --dry-run --theme Blue` sigue disponible para una
sesión interactiva que necesite revisar un solo tema.

### 5. Importación real

Solo después de revisar el manifiesto y el dry run:

```text
py "D:/Projects UE5/NoShellForWinter/Tools/HudTheme/Import-HudThemeVariants.py"
```

El importador:

- exige que exportación, generación y validación sigan teniendo los mismos hashes;
- verifica también el SHA-256 del `.uasset` fuente antes de generar, validar
  o importar, para impedir variantes obsoletas tras un cambio del arte original;
- importa únicamente bajo `/Game/_Game/Textures/UI/Themes`;
- conserva exactamente el `AssetName`;
- copia del Texture2D original sus settings de compresión, sRGB, filtro,
  mipmaps y direccionamiento;
- aplica a las variantes —sin tocar los originales— `TEXTUREGROUP_UI`,
  `NeverStream=true` y virtual-texture streaming desactivado, para que los
  cinco packs precargados permanezcan residentes y el cambio no muestre mips
  borrosos ni provoque carga tardía;
- etiqueta cada asset con ownership y huella del pipeline;
- reimporta de forma deliberada cada variante administrada ya existente, de
  modo que una edición manual no pueda quedar oculta por etiquetas antiguas;
- actualiza solo assets ya administrados por este pipeline;
- rechaza sobrescribir cualquier asset existente sin la etiqueta de ownership;
- nunca elimina, mueve ni renombra assets.

## Idempotencia

La exportación conserva PNG existentes cuando su hash no cambia. La generación
es determinista y solo reemplaza bytes diferentes. La importación produce el
mismo contenido validado en cada ejecución, pero vuelve a escribir las
variantes administradas para reparar cualquier deriva manual que las etiquetas
por sí solas no podrían detectar. Los manifiestos se escriben únicamente
cuando cambia su contenido.

Los PNG huérfanos de ejecuciones antiguas se reportan como warning y no se
eliminan automáticamente.

## Riesgos que requieren QA visual

- El método conserva luminancia y forma, pero reemplaza deliberadamente el
  color original. Un icono cuyo color tenga significado semántico puede
  necesitar exclusión o arte específico tras la revisión.
- La compresión de UE puede introducir diferencias frente al PNG. Por eso el
  importador copia los settings del Texture2D original y aun así se necesita
  QA en PIE.
- El pipeline crea recursos; no cambia por sí solo referencias de Widgets ni
  hace el cambio atómico de tema. Esa integración pertenece al runtime del
  rework.
- No debe ejecutarse la importación durante PIE ni en paralelo con otro proceso
  que guarde Content.
