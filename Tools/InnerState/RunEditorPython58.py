"""Run a project-owned editor script through UE's official localhost Python channel.

Native MCP first establishes target identity/settings. Never saves remote-execution
settings, and restores the original enabled flag on exit. No source-tree access.
"""
import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('D:/Unreal Engine 5/Library/UE_5.8')
sys.path.insert(0, str(ROOT / 'Tools/Equipment'))
sys.path.insert(0, str(ENGINE / 'Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python'))
from UnrealMcp import Client
import remote_execution


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('script', type=Path)
    args = parser.parse_args()
    script = args.script.resolve(strict=True)
    if not script.is_relative_to(ROOT) or script.suffix != '.py':
        raise SystemExit('Only an existing .py inside the writable target is allowed.')
    client = Client()
    ts = 'editor_toolset.toolsets.object.ObjectTools'
    client.describe(ts)
    ref = {'refPath': '/Script/PythonScriptPlugin.Default__PythonScriptPluginSettings'}
    before = json.loads(client.call(ts, 'get_properties', {'instance': ref, 'properties':
        ['bRemoteExecution', 'RemoteExecutionMulticastBindAddress', 'RemoteExecutionMulticastTtl']})['returnValue'])
    if before['RemoteExecutionMulticastBindAddress'] != '127.0.0.1' or before['RemoteExecutionMulticastTtl'] != 0:
        raise SystemExit('Expected localhost-only editor Python configuration.')
    remote = remote_execution.RemoteExecution()
    try:
        if not before['bRemoteExecution']:
            client.call(ts, 'set_properties', {'instance': ref, 'values': json.dumps({'bRemoteExecution': True})})
        remote.start()
        deadline = time.monotonic() + 5
        nodes = []
        while not nodes and time.monotonic() < deadline:
            time.sleep(.2)
            nodes = [n for n in remote.remote_nodes if Path(n.get('project_root', '')).resolve() == ROOT]
        if len(nodes) != 1:
            raise RuntimeError('Expected exactly one target editor Python node.')
        remote.open_command_connection(nodes[0]['node_id'])
        result = remote.run_command(f"exec(compile(open({str(script)!r}, encoding='utf-8').read(), {str(script)!r}, 'exec'))",
                                    exec_mode=remote_execution.MODE_EXEC_STATEMENT)
        print(json.dumps({'success': result.get('success'), 'result': result.get('result'), 'output': result.get('output')}, indent=2))
        if not result.get('success'):
            raise SystemExit(1)
    finally:
        remote.stop()
        if not before['bRemoteExecution']:
            client.call(ts, 'set_properties', {'instance': ref, 'values': json.dumps({'bRemoteExecution': False})})


if __name__ == '__main__':
    main()
