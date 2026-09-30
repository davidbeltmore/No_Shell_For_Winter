"""Read-only comparison of a complete package log against a reviewed exact baseline."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASELINE = ROOT / 'Docs/Migration/Evidence/KnownCookWarnings58.json'


def read_log(path):
    raw = path.read_bytes()
    encoding = 'utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    return raw, raw.decode(encoding, errors='strict').splitlines()


def scan(path):
    raw, lines = read_log(path)
    counts, locations = Counter(), {}
    errors, summaries = [], []
    phase = 'cook'
    for number, original in enumerate(lines, 1):
        line = re.sub(r'^(?:\[[^\]]*\])+', '', original).strip()
        if 'Success - ' in line and 'warning(s)' in line:
            summaries.append(line)
            phase = 'after_cook_summary'
        # UE repeats earlier messages in its Unique only summary. Keep originals only.
        if line.startswith('LogInit: Display: ') and re.search(r'\b(?:Warning|Error|Fatal):', line):
            continue
        if re.search(r'\b(?:Error|Fatal):|\bERROR:|Assertion failed:|Condition failed|Unhandled Exception|BUILD FAILED', line):
            errors.append({'line': number, 'message': line})
        if re.search(r'\bWarning:', line):
            key = (phase, line)
            counts[key] += 1
            locations.setdefault(key, []).append(number)
    return {'sha256': hashlib.sha256(raw).hexdigest(), 'counts': counts,
            'locations': locations, 'errors': errors, 'summaries': summaries,
            'completed': any('AutomationTool exiting with ExitCode=0' in x for x in lines)}


def compare(data, baseline):
    expected = {(x['phase'], x['message']): x['count'] for x in baseline['warnings']}
    rows = []
    for key in sorted(set(expected) | set(data['counts'])):
        actual, prior = data['counts'][key], expected.get(key, 0)
        status = ('NUEVA_O_CAMBIADA' if key not in expected else
                  'AUMENTO' if actual > prior else 'NO_OBSERVADA' if not actual else
                  'MENOR_FRECUENCIA' if actual < prior else 'CONOCIDA_PENDIENTE')
        rows.append({'phase': key[0], 'message': key[1], 'baseline': prior,
                     'actual': actual, 'status': status, 'lines': data['locations'].get(key, [])})
    alert = bool(data['errors'] or not data['completed'] or
                 any(x['status'] in ('NUEVA_O_CAMBIADA', 'AUMENTO') for x in rows))
    return rows, alert


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_BASELINE)
    parser.add_argument('--out', type=Path, required=True, help='New output directory; never overwrites a report')
    args = parser.parse_args()
    data = scan(args.log)
    baseline = json.loads(args.baseline.read_text(encoding='utf-8'))
    rows, alert = compare(data, baseline)
    args.out.mkdir(parents=True, exist_ok=False)
    result = {'status': 'REVISAR' if alert else 'SIN_NOVEDADES_RESPECTO_A_BASELINE',
              'scope': baseline['scope'], 'log': str(args.log.resolve()),
              'log_sha256': data['sha256'], 'baseline': str(args.baseline.resolve()),
              'baseline_sha256': hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
              'completed': data['completed'], 'cook_summaries': data['summaries'],
              'errors': data['errors'], 'warnings': rows}
    (args.out / 'warnings.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    text = ['# Advertencias del empaquetado', '', '**' + result['status'] + '**', '',
            'Conocida no significa corregida ni inocua. Calysto no se valida con este informe.', '',
            f"Log original: [{args.log.name}]({args.log.resolve().as_posix()})", '',
            f"SHA-256: `{data['sha256']}`", '',
            'Comparacion valida solo para el mismo flujo HUB/StorySelection/MenuMap, idioma y versiones.', '',
            '## Resumen', '', '| Fase | Advertencias actuales | Base |', '|---|---:|---:|']
    for phase in ('cook', 'after_cook_summary'):
        subset = [r for r in rows if r['phase'] == phase]
        text.append(f"| {phase} | {sum(r['actual'] for r in subset)} | {sum(r['baseline'] for r in subset)} |")
    text += ['', f"Errores detectados: {len(data['errors'])}. UAT completo con exit 0: {data['completed']}.", '',
             'La ausencia de un aviso no certifica su reparacion. Los logs originales no se modifican.', '']
    for title, subset in [('Nuevas, cambiadas o aumentadas', [r for r in rows if r['status'] in ('NUEVA_O_CAMBIADA', 'AUMENTO')]),
                          ('Conocidas y no observadas', [r for r in rows if r['status'] not in ('NUEVA_O_CAMBIADA', 'AUMENTO')])]:
        text += ['## ' + title, '']
        if title == 'Conocidas y no observadas':
            text += [f'{len(subset)} mensajes distintos. Detalle completo conservado en JSON y desplegable:', '',
                     '<details><summary>Ver advertencias conocidas</summary>', '']
        if not subset:
            text += ['Ninguna.', '']
        for r in subset:
            text += [f"- **{r['status']}** [{r['phase']}] {r['actual']} / base {r['baseline']}; lineas {r['lines']}",
                     '  ' + r['message'].replace('<', '&lt;').replace('>', '&gt;'), '']
        if title == 'Conocidas y no observadas':
            text += ['</details>', '']
    text += ['## Errores (siempre requieren revision)', '']
    text += [f"- Linea {e['line']}: {e['message']}" for e in data['errors']] or ['Ninguno detectado.']
    (args.out / 'warnings.md').write_text('\n'.join(text) + '\n', encoding='utf-8')
    print(f"{result['status']}: {args.out / 'warnings.md'}")
    return 2 if alert else 0


if __name__ == '__main__':
    raise SystemExit(main())
