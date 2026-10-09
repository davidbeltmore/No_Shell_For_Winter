"""Run a command with a wall-clock ceiling and durable process metrics.

Usage: python Tools/NPC/Bounded.py NAME SECONDS -- COMMAND [ARGS...]
Do not kill UBT descriptor guards: on timeout allow wrapper finally blocks to restore
the project descriptor before any editor is launched.
"""
import json,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
name,limit=sys.argv[1],int(sys.argv[2])
assert name.replace('-','').replace('_','').isalnum()
command=sys.argv[4:]; assert sys.argv[3]=='--' and command
out=root/'Saved/NPCDAZ'; out.mkdir(parents=True,exist_ok=True)
start=time.monotonic(); status='FAIL'
with (out/(name+'.log')).open('w',encoding='utf8') as log:
 p=subprocess.Popen(command,cwd=root,stdout=log,stderr=subprocess.STDOUT)
 try:
  code=p.wait(timeout=limit); status='PASS' if code==0 else 'FAIL'
 except subprocess.TimeoutExpired:
  status='TIMEOUT'
  # Stop descendants first; PowerShell guards then execute their finally blocks.
  script="$all=@(Get-CimInstance Win32_Process); $ids=[Collections.Generic.List[int]]::new(); $ids.Add("+str(p.pid)+"); for($i=0;$i -lt $ids.Count;$i++){ foreach($c in $all){if($c.ParentProcessId -eq $ids[$i]){$ids.Add([int]$c.ProcessId)}}}; for($i=$ids.Count-1;$i -gt 0;$i--){Stop-Process -Id $ids[$i] -Force -ErrorAction SilentlyContinue}"
  subprocess.run(['powershell','-NoProfile','-Command',script],timeout=15,capture_output=True)
  try: code=p.wait(timeout=15)
  except subprocess.TimeoutExpired: p.terminate(); code=p.wait(timeout=5)
result={'status':status,'exit_code':code,'seconds':round(time.monotonic()-start,3),'limit_seconds':limit,'command':command}
(out/(name+'-process.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result))
sys.exit(0 if status=='PASS' else 1)
