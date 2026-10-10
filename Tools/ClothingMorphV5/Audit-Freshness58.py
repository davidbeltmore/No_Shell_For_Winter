"""Read-only live certification of generated unisex bindings and V5 manifest.

Run through Tools/InnerState/RunEditorPython58.py after editor preparation.
Does not refresh, save, delete or compile assets. Uses the canonical V4 options.
"""
import json
import time
from pathlib import Path
import unreal


def audit_clothing_freshness():
    started = time.monotonic()
    director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    registry = unreal.load_asset('/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry')
    options = unreal.EFClothingNativeSourceCompileOptions()
    options.set_editor_property('output_root', '/EFClothingMorph/_Internal/Compiled/V4')
    # MinimumClearanceCm is a deprecated, protected zero default; do not assign it.
    options.set_editor_property('maximum_push_cm', 2.5)
    options.set_editor_property('only_stale', True)
    options.set_editor_property('strict_catalog_certification', True)
    result = unreal.EFClothingFitCompilerLibrary.validate_native_source_catalog_v4(
        director, registry, unreal.load_asset('/Game/DazToUnreal/Multiple/Multiple'), options)
    report = {key: result.get_editor_property(key) for key in [
        'fresh', 'enabled_row_count', 'valid_binding_count', 'draft_row_count',
        'invalid_row_count', 'stale_row_count', 'report']}
    manifest = unreal.load_asset('/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest')
    report['manifest_valid'] = bool(manifest and manifest.is_valid_manifest())
    passed = (report['fresh'] and report['manifest_valid']
              and report['enabled_row_count'] > 0
              and report['valid_binding_count'] == report['enabled_row_count']
              and not any(report[key] for key in ['draft_row_count', 'invalid_row_count', 'stale_row_count']))
    report['status'] = 'PASS' if passed else 'FAIL'
    report['seconds'] = round(time.monotonic() - started, 3)
    out = Path(unreal.Paths.project_dir()) / 'Saved/ClothingMorphV4QA/Freshness.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
    assert passed, report


audit_clothing_freshness()
