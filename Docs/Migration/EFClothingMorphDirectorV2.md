# EF Clothing Morph V2 - Director unico de prendas

La unica superficie publica de autoria es:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

El array **Clothing Catalog** funciona como el Director de Calysto. Cada elemento
es una prenda completa y **Garment Index / ID** es su indice estable. Mesh,
cuerpo, ajuste, offset, cobertura y exclusiones viven en el mismo elemento. Ya
no existen DataTables publicas paralelas.

## Uso cotidiano

1. Abre `DA_EFClothingMorphDirector`.
2. Expande **Clothing Catalog**.
3. Abre el indice deseado, por ejemplo `UnderWearPanty_Female`.
4. Activa **Enable This Garment Offset**.
5. Ajusta **Extra Surface Offset (cm)**. Empieza con `0.05 cm` y aumenta en
   pasos pequenos.
6. Guarda el Director y reinicia PIE o reequipa la prenda.

El offset se lee en runtime, despues de animacion y morphs. No cambia la mesh,
no invalida el binding y no requiere **Compile All Garments**. Un valor fuera del
rango se limita de forma segura; no hace desaparecer la prenda.

La separacion efectiva es:

`clamp(Global Offset + Garment Offset + Component/API Offset, 0, 0.35 cm)`

Tanto **Enable Runtime Offsets** como **Enable This Garment Offset** deben estar
activos para aplicar el valor de esa prenda.

## Opciones por indice

- **Garment Index / ID**: identidad unica y estable.
- **Enabled / Consider As Clothing**: incluye esa mesh en la V2.
- **Clothing Mesh (Source)**: mesh original usada por inventario/ACF.
- **Body Surface**: cuerpo DAZ exacto, Female ahora y Male mediante otra entrada.
- **Backend**: usa `Surface Wrap GPU` para el ajuste seguro en movimiento.
- **Fit Policy**: `Auto` es el valor recomendado; Tight/Hybrid/Loose/Rigid son
  overrides avanzados.
- **Extra Surface Offset (cm)**: ajuste outward continuo de runtime.
- **Coverage Tags / Hidden Body Material Slots**: cobertura y ocultacion visual.
- **Advanced Surface Exclusions**: elimina slots, ramas oseas o morphs de la
  proyeccion matematica.

`Geometry Fit Fallback` conserva el comportamiento legado, pero no promete la
misma garantia dinamica que `Surface Wrap GPU`.

Para Golden Palace u otra anatomia auxiliar, **Hidden Body Material Slots** la
oculta visualmente y **Excluded Body Surface Material Slots** la excluye de la
geometria usada para proyectar la prenda. Todo slot excluido de la superficie
debe estar tambien oculto.

## Agregar una prenda

1. Agrega un elemento a **Clothing Catalog**.
2. Escribe un **Garment Index / ID** unico.
3. Selecciona siempre la mesh original en **Clothing Mesh (Source)**; nunca una
   `SK_...` generada por EF Clothing Morph.
4. Asigna el **Body Surface** correcto.
5. Deja `Backend = Surface Wrap GPU`, `Fit Policy = Auto` y offset `0.00 cm`.
6. Configura coverage/exclusiones si son necesarias.
7. Guarda, cierra Unreal y ejecuta:

```powershell
Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.ps1
```

Los cambios estructurales -mesh, body, backend, fit, cobertura matematica o
exclusiones- requieren recompilar. El registry se publica de forma atomica solo
si todas las entradas habilitadas compilan y validan; no existe publicacion
individual de una fila.

## Por que existe una mesh interna adicional

V2 nunca modifica la prenda original, Female, Male, Multiple ni el skeleton
compartido. Por eso genera una Skeletal Mesh derivada con el sculpt, pesos,
`EF_AutoFit`, morphs y bounds certificados. Al equipar usa la derivada; al
desequipar restaura la original.

Por cada prenda compilada existen internamente:

- una mesh derivada;
- un fit profile;
- un SurfaceBinding;
- y un registry compartido para todo el catalogo.

Son implementacion runtime, no catalogos ni archivos que deban editarse. Estan
ocultos bajo:

`/EFClothingMorph/_Internal/Compiled/V26`

No deben equiparse manualmente. El binding puede ser grande y permanece separado
para permitir streaming; incrustar bindings de cientos de prendas dentro del
Director haria su carga inicial innecesariamente pesada.

No uses **Skeletal Mesh Editor > Deform > Offset** sobre una fuente certificada.
Eso altera su geometria/render fingerprint y vuelve obsoleto el binding. El
control correcto es **Extra Surface Offset (cm)** en el Director.

## Estado de la migracion

El cutover schema 2 ya retiro las dos DataTables antiguas y el viejo root publico
`/Game/_Generated/EFClothingMorphV2`. En Content Browser debe verse solo
`DA_EFClothingMorphDirector` dentro de la carpeta publica de EF Clothing Morph.

La validacion read-only se ejecuta con Unreal cerrado:

```powershell
Tools\ClothingMorphV2\Validate-EFClothingMorphDirector58.ps1
```

Comprueba el Director, IDs, pares source/body, offsets efectivos, registry,
perfiles/bindings internos, receipt de compilacion y hashes protegidos.
