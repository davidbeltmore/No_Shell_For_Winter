"""Revalidate current chest Blueprint provenance and the preserved cook closure.

Both validators are read-only for assets and retain append-only creator history.
Run after deliberate chest Blueprint edits, before the normal packaging wrapper.
"""
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
for name in ("Create-CalystoDungeonDirectorRuntimeAssetsV6.py",
             "Validate-CalystoDungeonDirectorV6CookClosure.py"):
    runpy.run_path(str(root / "Tools/Migration" / name), run_name="__main__")
