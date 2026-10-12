"""Small native MCP client for bounded editor tool discovery and calls."""
import json
import urllib.request


class Client:
    def __init__(self, request_timeout=45):
        if not 0 < request_timeout <= 120:
            raise ValueError('MCP request timeout must be within (0, 120] seconds')
        self.request_timeout = request_timeout
        self.session = None
        self.sequence = 0
        self.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                   'clientInfo': {'name': 'EquipmentWorkbench', 'version': '1'}})
        self.request('notifications/initialized', {}, notification=True)
        self.tool('list_toolsets', {})

    def request(self, method, params, notification=False):
        self.sequence += 1
        body = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if not notification:
            body['id'] = self.sequence
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
        if self.session:
            headers['Mcp-Session-Id'] = self.session
        request = urllib.request.Request('http://127.0.0.1:8000/mcp', json.dumps(body).encode(), headers)
        with urllib.request.urlopen(request, timeout=self.request_timeout) as response:
            self.session = response.headers.get('Mcp-Session-Id', self.session)
            if 'text/event-stream' in response.headers.get('Content-Type', ''):
                for line in response:
                    if line.startswith(b'data:'):
                        result = json.loads(line[5:]); break
                else:
                    return None
            else:
                data = response.read()
                if not data: return None
                result = json.loads(data)
        if 'error' in result: raise RuntimeError(result['error'])
        return result.get('result')

    def tool(self, name, arguments):
        result = self.request('tools/call', {'name': name, 'arguments': arguments})
        if result.get('isError'): raise RuntimeError(result)
        value = result.get('content', [{}])[0].get('text', '')
        try: return json.loads(value)
        except ValueError: return value

    def describe(self, toolset):
        return self.tool('describe_toolset', {'toolset_name': toolset})

    def call(self, toolset, tool, arguments):
        return self.tool('call_tool', {'toolset_name': toolset, 'tool_name': tool, 'arguments': arguments})


if __name__ == '__main__':
    import sys
    client = Client()
    description = client.describe(sys.argv[1])
    print(json.dumps(client.call(sys.argv[1], sys.argv[2], json.loads(sys.argv[3]))
                     if len(sys.argv) > 2 else description, indent=2))
