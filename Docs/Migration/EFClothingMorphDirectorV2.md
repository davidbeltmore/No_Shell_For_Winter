# EF Clothing Morph V2 - Director unico de prendas

La unica superficie publica de autoria es:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

El array **Catalogo de prendas** funciona como el Director de Calysto. Cada elemento
es una prenda completa y **Indice / ID de la prenda** es su identidad estable. Mesh,
cuerpo, ajuste, offset, cobertura y exclusiones viven en el mismo elemento. Ya
no existen DataTables publicas paralelas.

## Uso cotidiano

1. Abre `DA_EFClothingMorphDirector`.
2. Expande **Catalogo de prendas**.
3. Abre el indice deseado, por ejemplo `UnderWearPanty_Female`.
4. Activa **Activar offset de esta prenda** dentro de ese mismo indice.
5. Ajusta **Offset extra hacia afuera (cm)**. Empieza con `0.05 cm` y aumenta en
   pasos pequenos.
6. Guarda el Director y reinicia PIE o reequipa la prenda.

El offset se lee en runtime, despues de animacion y morphs. No cambia la mesh,
no invalida el binding y no requiere **Compile All Garments**. Un valor fuera del
rango se limita de forma segura; no hace desaparecer la prenda.

La separacion autorada por indice es:

`clamp(Offset del indice seleccionado, 0, 0.35 cm)`

No existe un master offset ni un offset global en el Director. Cada prenda
controla exclusivamente su propio valor. Una llamada runtime explicita a
`SetGarmentClearanceOffsetCm` sustituye temporalmente el valor del indice para
ese componente; no se suma una segunda vez.

## Opciones por indice

- **Indice / ID de la prenda**: identidad unica y estable.
- **Usar como prenda**: incluye esa mesh en la V2.
- **Mesh original de la prenda**: mesh usada por inventario/ACF.
- **Cuerpo de referencia**: cuerpo DAZ exacto, Female ahora y Male mediante otra entrada.
- **Metodo de ajuste**: usa `Ajuste automatico GPU` para el movimiento.
- **Tipo de ajuste**: `Automatico` es el valor recomendado; los otros modos son
  excepciones avanzadas.
- **Activar offset de esta prenda**: activa el ajuste exclusivamente para este indice.
- **Offset extra hacia afuera (cm)**: separacion continua exclusiva de esta prenda.
- **Zonas que cubre / Partes del cuerpo a ocultar**: cobertura y ocultacion visual.
- **Opciones avanzadas**: excluye slots, ramas oseas o morphs de la
  proyeccion matematica.

`Geometry Fit Fallback` conserva el comportamiento legado, pero no promete la
misma garantia dinamica que `Surface Wrap GPU`.

Para Golden Palace u otra anatomia auxiliar, **Hidden Body Material Slots** la
oculta visualmente y **Superficies excluidas del ajuste** la excluye de la
geometria usada para proyectar la prenda. Todo slot excluido de la superficie
debe estar tambien oculto.

## Agregar una prenda

1. Agrega un elemento a **Catalogo de prendas**.
2. Escribe un **Indice / ID de la prenda** unico.
3. Selecciona siempre la mesh original en **Mesh original de la prenda**; nunca una
   `SK_...` generada por EF Clothing Morph.
4. Asigna el **Cuerpo de referencia** correcto.
5. Deja **Metodo de ajuste** y **Tipo de ajuste** en sus valores recomendados, con offset `0.00 cm`.
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
control correcto es **Offset extra hacia afuera (cm)** en el Director.

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
