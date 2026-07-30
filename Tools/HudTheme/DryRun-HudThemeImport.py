"""Run the HUD theme importer in protected dry-run mode inside UE 5.8."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path


IMPORTER = Path(__file__).resolve().with_name("Import-HudThemeVariants.py")
sys.argv = [str(IMPORTER), "--dry-run"]
runpy.run_path(str(IMPORTER), run_name="__main__")
