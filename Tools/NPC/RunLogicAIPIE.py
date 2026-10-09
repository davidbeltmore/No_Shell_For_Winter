"""Bounded AI-only retry for a measured navigation/controller failure."""
from pathlib import Path
import unreal
NPC_LOGIC_COMMAND='Project.NPCLogic.QA ai'
try:
    ai_runner=Path(unreal.Paths.project_dir())/'Tools/NPC/RunLogicPIE.py'
    exec(compile(ai_runner.read_text(),str(ai_runner),'exec'))
finally:
    del NPC_LOGIC_COMMAND
