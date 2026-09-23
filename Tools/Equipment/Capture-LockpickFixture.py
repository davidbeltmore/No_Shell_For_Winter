"""Capture the real Slate minigame; leave its gate pending for actual visual review."""
import argparse, base64, json, time
from pathlib import Path
from UnrealMcp import Client

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run_directory',type=Path)
args=parser.parse_args()
deadline=time.monotonic()+300
while time.monotonic()<deadline:
    result=args.run_directory/'result.json'
    if result.exists():
        try: state=json.loads(result.read_text())
        except (OSError,ValueError): state={}
        if state.get('phase')=='minigame_visual' and state.get('status')=='PENDING':
            client=Client();client.describe('EditorToolset.EditorAppToolset')
            capture=client.call('EditorToolset.EditorAppToolset','CaptureEditorImage',{})['returnValue']
            assert capture['mimeType']=='image/png'
            path=args.run_directory/'minigame-slate.png'
            path.write_bytes(base64.b64decode(capture['data'],validate=True))
            print(path);break
        if state.get('status') in ('PASS','FAIL'): raise RuntimeError('Fixture finished before visual gate: '+str(state.get('error','')))
    time.sleep(0.5)
else: raise TimeoutError('No minigame visual gate within five minutes')
