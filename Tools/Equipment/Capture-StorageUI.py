"""Capture actual editor Slate/UMG through native MCP, without HighResShot."""
import argparse
import base64
from pathlib import Path
from UnrealMcp import Client

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
client = Client()
client.describe('EditorToolset.EditorAppToolset')
result = client.call('EditorToolset.EditorAppToolset', 'CaptureEditorImage', {})['returnValue']
if result['mimeType'] != 'image/png':
    raise RuntimeError('Unexpected editor capture format: ' + result['mimeType'])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(base64.b64decode(result['data'], validate=True))
print(args.output)
