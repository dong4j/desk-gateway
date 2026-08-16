"""向小智 AI 暴露 Desk Gateway 的固定 MCP 工具。"""

from desk_gateway_client import DeskGatewayClient, DeskGatewayConfig
from mcp.server.fastmcp import FastMCP


def create_mcp_server() -> FastMCP:
    """创建 MCP Server，并在注册工具前校验所有运行配置。"""

    client = DeskGatewayClient(DeskGatewayConfig.from_env())
    server = FastMCP("DeskGateway")

    @server.tool(name="desk.get_status")
    def get_status() -> dict:
        """查询升降桌状态、高度、安全上限、档位和传感器状态。"""

        return client.get_status()

    @server.tool(name="desk.raise_to_max")
    def raise_to_max() -> dict:
        """仅在用户明确要求升到最高或升到顶时调用。"""

        return client.raise_to_max()

    @server.tool(name="desk.goto_sit")
    def goto_sit() -> dict:
        """用户明确要求坐姿或档位一时，闭环前往坐姿高度。"""

        return client.goto_sit()

    @server.tool(name="desk.goto_stand")
    def goto_stand() -> dict:
        """用户明确要求站姿或档位四时，闭环前往站姿高度。"""

        return client.goto_stand()

    @server.tool(name="desk.stop")
    def stop() -> dict:
        """用户要求停止、停下或紧急停止时立即调用。"""

        return client.stop()

    return server


if __name__ == "__main__":
    create_mcp_server().run(transport="stdio")
