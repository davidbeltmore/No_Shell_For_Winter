# Seguimiento de advertencias conocidas

El informe conserva el log original y distingue mensajes conocidos pendientes,
nuevos o cambiados, aumentos, disminuciones y mensajes no observados. No modifica
categorias de logging, severidades, plugins, assets ni configuracion de Unreal.
Conocido no significa corregido, inocuo ni aprobado para todos los escenarios.

## Uso automatico

`Tools/Migration/Package-PracticeProgression58.ps1` genera `WarningReport/warnings.md`
y `warnings.json` junto a cada `uat.log`, incluso si UAT falla. Un resultado que
requiere revision produce un aviso visible. El resultado original de UAT se
mantiene: el informe no convierte un fallo del empaquetado en exito.

## Uso manual

Desde la raiz del proyecto:

```powershell
python Tools/Migration/Report-KnownWarnings58.py --log "ruta/uat.log" --out "Saved/Analysis/MiInformeNuevo"
```

La carpeta de salida debe ser nueva. Exit 0 significa sin novedades respecto a
la referencia; exit 2 significa revisar mensajes nuevos/aumentados, errores o
ausencia del cierre UAT exitoso. Otros errores del analizador tambien requieren
revision. Para CI se puede ejecutar este comando como puerta independiente.

## Referencia y alcance

Referencia exacta: `Evidence/KnownCookWarnings58.json`, extraida del paquete
`Package_20260930_151131`, snapshot `81fb51cd`. Incluye hash del log y conteos
por mensaje y fase. No hay patrones amplios que acepten toda una categoria.
Solo se retiran prefijos de timestamp/contador y la repeticion del resumen
`LogInit: Display:`; las emisiones originales permanecen contabilizadas.

Comparar solo el mismo flujo HUB/StorySelection/MenuMap, idioma y versiones.
Un cambio en rutas, idioma, versiones o texto genera revision conservadora.
La referencia nunca se actualiza automaticamente. Un mensaje ausente tampoco
se considera reparado: podria faltar el escenario que lo emite.

La referencia tiene 26 emisiones durante el cook (25 mensajes distintos) y
969 de UECP despues del resumen. El detalle conocido se presenta desplegable
en Markdown y completo en JSON; las novedades aparecen primero.
El informe no valida Calysto ni ejecuta sus sistemas. Conserva el aviso PCG
presente en el log como evidencia pendiente del alcance excluido.

Quinn y las pruebas internas por localizacion siguen documentados en
`CompatibilityClosure_20260928.md`: no se inventan ocurrencias en un cook donde
no aparecen. Los errores detectados siempre requieren revision, aunque sean
historicamente conocidos. Este analizador es para logs completos de empaquetado;
no certifica logs arbitrarios de PIE ni sustituye pruebas funcionales.

## Validacion de esta herramienta

- `python Tools/Migration/Test-KnownWarnings58.py`: PASS. Comprueba duplicacion
  del resumen, mensaje nuevo/cambiado, aumento, error, Condition failed,
  cierre incompleto, mensaje ausente, UTF-16 y preservacion del archivo original.
- Informe real: `Saved/Analysis/CompatibilityClosure/WarningTriage_20260930/warnings.md`.
  26 + 969 advertencias, cero novedades contra la referencia y cero errores
  detectados. Es una comparacion con el mismo log, no un nuevo cook.
- No se ejecutaron Unreal, build, PIE ni empaquetado nuevos: este cambio afecta
  exclusivamente al analisis posterior de logs y al wrapper de empaquetado.

Los riesgos y reparaciones pendientes conservan el estado del informe de
compatibilidad. Engine, Marketplace y el proyecto fuente no se modifican.
