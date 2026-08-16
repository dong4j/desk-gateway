"""Desk Gateway REST 的固定语义客户端。

该模块只暴露小智语音控制需要的五个动作。REST 路径、HTTP Method 和认证头均由
代码固定，不能由模型参数覆盖，避免把通用 HTTP 能力暴露给 MCP 调用方。
"""

from __future__ import annotations

import json
import os
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


class DeskGatewayError(RuntimeError):
    """Desk Gateway 配置、网络或安全门禁失败。"""


@dataclass(frozen=True)
class DeskGatewayConfig:
    """桥接进程使用的 Desk Gateway 固定连接配置。"""

    base_url: str
    api_key: str
    timeout_seconds: float = 5.0

    @classmethod
    def from_env(cls, env: Mapping[str, str] | None = None) -> DeskGatewayConfig:
        """从环境变量读取配置，并在 MCP Server 启动前完成校验。"""

        values = os.environ if env is None else env
        base_url = values.get("DESK_GATEWAY_URL", "").strip().rstrip("/")
        api_key = values.get("DESK_GATEWAY_KEY", "").strip()
        timeout_text = values.get("DESK_HTTP_TIMEOUT_SECONDS", "5").strip()

        if not base_url:
            raise DeskGatewayError("missing environment variable: DESK_GATEWAY_URL")
        if not api_key:
            raise DeskGatewayError("missing environment variable: DESK_GATEWAY_KEY")

        parsed = urlsplit(base_url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise DeskGatewayError("DESK_GATEWAY_URL must be an http(s) origin")
        if parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise DeskGatewayError(
                "DESK_GATEWAY_URL must not contain credentials, query, or fragment"
            )
        if parsed.path not in {"", "/"}:
            raise DeskGatewayError("DESK_GATEWAY_URL must not contain an API path")

        try:
            timeout_seconds = float(timeout_text)
        except ValueError as exc:
            raise DeskGatewayError(
                "DESK_HTTP_TIMEOUT_SECONDS must be a number"
            ) from exc
        if not 0 < timeout_seconds <= 30:
            raise DeskGatewayError("DESK_HTTP_TIMEOUT_SECONDS must be within (0, 30]")

        return cls(
            base_url=base_url,
            api_key=api_key,
            timeout_seconds=timeout_seconds,
        )


class DeskGatewayClient:
    """将 MCP 工具调用映射为受控的 Desk Gateway REST 请求。"""

    def __init__(self, config: DeskGatewayConfig) -> None:
        self._config = config

    def _request(self, method: str, path: str) -> dict[str, Any]:
        """调用代码内固定的 REST 路径，并把失败统一转换为工具错误。"""

        request = Request(
            f"{self._config.base_url}{path}",
            data=b"" if method == "POST" else None,
            method=method,
            headers={
                "Accept": "application/json",
                "X-Desk-Key": self._config.api_key,
            },
        )
        try:
            with urlopen(request, timeout=self._config.timeout_seconds) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")[:512]
            raise DeskGatewayError(f"Desk Gateway HTTP {exc.code}: {body}") from exc
        except (URLError, TimeoutError) as exc:
            raise DeskGatewayError(f"Desk Gateway unavailable: {exc}") from exc
        except json.JSONDecodeError as exc:
            raise DeskGatewayError("Desk Gateway returned invalid JSON") from exc

        if not isinstance(payload, dict):
            raise DeskGatewayError("Desk Gateway returned a non-object response")
        if method == "POST" and payload.get("ok") is not True:
            raise DeskGatewayError(f"Desk Gateway rejected command: {payload}")
        return payload

    def _status_preflight(self) -> dict[str, Any]:
        """运动前检查公共策略；ESP32 执行时仍会再次做最终裁决。"""

        status = self.get_status()
        if status.get("child_lock") is True:
            raise DeskGatewayError("Desk Gateway child lock is enabled")
        control_sources = status.get("control_sources")
        if not isinstance(control_sources, dict) or (
            control_sources.get("rest") is not True
        ):
            raise DeskGatewayError("Desk Gateway REST control source is disabled")
        return status

    @staticmethod
    def _required_height(status: Mapping[str, Any], name: str) -> int:
        """读取可信高度值，拒绝缺失值、布尔值和非整数。"""

        value = status.get(name)
        if isinstance(value, bool) or not isinstance(value, int):
            raise DeskGatewayError(f"Desk Gateway status is missing {name}")
        return value

    def get_status(self) -> dict[str, Any]:
        """查询当前高度、运动状态、安全配置与 Driver 能力。"""

        return self._request("GET", "/api/v1/desk/status")

    def raise_to_max(self) -> dict[str, Any]:
        """请求设备侧有界上升，禁止退化为手动持续上升。"""

        status = self._status_preflight()
        if status.get("raise_to_max_supported") is not True:
            raise DeskGatewayError("Desk Gateway does not support bounded raise-to-max")
        if status.get("status") != "idle":
            raise DeskGatewayError(
                f"Desk is not idle: {status.get('status', 'unknown')}"
            )
        if status.get("height_sim") is True:
            raise DeskGatewayError("Desk Gateway is using simulated height")
        if status.get("height_known") is not True or (
            status.get("tof_height_known") is not True
        ):
            raise DeskGatewayError("Desk height sensor is unavailable")
        if status.get("upward_blocked") is True:
            raise DeskGatewayError(
                "Upward motion is blocked by the local safety policy"
            )

        height_mm = self._required_height(status, "height_mm")
        max_height_mm = self._required_height(status, "max_height_mm")
        if height_mm >= max_height_mm:
            return {
                "ok": True,
                "state": "already_at_max",
                "height_mm": height_mm,
                "max_height_mm": max_height_mm,
            }

        self._request("POST", "/api/v1/desk/raise-to-max")
        return {
            "ok": True,
            "state": "started",
            "message": "已经开始上升，将由设备在安全上限自动停止",
            "start_height_mm": height_mm,
            "max_height_mm": max_height_mm,
        }

    def goto_sit(self) -> dict[str, Any]:
        """闭环前往 Desk Gateway 档位 1。"""

        status = self._status_preflight()
        self._request("POST", "/api/v1/desk/preset/1/goto")
        return {
            "ok": True,
            "state": "started",
            "target": "sit",
            "target_height_mm": status.get("preset1_height_mm"),
        }

    def goto_stand(self) -> dict[str, Any]:
        """闭环前往 Desk Gateway 档位 4。"""

        status = self._status_preflight()
        self._request("POST", "/api/v1/desk/preset/4/goto")
        return {
            "ok": True,
            "state": "started",
            "target": "stand",
            "target_height_mm": status.get("preset4_height_mm"),
        }

    def stop(self) -> dict[str, Any]:
        """立即停止；STOP 不受运动前置检查阻塞。"""

        self._request("POST", "/api/v1/desk/stop")
        return {"ok": True, "state": "stopped"}
