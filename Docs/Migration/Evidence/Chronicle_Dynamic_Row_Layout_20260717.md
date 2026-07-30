# Chronicle: filas de altura dinámica

Fecha de ejecución: 2026-07-17 (America/Bogota)

## Alcance

- Proyecto objetivo: NoShellForWinter
- Referencia: compilación privada heredada (solo lectura, no modificada; identidad omitida)
- Subsistema ampliado: `UProjectActivityFeedSubsystem`
- Rutas públicas conservadas: `/Game/_Game/Widgets/Chronicle/**`
- WBP conservados: standard, gain, dialogue quote, empty state, globales y host principal

## Implementación

- `FProjectChronicleLayoutPolicy` centraliza altura mínima, ancho máximo, separación y reparto inline.
- La política se obtiene desde `UProjectActivityFeedSubsystem::GetChronicleLayoutPolicy`.
- Valores configurados:
  - compacto: `390 px`;
  - expandido: `450 px`;
  - inline: `38%` primario y `62%` secundario.
- `RowHeight` se interpreta como altura mínima.
- `RootSizeBox` limpia `HeightOverride` y usa `MinDesiredHeight`.
- `MessageText`, `PrimaryTextBlock` y `SecondaryTextBlock` usan auto-wrap, `DefaultWrapping`, ancho de política, ancho mínimo cero y elipsis de overflow.
- Cada slot de `EntriesBox` usa tamaño `Automatic`; después de reconstruir filas se invalida el layout y se ejecuta un prepass.
- El scroll por entrada navega widgets reales con `ScrollWidgetIntoView`; las entradas nuevas conservan `ScrollToEnd`.

## Gates ejecutados

| Gate | Estado | Evidencia |
|---|---|---|
| Development Editor C++ | PASS | `C:\Users\bigin\AppData\Local\UnrealBuildTool\Log.txt`; `Result: Succeeded` |
| Automatización de política | PASS | `Saved/Migration/Automation/ChronicleDynamicRows_20260717_2234/index.json` |
| Automatización de filas dinámicas | PASS | mismo informe: 2/2 success, 0 warnings, 0 errors; cubre texto corto/largo, salto explícito, comillas, URL indivisible, filas de alturas distintas y diálogo inline |
| Runtime winner de Chronicle | PASS | `Saved/CodexUser/ChronicleAutomation_20260717_220205/Saved/CodeWidgetDesignerBridge/Reports/RuntimeAudit_ProjectActivityFeedWidget_Chronicle.json` |
| Preflight CodeWidgetDesignerBridge | PASS | `Saved/CodexUser/ChronicleAutomation_20260717_220205/Saved/CodeWidgetDesignerBridge/Reports/Chronicle_Preflight.json`: 13 expected, 13 existing, 0 missing |
| Compilación de 13 WBP | PASS | `Saved/Migration/Logs/ChronicleCompile13WBP_20260717_221330.log`: 0 errors, 0 warnings, 0 failed loads |
| Development Game + cook + paquete Windows | PASS | `Saved/Migration/Logs/ChronicleDynamicRows_PackageRetry_20260717.log`: 7,044/7,044 paquetes, cook exit code 0, IoStore success, stage/archive completos y `BUILD SUCCESSFUL` |
| Smoke test del paquete Windows | PASS | `Docs/Migration/Evidence/Chronicle_Packaged_Smoke_20260717.json`; el proceso permaneció activo 20 segundos con `-nullrhi -nosound -unattended` y se cerró únicamente su PID |
| PIE visual compacto/expandido, J, scroll y entrada nueva | PENDING_USER_DECISION | Unreal muestra recuperación de un autosave previo de `/Game/_Game/Hub/HUB`; no se descartó ni restauró automáticamente |
| Escalas UI comunes | PENDING | requiere el PIE visual anterior |

El primer intento de cook procesó los 7,044 paquetes con resultado interno 0, pero UAT terminó con `Error_UnknownCookFailure` porque el Editor abierto ocupaba `127.0.0.1:8000` y el commandlet registró el fallo de binding como error. Se cerró únicamente esa instancia, se comprobó que el SHA-256 del autosave no cambió y el reintento completo pasó sin errores.

## Invariantes protegidos

Informe: `Saved/Migration/Evidence/ChronicleDynamicRows_ProtectedInvariantVerification_20260717.json`.

- ACFU 4.3.5: PASS, 5,043/5,043 archivos.
- Plugin DazToUnreal 5.8.0.491: PASS, 213/213 archivos.
- Female: PASS.
- Multiple: PASS.
- Línea base Phase 0 para `Target_Daz_Assets`: FAIL heredado, 67 diferencias dentro de Male.
- Player: FAIL contra Phase 0; el archivo actual data de 2026-07-14, antes de esta tarea.
- Male: FAIL contra Phase 0; el archivo actual data de 2026-07-16, antes de esta tarea.

No se modificó, restauró ni reemplazó ninguno de esos assets durante este trabajo.

## Estado del autosave

`Saved/Autosaves/PackageRestoreData.json` conserva una única entrada para:

- paquete: `/Game/_Game/Hub/HUB`;
- autosave: `Game/_Game/Hub/HUB_Auto1.umap`.

El Editor quedó cerrado después de conservar el autosave y liberar el puerto usado por el cook. En el próximo arranque volverá a solicitar **Restore Selected** o **Skip Restore**; esa elección queda reservada al propietario del proyecto.
