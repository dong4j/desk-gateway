"""Desk Gateway MCP REST 客户端的本地 Mock 测试。"""

from __future__ import annotations

import json
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

INTEGRATION_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(INTEGRATION_DIR))

from desk_gateway_client import (
    DeskGatewayClient,
    DeskGatewayConfig,
    DeskGatewayError,
)


def safe_status(**overrides: Any) -> dict[str, Any]:
    """构造允许有界运动的最小真实状态。"""

    status: dict[str, Any] = {
        "status": "idle",
        "height_mm": 870,
        "max_height_mm": 940,
        "preset1_height_mm": 560,
        "preset4_height_mm": 870,
        "height_sim": False,
        "height_known": True,
        "tof_height_known": True,
        "raise_to_max_supported": True,
        "child_lock": False,
        "upward_blocked": False,
        "control_sources": {"rest": True},
    }
    status.update(overrides)
    return status


class MockDeskHandler(BaseHTTPRequestHandler):
    """记录请求并返回测试用 Desk Gateway JSON。"""

    server: MockDeskServer

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _record(self) -> None:
        self.server.requests.append(
            {
                "method": self.command,
                "path": self.path,
                "key": self.headers.get("X-Desk-Key"),
            }
        )

    def do_GET(self) -> None:
        self._record()
        if self.path != "/api/v1/desk/status":
            self._send_json(404, {"error": "not_found"})
            return
        self._send_json(self.server.get_status_code, self.server.status_payload)

    def do_POST(self) -> None:
        self._record()
        self._send_json(self.server.post_status_code, self.server.post_payload)

    def log_message(self, _format: str, *args: object) -> None:
        """测试不输出 HTTP access log。"""


class MockDeskServer(ThreadingHTTPServer):
    """保存 Mock 响应和收到的请求。"""

    requests: list[dict[str, Any]]
    status_payload: dict[str, Any]
    get_status_code: int
    post_payload: dict[str, Any]
    post_status_code: int


class DeskGatewayClientTests(unittest.TestCase):
    """验证 MCP 工具只调用固定 REST 路径并正确执行安全门禁。"""

    def setUp(self) -> None:
        self.server = MockDeskServer(("127.0.0.1", 0), MockDeskHandler)
        self.server.requests = []
        self.server.status_payload = safe_status()
        self.server.get_status_code = 200
        self.server.post_payload = {"ok": True, "err": "ESP_OK"}
        self.server.post_status_code = 200
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            name="desk-mcp-test-server",
            daemon=True,
        )
        self.thread.start()
        host, port = self.server.server_address
        self.client = DeskGatewayClient(
            DeskGatewayConfig(
                base_url=f"http://{host}:{port}",
                api_key="test-desk-key",
                timeout_seconds=1,
            )
        )

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def test_raise_to_max_uses_bounded_route_and_authentication(self) -> None:
        result = self.client.raise_to_max()

        self.assertEqual("started", result["state"])
        self.assertEqual(
            [
                {
                    "method": "GET",
                    "path": "/api/v1/desk/status",
                    "key": "test-desk-key",
                },
                {
                    "method": "POST",
                    "path": "/api/v1/desk/raise-to-max",
                    "key": "test-desk-key",
                },
            ],
            self.server.requests,
        )

    def test_raise_to_max_is_idempotent_at_maximum_height(self) -> None:
        self.server.status_payload = safe_status(height_mm=940)

        result = self.client.raise_to_max()

        self.assertEqual("already_at_max", result["state"])
        self.assertEqual(1, len(self.server.requests))
        self.assertEqual("GET", self.server.requests[0]["method"])

    def test_raise_to_max_fails_closed_for_unsafe_status(self) -> None:
        unsafe_states = {
            "unsupported": {"raise_to_max_supported": False},
            "moving": {"status": "moving_up"},
            "simulated": {"height_sim": True},
            "unknown_height": {"height_known": False},
            "unknown_tof_height": {"tof_height_known": False},
            "blocked": {"upward_blocked": True},
            "locked": {"child_lock": True},
            "rest_disabled": {"control_sources": {"rest": False}},
        }

        for name, overrides in unsafe_states.items():
            with self.subTest(name=name):
                self.server.requests = []
                self.server.status_payload = safe_status(**overrides)
                with self.assertRaises(DeskGatewayError):
                    self.client.raise_to_max()
                self.assertEqual(1, len(self.server.requests))
                self.assertEqual("GET", self.server.requests[0]["method"])

    def test_presets_use_fixed_routes(self) -> None:
        sit = self.client.goto_sit()
        stand = self.client.goto_stand()

        self.assertEqual(560, sit["target_height_mm"])
        self.assertEqual(870, stand["target_height_mm"])
        post_paths = [
            request["path"]
            for request in self.server.requests
            if request["method"] == "POST"
        ]
        self.assertEqual(
            [
                "/api/v1/desk/preset/1/goto",
                "/api/v1/desk/preset/4/goto",
            ],
            post_paths,
        )

    def test_stop_does_not_depend_on_motion_preflight(self) -> None:
        self.server.status_payload = safe_status(
            child_lock=True,
            control_sources={"rest": False},
        )

        result = self.client.stop()

        self.assertEqual({"ok": True, "state": "stopped"}, result)
        self.assertEqual(
            [
                {
                    "method": "POST",
                    "path": "/api/v1/desk/stop",
                    "key": "test-desk-key",
                }
            ],
            self.server.requests,
        )

    def test_http_failure_does_not_expose_api_key(self) -> None:
        self.server.get_status_code = 401
        self.server.status_payload = {"error": "unauthorized"}

        with self.assertRaises(DeskGatewayError) as raised:
            self.client.get_status()

        self.assertIn("HTTP 401", str(raised.exception))
        self.assertNotIn("test-desk-key", str(raised.exception))

    def test_environment_configuration_rejects_api_paths(self) -> None:
        with self.assertRaises(DeskGatewayError):
            DeskGatewayConfig.from_env(
                {
                    "DESK_GATEWAY_URL": "http://127.0.0.1/api/v1",
                    "DESK_GATEWAY_KEY": "key",
                }
            )


if __name__ == "__main__":
    unittest.main()
