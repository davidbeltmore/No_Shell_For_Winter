"""Bounded MCP readiness: returns only after the target HUB and editor tools respond."""
import sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Equipment'))
from UnrealMcp import Client
start=time.monotonic(); last=''
not_before=time.time()-5
log=Path(__file__).resolve().parents[2]/'Saved/Logs/NoShellForWinter.log'
while time.monotonic()-start<120:
    try:
        # Do not enqueue HTTP requests while the game thread is still loading.
        # Timed-out startup requests preceded a DevHttp/InheritedContext crash.
        recent=log.read_text(encoding='utf-8',errors='replace')[-100000:] if log.exists() and log.stat().st_mtime>=not_before else ''
        if '=== Critical error: ===' in recent: raise SystemExit('Editor crashed during startup; inspect the log before retrying')
        if 'Completed OnFilesLoaded.Broadcast' not in recent:
            time.sleep(2);continue
        client=Client(); ts='editor_toolset.toolsets.scene.SceneTools'; client.describe(ts)
        level=client.call(ts,'get_current_level',{})['returnValue']
        if level=='/Game/_Game/Hub/HUB': print('MCP HUB ready in %.1fs'%(time.monotonic()-start)); break
    except Exception as exc: last=str(exc)
    time.sleep(2)
else: raise RuntimeError('120-second MCP readiness deadline: '+last)
