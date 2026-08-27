# EF Clothing Morph V2 — Director de prendas

El control de V2 se divide deliberadamente en dos DataTables enlazadas por el
**nombre de fila**. Ese nombre es el índice estable de una prenda; por ejemplo,
`UnderWearPanty_Female`.

| Asset | Propósito | Cuándo usarlo |
| --- | --- | --- |
| `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector` | Punto de entrada tipo Director de Calysto. Expone ambos catálogos y el límite global seguro. | Abrir primero. |
| `/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments` | Registro estructural y de compilación. | Añadir una prenda nueva o cambiar source/body/coverage/policy. |
| `/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarmentTuning` | Ajuste runtime sin topología. | Ajustar `Extra Surface Offset (cm)` de una prenda ya certificada. |

## Ajustar offset sin romper V2

1. Abre `DA_EFClothingMorphDirector` y sigue los enlaces de catálogo.
2. En `DT_EFClothingGarmentTuning`, encuentra la fila con el mismo índice que
   la prenda; actualmente `UnderWearPanty_Female`.
3. Cambia **Extra Surface Offset (cm)** entre `0.00` y `0.35` y guarda.
4. Reinicia PIE o reequipa la prenda. No se necesita recompilar ni editar la mesh.

El offset se suma después de skinning/morphs en el deformer GPU y V2 reserva sus
bounds para todo el rango permitido; no puede desaparecer por culling debido a
ese ajuste.

## Añadir una prenda nueva

1. En `DT_EFClothingGarments`, agrega una fila cuyo nombre sea un índice único y
   estable, por ejemplo `F_010_Top_Leather_Female`.
2. Configura `SourceGarment`, `BodySurface`, estado Enabled y la política de fit.
   Los campos de exclusión son avanzados y sólo se usan si esa prenda requiere
   excluir una zona concreta de la superficie corporal.
3. Agrega una fila con **el mismo nombre** en
   `DT_EFClothingGarmentTuning`; déjala inicialmente en `0.00 cm`.
4. Cierra Unreal y ejecuta
   `Tools/ClothingMorphV2/Compile-EFClothingGarmentCatalog58.ps1`.
5. Reabre Unreal y prueba la prenda. El compilador publicará la mesh derivada,
   pesos, profile y binding sólo si el par fuente/cuerpo es válido.

## Regla importante

No uses **Skeletal Mesh Editor → Deform → Offset** sobre una mesh fuente ya
certificada. Eso modifica su geometría y V2 la ocultará fail-closed para evitar
usar un binding obsoleto. Si se necesita cambiar geometría, duplica la mesh a un
asset propio, registra la copia en el catálogo y recompila.

## Validación de esta entrega

- `Validate-EFClothingMorphDirector58.ps1`: PASS. Carga de forma read-only el
  Director, ambos catálogos, el registry V26 y el índice
  `UnderWearPanty_Female`; también verifica que no se modificaron Female, Male,
  Multiple, Player, la panty ni los nuevos assets de control.
- `Compile-EFClothingGarmentCatalog58.ps1`: PASS, con una fila habilitada, un
  profile y un binding V26 válidos.
- Builds Editor y Game Development: PASS.
- PIE V26: el guard de entrada ya evita que la mesh fuente de un slot inicial se
  dibuje mientras los assets V26 terminan de cargar. El readback posterior llegó
  al deformer/binding exactos; el gate geométrico estricto conserva un hallazgo
  previo de dos contactos triangulares tangenciales en Idle, aunque sus gaps y
  residuos certificados pasan. No se presenta como PASS ni se resuelve con un
  offset de catálogo.
- Cook/package Development: PENDING por un error preexistente de configuración
  de Game Features (`PrimaryAssetTypesToScan` no tiene `GameFeatureData`), ajeno
  a los assets del Director. El intento y log de UAT están bajo
  `Saved/Migration/CalystoDungeonDirectorV4/Packages/Development_20260827_025258`
  y `C:/Users/bigin/AppData/Roaming/Unreal Engine/AutomationTool/Logs/`.
