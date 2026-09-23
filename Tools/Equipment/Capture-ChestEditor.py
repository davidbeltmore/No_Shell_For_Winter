"""Capture actual native Editor/Details UI through MCP; never synthesize an image."""
import base64
import json
import sys
from pathlib import Path
from UnrealMcp import Client
client=Client(); toolset='EditorToolset.EditorAppToolset';client.describe(toolset)
result=client.call(toolset,'CaptureEditorImage',{})['returnValue']
path=Path(sys.argv[1]);path.parent.mkdir(parents=True,exist_ok=True)
path.write_bytes(base64.b64decode(result['data']))
print(json.dumps({'path':str(path),'bytes':path.stat().st_size}))
