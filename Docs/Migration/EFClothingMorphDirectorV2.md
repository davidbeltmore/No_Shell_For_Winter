# EF Clothing Morph V2 - Director unico de prendas

La unica superficie publica de autoria es:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

Toda la interfaz visible del Director (categorias, campos, enums y tooltips) esta
en ingles. Esta guia esta escrita en espanol, pero conserva en **negrita** los
nombres ingleses exactos que aparecen en Unreal.

El array **Garment Catalog** funciona como el Director de Calysto. Cada elemento
es una prenda completa y **Garment Index / ID** es su identidad estable. Mesh,
cuerpo, ajuste, separacion, grosor, cobertura y exclusiones viven en el mismo elemento. Ya
no existen DataTables publicas paralelas.

## Uso cotidiano

1. Abre `DA_EFClothingMorphDirector`.
2. Expande **Garment Catalog**.
3. Abre el indice deseado, por ejemplo `UnderWearPanty_Female`.
4. Activa **Enable Runtime Clearance** dentro de ese mismo indice.
5. Ajusta **Additional Skin Clearance (cm)**. Empieza con `0.05 cm` y aumenta en
   pasos pequenos.
6. Para cambiar el grosor visible, ajusta **Visible Thickness (cm, Runtime)** en
   ese mismo indice. El cambio es runtime y exclusivo de esa prenda.
7. Guarda el Director. Ninguno de estos dos valores runtime exige recompilar.

La separacion y el grosor visible se leen en runtime, despues de animacion y morphs.
No cambian la topologia compilada,
no invalidan el binding y no requieren **Compile All Garments**. Un valor fuera del
rango se limita de forma segura; no hace desaparecer la prenda.

La separacion autorada por indice es:

`clamp(Separacion del indice seleccionado, 0, 0.35 cm)`

No existe un master offset ni un offset global en el Director. Cada prenda
controla exclusivamente su propio valor. Una llamada runtime explicita a
`SetGarmentClearanceOffsetCm` sustituye temporalmente el valor del indice para
ese componente; no se suma una segunda vez.

## Opciones por indice

- **Garment Index / ID**: identidad unica y estable.
- **Enable Garment**: incluye esa mesh en la V2.
- **Editable Source Garment Mesh**: unica mesh publica que debe editarse. Es la fuente
  usada por inventario/ACF y admite las herramientas nativas del Skeletal Mesh Editor.
- **Reference Body**: cuerpo DAZ exacto, Female ahora y Male mediante otra entrada.
- **Fit Backend**: usa `Automatic GPU Surface Wrap (Recommended)` para el movimiento.
- **Runtime Fit Policy**: `Automatic Region Classification (Recommended)` es el valor recomendado; los otros modos son
  excepciones avanzadas.
- **Enable Runtime Clearance**: activa la separacion runtime exclusivamente para este indice.
- **Additional Skin Clearance (cm)**: mueve la prenda completa; no crea volumen.
- **Enable Adjustable Thickness (One-Time Compile)**: genera una vez la topologia
  emparejada necesaria para controlar el grosor en runtime, sin tocar la fuente.
- **Visible Thickness (cm, Runtime)**: distancia visible entre la capa interior y la
  exterior. Cambiarla no recompila, no reconstruye y no oculta la prenda. `0.05 cm`
  es un inicio conservador para tela fina; `0.20 cm` produce un grosor mas visible.
- **Iterative Offset Steps**: iteraciones del metodo Iterative nativo; mas pasos siguen mejor las
  curvas, a costa de mas tiempo de compilacion.
- **Create Boundary Walls (Required)**: crea las paredes de cintura,
  mangas y aberturas. Debe permanecer activado para obtener una mesh cerrada certificada.
- **Smoothing Per Step / Reproject After Smoothing**: controles avanzados equivalentes al
  offset iterativo de Unreal. Conserva ambos en sus valores por defecto salvo que
  una prenda concreta necesite suavizar su capa exterior.

La capa interior, la capa exterior de referencia y las paredes se compilan una
sola vez cuando se activa **Enable Adjustable Thickness (One-Time Compile)**. A
partir de ahi, **Visible Thickness (cm, Runtime)** escala esa separacion por prenda
en el deformer runtime; no vuelve a generar assets ni invalida el binding.

Todo indice nuevo incluye estos controles y comienza con **Enable Garment** y
**Enable Adjustable Thickness (One-Time Compile)** desactivados. Completa primero
el ID, la mesh y el cuerpo;
activalos al final. Un elemento nuevo completamente vacio y desactivado se trata
como placeholder de autoria: no bloquea las prendas ya configuradas ni se publica
en el registry. Si empiezas a rellenarlo, asigna un ID estable antes de compilar.
Un ajuste estructural excesivo puede
auto-intersectar una prenda concava; en ese caso el compilador falla sin publicar
el resultado y debes ajustar la fuente o los controles estructurales.

El shell cambia la topologia de la derivada. Si la mesh original ya contiene un
mapeo de **Chaos Cloth**, deja **Enable Adjustable Thickness (One-Time Compile)**
desactivado hasta reconstruir ese mapeo para la topologia generada; el compilador
lo rechaza de forma segura en lugar de publicar datos de Cloth obsoletos. La
separacion y el Surface Wrap GPU siguen disponibles sin crear shell.

El compilador usa el offset iterativo de Unreal sobre la capa exterior de
referencia y conserva una copia interior ajustada a la piel. Si el offset crea un
contacto nuevo en una zona concava, contrae solamente un microparche exterior con
falloff; nunca mueve la capa interior. Los contactos que ya pertenecian a
costuras/paneles de la fuente, los microparches reparados y la exclusion anatomica
se certifican por separado en el receipt.

- **Covered Body Regions / Body Material Slots to Hide**: cobertura y ocultacion visual.
- **Advanced**: excluye slots, ramas oseas o morphs de la
  proyeccion matematica.

**Geometry Fit Fallback** conserva el comportamiento legado, pero no promete la
misma garantia dinamica que **Automatic GPU Surface Wrap (Recommended)**.

Para Golden Palace u otra anatomia auxiliar, **Body Material Slots to Hide** la
oculta visualmente y **Excluded Body Surfaces** la excluye de la
geometria usada para proyectar la prenda. Todo slot excluido de la superficie
debe estar tambien oculto.

## Agregar una prenda

1. Agrega un elemento a **Garment Catalog**.
2. Escribe un **Garment Index / ID** unico.
3. Selecciona siempre la mesh original en **Editable Source Garment Mesh**; nunca una
   `SK_...` generada por EF Clothing Morph.
4. Asigna el **Reference Body** correcto.
5. Deja **Fit Backend** y **Runtime Fit Policy** en sus valores recomendados, con
   **Additional Skin Clearance** en `0.00 cm`. Activa **Enable Adjustable
   Thickness (One-Time Compile)** si esa mesh es una lamina sin volumen.
6. Configura coverage/exclusiones si son necesarias.
7. Guarda. Antes de iniciar PIE o empaquetar, el gate de frescura recompila
   automaticamente el bundle interno si detecta un cambio estructural o una
   edicion guardada de la mesh fuente.

Para forzar la misma actualizacion manualmente con Unreal cerrado, ejecuta:

```powershell
Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.ps1
```

Los cambios estructurales -mesh, body, backend, fit, activacion del shell, pasos
del offset, suavizado, cobertura matematica o exclusiones- requieren refrescar el
bundle. El gate lo hace automaticamente antes de PIE/package. **Additional Skin
Clearance (cm)** y **Visible Thickness (cm, Runtime)** no recompilan. El registry
se publica de forma atomica solo
si todas las entradas habilitadas compilan y validan; no existe publicacion
individual de una fila.

## Por que existe una mesh interna adicional

V2 nunca modifica Female, Male, Multiple ni el skeleton compartido. La prenda
seleccionada en **Editable Source Garment Mesh** si es la fuente de autoria intencional:
puedes abrirla en el Skeletal Mesh Editor y usar las herramientas nativas de UE5,
incluyendo `Deform > Offset`, sculpt y edicion de su geometria. Al guardarla, V2
detecta el nuevo fingerprint y refresca automaticamente el bundle interno antes
del siguiente PIE o package.

Para proteger la fuente y el skeleton, V2 genera una Skeletal Mesh derivada con el sculpt, pesos,
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

No deben editarse ni equiparse manualmente. El binding puede ser grande y permanece separado
para permitir streaming; incrustar bindings de cientos de prendas dentro del
Director haria su carga inicial innecesariamente pesada.

El bundle anterior permanece como ultimo resultado valido hasta que la
recompilacion automatica publica el reemplazo de forma atomica. Una edicion manual
de la fuente nunca debe hacer desaparecer una prenda a mitad de gameplay. Si el
refresco no valida, PIE/package se detiene con un error claro y conserva el ultimo
bundle valido; no publica assets parciales.

## Estado de la migracion

El cutover schema 3 conserva el unico Director y agrega grosor real por indice.
El schema 2 ya habia retirado las dos DataTables antiguas y el viejo root publico
`/Game/_Generated/EFClothingMorphV2`. En Content Browser debe verse solo
`DA_EFClothingMorphDirector` dentro de la carpeta publica de EF Clothing Morph.

La validacion read-only se ejecuta con Unreal cerrado:

```powershell
Tools\ClothingMorphV2\Validate-EFClothingMorphDirector58.ps1
```

Comprueba el Director, IDs, pares source/body, separaciones y shells efectivos, registry,
perfiles/bindings internos, receipt de compilacion y hashes protegidos.
