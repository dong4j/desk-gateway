"""小智 MCP 启动脚本的 .env 加载测试。"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

INTEGRATION_DIR = Path(__file__).resolve().parents[1]
RUN_SCRIPT = INTEGRATION_DIR / "scripts" / "run.sh"


class RunScriptTests(unittest.TestCase):
    """验证单一 .env 能完整配置官方 MCP Pipe 子进程。"""

    def test_loads_shell_quoted_env_file(self) -> None:
        with tempfile.TemporaryDirectory(prefix="desk-mcp-run-") as temp_dir:
            temp_path = Path(temp_dir)
            pipe_script = temp_path / "mcp_pipe.py"
            pipe_script.write_text(
                textwrap.dedent(
                    """
                    import json
                    import os

                    print(json.dumps({
                        "endpoint": os.environ["MCP_ENDPOINT"],
                        "gateway_url": os.environ["DESK_GATEWAY_URL"],
                        "gateway_key": os.environ["DESK_GATEWAY_KEY"],
                        "timeout": os.environ["DESK_HTTP_TIMEOUT_SECONDS"],
                    }))
                    """
                ),
                encoding="utf-8",
            )
            config_file = temp_path / ".env"
            config_file.write_text(
                textwrap.dedent(
                    f"""
                    MCP_ENDPOINT='wss://example.test/mcp/?token=abc&mode=desk'
                    MCP_PIPE_DIR='{temp_path}'
                    MCP_PYTHON='{sys.executable}'
                    DESK_GATEWAY_URL='http://192.168.1.100'
                    DESK_GATEWAY_KEY='local test key'
                    DESK_HTTP_TIMEOUT_SECONDS='7'
                    """
                ),
                encoding="utf-8",
            )

            clean_env = os.environ.copy()
            for name in (
                "MCP_ENDPOINT",
                "MCP_PIPE_DIR",
                "MCP_PYTHON",
                "DESK_GATEWAY_URL",
                "DESK_GATEWAY_KEY",
                "DESK_HTTP_TIMEOUT_SECONDS",
                "MCP_ENDPOINT_KEYCHAIN_SERVICE",
                "DESK_GATEWAY_KEYCHAIN_SERVICE",
            ):
                clean_env.pop(name, None)
            clean_env["DESK_MCP_CONFIG"] = str(config_file)

            completed = subprocess.run(
                [str(RUN_SCRIPT)],
                cwd=INTEGRATION_DIR,
                env=clean_env,
                check=True,
                capture_output=True,
                text=True,
            )

        payload = json.loads(completed.stdout)
        self.assertEqual(
            "wss://example.test/mcp/?token=abc&mode=desk",
            payload["endpoint"],
        )
        self.assertEqual("http://192.168.1.100", payload["gateway_url"])
        self.assertEqual("local test key", payload["gateway_key"])
        self.assertEqual("7", payload["timeout"])


if __name__ == "__main__":
    unittest.main()
