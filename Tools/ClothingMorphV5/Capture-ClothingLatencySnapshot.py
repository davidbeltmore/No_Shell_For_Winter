"""Task-scoped source/evidence hashes; compares the protected unisex baseline."""
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT/'Saved/ClothingLatencyQA'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


baseline = json.loads((ROOT/'Saved/ClothingUnisexQA/protected_before.json').read_text())
protected = {name:digest(pathlib.Path(name)) if pathlib.Path(name).is_file() else None for name in baseline}
differences = [name for name in baseline if baseline[name] != protected[name]]
(OUT/'protected_check.json').write_text(json.dumps(dict(status='FAIL' if differences else 'PASS',
    reference='Saved/ClothingUnisexQA/protected_before.json', baseline_files=len(baseline), differences=differences,
    scope='Previously recorded protected files; not a full new-file inventory'),indent=2))
source_before = json.loads((OUT/'runtime_source_before.json').read_text())
source_changes = [name for name,value in source_before.items() if digest(ROOT/name) != value]
paths = [
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Public/EFClothingEquipmentBridgeComponent.h',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/EFClothingEquipmentBridgeComponent.cpp',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/EFClothingMorphWorldSubsystem.cpp',
    'Docs/Migration/EFClothingMorphLatency.md',
]
paths += ['Tools/ClothingMorphV5/'+name for name in (
    'Measure-ClothingLatency58.py','Measure-ClothingTransitions58.py','Run-ClothingLatency58.ps1',
    'Summarize-ClothingLatency58.py','Validate-ClothingEquipment58.py','Validate-UnisexPIE58.py',
    'Run-UnisexPIE58.ps1','Capture-ClothingLatencySnapshot.py')]
snapshot = dict(head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    baseline='git_before.txt and source_before; workspace was already dirty',
    changed_preexisting_runtime_sources=source_changes,
    task_files={name:digest(ROOT/name) for name in paths},
    skill={'A:/Skills Codex/ef-clothing-unisex-workbench/SKILL.md':digest(pathlib.Path('A:/Skills Codex/ef-clothing-unisex-workbench/SKILL.md'))})
evidence = list(OUT.glob('*/result.json')) + list(OUT.glob('*.log'))
evidence += list((OUT/'UnisexRegression/PIE').glob('*.json'))
evidence += list((OUT/'UnisexRegression/PIE').glob('*.png'))
evidence += [ROOT/'Saved/ClothingMorphV4QA/BlueprintCompile.json']
snapshot['evidence_files'] = {str(path.relative_to(ROOT)):digest(path) for path in evidence if path.is_file()}
(OUT/'task_snapshot.json').write_text(json.dumps(snapshot,indent=2))
print(json.dumps(dict(protected='FAIL' if differences else 'PASS',files=len(baseline),source_changes=source_changes)))
if differences:
    raise SystemExit(1)
