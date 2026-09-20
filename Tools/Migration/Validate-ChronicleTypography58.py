"""Compile Chronicle Blueprints and run the existing compact/expanded PIE fixture."""
import os
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
output = root / "Saved/Migration/ChronicleTypography"
output.mkdir(parents=True, exist_ok=True)
os.environ["CODEX_CHRONICLE_WBP_REPORT"] = str(output / "BlueprintCompile.json")
os.environ["CODEX_CHRONICLE_QA_DIR"] = str(output)
runpy.run_path(str(root / "Tools/Migration/Validate-ChronicleWidgets58.py"))
runpy.run_path(str(root / "Tools/Migration/ChronicleVisualQA58.py"))
