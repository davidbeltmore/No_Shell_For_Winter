"""Record only this update against its session snapshots, preserving earlier work."""
from pathlib import Path
import difflib
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/ClothingUnisexQA'
BASE = OUT / 'source_before'
added = [
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Public/EFClothingBodyTarget.h',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/Tests/EFClothingUnisexTests.cpp',
    'Plugins/EFClothingMorph/Source/EFClothingMorphEditor/Private/EFClothingUnisexSurface.inl',
    'Plugins/EFClothingMorph/Source/EFClothingMorphEditor/Private/EFClothingBodyCoverageBuilder.inl',
    'Docs/Migration/EFClothingMorphUnisex.md',
]
added += [str(p.relative_to(ROOT)).replace('\\', '/') for p in (ROOT/'Tools/ClothingMorphV5').glob('*Unisex*') if p.is_file()]
paths = sorted(set(added + [str(p.relative_to(BASE)).replace('\\', '/') for p in BASE.rglob('*') if p.is_file()]))
patch, files = [], []
for rel in paths:
    current, baseline = ROOT / rel, BASE / rel
    old = baseline.read_bytes() if baseline.exists() else b''
    new = current.read_bytes()
    if old == new:
        continue
    patch.extend(difflib.unified_diff(old.decode('utf-8-sig').splitlines(True), new.decode('utf-8-sig').splitlines(True),
                                    fromfile='before/'+rel, tofile='after/'+rel))
    files.append({'path': rel, 'before_sha256': hashlib.sha256(old).hexdigest() if old else None,
                  'after_sha256': hashlib.sha256(new).hexdigest()})
(OUT/'task_changes.patch').write_text(''.join(patch), encoding='utf-8')
assets = [ROOT/'Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset']
assets += list((ROOT/'Plugins/EFClothingMorph/Content/_Internal/Compiled').rglob('*.uasset'))
assets += [ROOT/'Plugins/EFClothingMorph/Content/Deformers/DG_EFBodyCoverageDQS.uasset']
asset_hashes = {str(p.relative_to(ROOT)).replace('\\','/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in assets}
(OUT/'task_snapshot.json').write_text(json.dumps({'base_commit': (OUT/'head.txt').read_text().strip(),
    'baseline_git_status': 'git_before.txt', 'files': files, 'current_generated_assets': asset_hashes}, indent=2), encoding='utf-8')
print(f'Task snapshot: {len(files)} source/tool/document files; {len(assets)} asset hashes.')
