# Calysto RoomTheme material repair — 2026-07-25

## Resultado

`PASS` para la selección runtime de materiales de `RoomTheme` en Floor, Wall y
Roof, tanto al iniciar directamente
`/Game/Procedural/Maps/DungeonGeneration` como al entrar desde HUB mediante
`DoorToLevel`.

El defecto tenía tres causas independientes:

1. `MI_Stone_Wall_21` heredaba de un material Marketplace sin las banderas de
   uso para `Instanced Static Meshes` y `Nanite`. PCG sí asignaba la instancia,
   pero el renderer la sustituía por el material por defecto.
2. El grafo nativo de Calysto
   `PCG_ExtractRoomThemeMaterial:GetAttributeFromPointIndex_12` leía
   `FloorMaterial` y lo publicaba como `RoofMaterial`.
3. El selector nativo de la rama temática Roof en
   `PCG_SetDungeonMesh:StaticMeshSpawner_48` consumía `FloorMaterial` en lugar
   de `RoofMaterial`.

Los dos errores de Roof también están presentes en el proyecto de referencia
Calysto 5.8 situado en
`D:\Projects UE5\CalystoExample\Content\Calysto`. Por tanto, no fueron causados
por la migración de LustAsDeadlySin, `DoorToLevel` ni EFProcedural.

## Alcance y seguridad

- Target escrito: `D:\Projects UE5\NoShellForWinter`.
- Referencia Calysto 5.8: inspección de solo lectura.
- Proyecto fuente LustAsDeadlySin: no fue escrito ni resguardado con UE 5.8.
- Plugins Marketplace y Engine: no fueron modificados.
- Snapshot Git de referencia: `7709180e4b8cee1230e0891c5b7cd5c7bdd7d9bb`,
  rama `migration/ue58-lasd-parity`.
- El working tree ya contenía cambios de usuario; no se hizo reset, checkout ni
  commit automático.

La comparación binaria inicial encontró 405 archivos bajo `Content/Calysto`:
398 eran idénticos a CalystoExample y solo siete ya diferían:

- `BP_MassiveDungeon`
- `DA_DungeonMaterial`
- `DA_RoomTheme`
- `MI_RedTiles`
- `SM_Floor`
- `SM_Roof`
- `SM_Wall`

Los grafos PCG involucrados eran inicialmente idénticos a CalystoExample, lo
que permitió atribuir los dos errores de Roof a la versión nativa 5.8.

## Correcciones mínimas

### Compatibilidad de material

- Se creó el adaptador project-owned
  `/Game/Procedural/Materials/Calysto/M_OneClickBasicMaster_ISM`.
- Es una copia local del master requerido; el original Marketplace permanece
  intacto.
- `bUsedWithInstancedStaticMeshes = true`.
- `bUsedWithNanite = true`.
- `/Game/ShareTextures/Wall/1K/MI_Stone_Wall_21` fue reparentado al adaptador.

### Ruta Roof de RoomTheme

- En
  `/Game/Calysto/Dungeon/PCG/Function/PCG_ExtractRoomThemeMaterial`,
  `GetAttributeFromPointIndex_12` quedó:
  `RoofMaterial -> RoofMaterial`.
- En `/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh`, el selector de
  `StaticMeshSpawner_48` quedó con
  `materialOverrideAttributes = ["RoofMaterial"]`.
- No se cambiaron conexiones, gramática, generación de salas ni lógica de
  `DoorToLevel`.

Hashes SHA-256 después de la reparación:

| Asset | Target reparado | CalystoExample 5.8 |
|---|---|---|
| `PCG_ExtractRoomThemeMaterial` | `DC8E3BEF7B5F00CC3FCB369D2BA336EEF1E663C69FFF39AF6CE424C67673F411` | `C7BBB6D03DF77413B92E6E9888989D37BE5FAEAFC62F3DCFF843B2821C4CA3DF` |
| `PCG_SetDungeonMesh` | `7A185669F120870E8A201B10DEBC226E3C4222988528BAF1F4F7EA3650280318` | `2AB54CDEFDFB2B72694E817762F37EA8458FAF85B64B0DD3C580F513E0E22AD9` |

### Configuración final

`DA_RoomTheme` conserva dos entradas de peso `1.0`:

- `DA_RoomForge`
- `DA_RoomShrine`

En ambas entradas:

- los tres `ThemeOverride*Material` están activos;
- Floor, Wall y Roof usan `MI_Stone_Wall_21`.

También se restauró el fallback nativo, para que las piezas no cubiertas por un
tipo de sala nunca dependan de WorldGrid:

- `BP_MassiveDungeon.DungeonMaterial = DA_DungeonMaterial`;
- overrides globales Floor, Wall, Roof y Ramp activos;
- `DA_DungeonMaterial` usa `MI_GreyTiles` en Floor, Wall y Roof;
- los slots base de `SM_Floor`, `SM_Wall` y `SM_Roof` usan `MI_GreyTiles`.

## Prueba y error visual

| Evidencia | Resultado |
|---|---|
| `01-baseline-worldgrid-pie.png` | Reproducción inicial de WorldGrid |
| `02-native-roomthemes-pie.png` | A/B con temas nativos |
| `03-custom-stone-usage-fixed-pie.png` | Piedra personalizada después de reparar Usage |
| `04-final-direct-map-pie.png` | Primera configuración final directa |
| `05-door-to-level-before-interact.png` | Jugador colocado frente a la puerta real |
| `06-final-via-door-to-level-pie.png` | Primera entrada real mediante `E` |
| `08-roof-isolated-fixed-pie.png` | Floor/Wall/Roof deliberadamente distintos; Roof ya independiente |
| `09-final-direct-map-after-roof-fix.png` | Configuración final tras las dos correcciones Roof |
| `10-final-via-door-after-roof-fix.png` | Configuración final entrando desde HUB por `DoorToLevel` |
| `11-packaged-direct-map-after-fix.png` | Ejecutable empaquetado en `DungeonGeneration`, sin WorldGrid |

Directorio:
`Saved/Migration/CalystoRoomTheme/`.

La prueba aislada usó temporalmente:

- Floor: `MI_RedTiles`;
- Wall: `MI_GreenTile`;
- Roof: `MI_Stone_Wall_21`.

La inspección runtime resultante confirmó 128 instancias temáticas de Floor
con Red, 98 de Wall con Green y 128 de Roof con Stone, más sus instancias de
fallback gris. Esta prueba separa la corrección de Roof de una coincidencia
visual producida por usar la misma piedra en las tres superficies.

## Evidencia runtime final

### Inicio directo

PCG terminó y creó:

| Superficie | Temático Stone | Fallback Grey |
|---|---:|---:|
| Floor | 176 | 11 |
| Wall | 126 | 28 |
| Roof | 176 | 11 |

### Entrada por DoorToLevel

Se cargó HUB, se colocó el pawn junto a `DoorToLevel_C_0`, se envió una tecla
`E` real a la ventana PIE y se verificó que el mundo pasó a
`DungeonGeneration`.

| Superficie | Temático Stone | Fallback Grey |
|---|---:|---:|
| Floor | 112 | 24 |
| Wall | 76 | 52 |
| Roof | 112 | 24 |

Los índices `_0`/`_1` de los componentes cambian según el orden de creación;
la comprobación se hizo por `overrideMaterials` e `instanceCount`, no por el
sufijo del componente.

El log final de PIE contiene:

- `LogEFProceduralPCGRuntime: PCG generation finished for world DungeonGeneration`;
- cero coincidencias para `missing usage flag`;
- cero coincidencias para `Default Material will be used`;
- cero coincidencias para `WorldGridMaterial`.

## Cook, package y ejecución empaquetada

El primer package completo terminó correctamente, pero al ejecutar reveló un
crash preexistente durante la inicialización estática:

`EFProjectUITheme::Positive -> EFProjectUITheme::GetTheme ->
UEFProjectUISettings::Get`.

Para poder completar el smoke test se añadió un guard mínimo y project-owned
en
`Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectUITheme.cpp`:
`GetTheme()` devuelve su tema fallback cuando `UObjectInitialized()` todavía
es falso, antes de consultar un CDO. No se modificó ningún plugin Marketplace
o Engine. El archivo ya contenía cambios locales anteriores; el cambio de esta
tarea se limita al include de `UObjectBase.h` y al guard.

Package final:

- log:
  `Saved/Migration/Logs/Package_RoomThemeStaticInit_20260725_210206.stdout.log`;
- output:
  `Saved/Migration/Package/RoomThemeStaticInit_20260725_210206/`;
- build: `PASS`;
- cook: `PASS`, 5.818 packages;
- stage: `PASS`;
- Pak/IoStore: `PASS`;
- archive: `PASS`;
- UAT: `BUILD SUCCESSFUL`, `ExitCode=0`.

Hashes SHA-256 del archive:

| Ejecutable | Bytes | SHA-256 |
|---|---:|---|
| Bootstrap `Windows/NoShellForWinter.exe` | 171.520 | `6DCEB1F2E2B236EFFD1C8F07375B8B5CFFEA63773A01119760B6927057D11B5D` |
| Binario `Binaries/Win64/NoShellForWinter.exe` | 404.067.328 | `F6C1C84F523A9554022497D551B1501D6B47995AEBD5BD7D43FA4404762F3F6E` |

El ejecutable final abrió directamente `DungeonGeneration`, creó
`BP_MassiveDungeonRuntime` y terminó PCG antes de la captura 11. Su log tiene:

- una coincidencia para
  `PCG generation finished for world DungeonGeneration`;
- cero coincidencias para `missing usage flag`;
- cero coincidencias para `Default Material will be used`;
- una mención informativa a `WorldGridMaterial`, causada por la carga recursiva
  interna de `DefaultTextMaterialOpaque`; no es un warning ni una asignación a
  una mesh.

El smoke test empaquetado de los materiales es `PASS`. El log empaquetado no
está completamente limpio: conserva un `Accessed None` de `PCG_Editor`, 16
errores sobre el atributo `Object Transform` y, después de 64 segundos, un
crash ajeno a Calysto:

`TSubclassOf<APawn>::operator* ->
UProjectCharacterIdentitySubsystem::IsConfiguredClass` en
`ProjectCharacterIdentitySubsystem.cpp:230`.

PCG ya había terminado y la dungeon temática era visible antes del crash. La
estabilidad prolongada del ejecutable queda en `FAIL`; no se modificó ese
subsistema de identidad porque sería una ampliación material del alcance.

## Gates

| Gate | Estado | Evidencia |
|---|---|---|
| Comparación CalystoExample 5.8 | PASS | 405 archivos; 398 idénticos; defectos Roof nativos |
| Material Usage ISM/Nanite | PASS | flags live + ausencia de warnings |
| `DA_RoomTheme` final | PASS | dos entradas Forge/Shrine inspeccionadas y guardadas |
| Blueprint compile | PASS | `BP_MassiveDungeon`, warnings-as-errors |
| PIE directo | PASS | captura 09 + componentes runtime |
| PIE por DoorToLevel | PASS | captura 10 + componentes runtime |
| Visual QA | PASS | capturas 01–11 |
| Cold Editor build | PASS | `ColdBuildReopen.stdout.log`, `Result: Succeeded` |
| Cook | PASS | 5.818 packages; `COOK COMMAND COMPLETED` |
| Stage/Pak/IoStore/Archive | PASS | package final + UAT `ExitCode=0` |
| Packaged RoomTheme smoke | PASS | captura 11 + PCG terminado + cero fallback warnings |
| Packaged runtime prolongado | FAIL | crash de `ProjectCharacterIdentitySubsystem` a los 64 s |

La reparación de RoomTheme supera todos sus gates. El cierre global del
proyecto no puede marcarse como `PASS` mientras siga el crash de identidad y
los invariantes protegidos indicados abajo.

## Invariantes protegidos posteriores

- ACFU 4.3.5: `PASS`, 5.043 archivos; manifiesto
  `69F46CACC120E44AC3B1729342E059CCD24D77D01534CB4E0F36C4A8A26D87F9`.
- DazToUnreal 5.8.0.491: `PASS`, 213 archivos; manifiesto
  `523200EBEED3B1284445C0570029CE746C10D4E5039B41AAD769544166E2B491`.
- Female: `PASS`,
  `425EAB06C1DEE4BFDAEA7890D419FE60EA2443F472004168EC5AC4C98F3B9CFC`.
- Multiple: `PASS`,
  `3FC4E53877C2EA61A766761E12095FA5F9AF48993A2ECE6CE131CFCCA6BF9583`.
- Male: `PASS`,
  `E4C507062363E81F6EBF00053741FB5F6A66F3650CCBF2DBC8530DEA175EA8F6`.
- Player: `FAIL` contra el baseline de sesión. Su timestamp
  (`2026-07-25 16:27:24`) precede esta tarea y nunca estuvo dentro del conjunto
  de assets guardados. Hash actual:
  `E7EDE80A927A34004014F497141097AC64597AD826C9EE85B0077EE7EA33891D`.
- Frederick: `PENDING_RUNTIME_IDENTITY`; no existe un package resoluble con
  ese nombre ni en target ni en la referencia fuente inspeccionada.

El gate global de invariantes se mantiene en `FAIL/PENDING` por Player y
Frederick, independientemente de este repair. No se revirtió ni resguardó
ninguno de esos assets.

## Documentación consultada

- Calysto Massive Dungeon — Room Theme:
  `https://qwertystudio.gitbook.io/massive-dungeon/room-theme/room-theme`
- Calysto Massive Dungeon — Override Dungeon Material:
  `https://qwertystudio.gitbook.io/massive-dungeon/customize-your-dungeon/override-dungeon-material`
- Calysto Massive Dungeon — Quick Start:
  `https://qwertystudio.gitbook.io/massive-dungeon/quick-start`
- Epic — Material Properties:
  `https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-material-properties?lang=en-US`
- Epic — `UMaterial`:
  `https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterial`
- Epic — `UPCGStaticMeshSpawnerSettings`:
  `https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/PCG/UPCGStaticMeshSpawnerSettings`
