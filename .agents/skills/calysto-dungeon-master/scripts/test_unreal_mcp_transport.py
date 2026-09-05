"""Exercise the PowerShell MCP transport against local mocks, never Unreal."""

from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time
import unittest


HELPER = Path(__file__).with_name("Invoke-UnrealMcpMetaTool.ps1")
POWERSHELL = shutil.which("powershell") or shutil.which("pwsh")


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        try:
            request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            self.server.methods.append(request["method"])
            mode = self.server.mode
            if mode == "slow_chain":
                self.server.release.wait(0.85)
            if request["method"] == "initialize":
                payload = {"jsonrpc": "2.0", "id": 1, "result": {"protocolVersion": "2025-11-25"}}
            elif request["method"] == "notifications/initialized":
                payload = {}
            else:
                payload = {"jsonrpc": "2.0", "id": 2, "result": {"content": [{"type": "text", "text": "mock-ok"}]}}
                if mode == "rpc_error":
                    payload = {"jsonrpc": "2.0", "id": 2, "error": {"code": -32000, "message": "mock-rpc-error"}}
                elif mode == "tool_error":
                    payload["result"]["isError"] = True
                    payload["result"]["content"][0]["text"] = "mock-tool-error"
                if mode == "stall_headers":
                    self.server.release.wait(5)
            body = json.dumps(payload).encode("utf-8")
            is_call = request["method"] == "tools/call"
            is_sse = is_call and mode in ("sse", "stall_sse")
            if is_sse:
                body = b"data: " + body + b"\n\n"
            self.send_response(200)
            self.send_header("Mcp-Session-Id", "mock-session")
            self.send_header("Content-Type", "text/event-stream" if is_sse else "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if is_call and mode in ("stall_body", "stall_sse"):
                self.wfile.flush()
                self.server.release.wait(5)
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass  # The deadline tests deliberately close the client socket.


@contextmanager
def mock_server(mode):
    server = ThreadingHTTPServer(("127.0.0.1", 0), MockHandler)
    server.mode = mode
    server.methods = []
    server.release = threading.Event()
    worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
    worker.start()
    try:
        yield server
    finally:
        server.release.set()
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)


@unittest.skipUnless(POWERSHELL, "PowerShell is required for the actual transport tests")
class McpTransportTests(unittest.TestCase):
    def invoke(self, server, timeout=3, raw=False):
        command = [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HELPER),
                   "-ToolName", "list_toolsets", "-ArgumentsJson", "{}", "-Uri",
                   f"http://127.0.0.1:{server.server_port}/mcp", "-TimeoutSec", str(timeout)]
        if raw:
            command.append("-Raw")
        start = time.monotonic()
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout + 5,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return result, time.monotonic() - start

    def test_json_success_performs_complete_handshake(self):
        with mock_server("json") as server:
            result, _ = self.invoke(server)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "mock-ok")
            self.assertEqual(server.methods, ["initialize", "notifications/initialized", "tools/call"])

    def test_raw_success_is_valid_json(self):
        with mock_server("json") as server:
            result, _ = self.invoke(server, raw=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["result"]["content"][0]["text"], "mock-ok")

    def test_sse_success(self):
        with mock_server("sse") as server:
            result, _ = self.invoke(server)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "mock-ok")

    def test_rpc_and_tool_errors_fail_including_raw_mode(self):
        for mode, expected in (("rpc_error", "mock-rpc-error"), ("tool_error", "mock-tool-error")):
            for raw in (False, True):
                with self.subTest(mode=mode, raw=raw), mock_server(mode) as server:
                    result, _ = self.invoke(server, raw=raw)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stderr)

    def test_headers_json_and_sse_body_stalls_fail_within_shared_bound(self):
        for mode in ("stall_headers", "stall_body", "stall_sse"):
            with self.subTest(mode=mode), mock_server(mode) as server:
                result, elapsed = self.invoke(server, timeout=1)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("tools/call", server.methods)
                self.assertLess(elapsed, 3.5)
                self.assertTrue("timed out" in result.stderr or "deadline" in result.stderr or "canceled" in result.stderr,
                                result.stderr)

    def test_initialize_notify_and_call_share_one_deadline(self):
        with mock_server("slow_chain") as server:
            result, elapsed = self.invoke(server, timeout=2)
            self.assertNotEqual(result.returncode, 0)
            self.assertGreaterEqual(len(server.methods), 2)
            self.assertLess(elapsed, 4.5)


if __name__ == "__main__":
    unittest.main()
