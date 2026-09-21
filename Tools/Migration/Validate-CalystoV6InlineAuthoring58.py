"""Read-only refresh of V6 authoring and cook-closure evidence after the inline upgrade."""
from pathlib import Path
import os
import runpy
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
if root != Path(r"D:\Projects UE5\NoShellForWinter").resolve():
    raise RuntimeError("Wrong project for Calysto V6 authoring validation")
try:
    for script in ("Create-CalystoDungeonDirectorPolicyV6.py", "Validate-CalystoDungeonDirectorV6CookClosure.py"):
        runpy.run_path(str(root / "Tools/Migration" / script), run_name="__main__")
finally:
    # The normal Editor startup-hook route also verifies a clean process exit;
    # commandlet receipts alone cannot prove that Python shutdown succeeded.
    if os.environ.get("CODEX_RUN_MIGRATION_BASELINE_PIE") == "1":
        unreal.SystemLibrary.quit_editor()
