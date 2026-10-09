"""Bounded MCP readiness: returns only after the target HUB and editor tools respond."""
import sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Equipment'))
from UnrealMcp import Client
start=time.monotonic(); last=''
while time.monotonic()-start<120:
    try:
        client=Client(); ts='editor_toolset.toolsets.scene.SceneTools'; client.describe(ts)
        level=client.call(ts,'get_current_level',{})['returnValue']
        if level=='/Game/_Game/Hub/HUB': print('MCP HUB ready in %.1fs'%(time.monotonic()-start)); break
    except Exception as exc: last=str(exc)
    time.sleep(2)
else: raise RuntimeError('120-second MCP readiness deadline: '+last)
