# 参与 Desk Gateway

**语言：** [English](./CONTRIBUTING.md) · 简体中文

欢迎提 Issue / PR。

参与本项目须遵守 [行为准则](./CODE_OF_CONDUCT.md)。潜在漏洞按 [SECURITY.zh-CN.md](./SECURITY.zh-CN.md)
处理，**不要**发到公开 Issue。

提交问题前先读 [SUPPORT.zh-CN.md](./SUPPORT.zh-CN.md) 并选对模板。要把 Web、脚本、手机、Watch、键盘
接到**你自己的**局域网，按 [本地多端部署清单](./docs/guides/local-multi-client-setup.md) 改 IP、密码和路径。

## 怎么贡献

1. 非琐碎改动先开 Issue。
2. Fork 后开主题分支。
3. diff 保持聚焦，不要夹带无关重构。
4. 按下面「分层检查」跑过对应命令再开 PR。
5. 填完整 PR 模板，写清自动化和真机验证边界。
6. 协议发现记在 `docs/`，写明已验证 vs 未知。入口见 [docs/README.zh-CN.md](./docs/README.zh-CN.md)；
   用法改动放 `docs/guides/control-methods.md` 或 `docs/guides/rest-api.md`。
7. 用户可见变化写入 [CHANGELOG.md](./CHANGELOG.md)。

## ESP-IDF（固件）

本仓库固定 **ESP-IDF v6.0.2**，目标芯片 `esp32s3`，不要混用其他版本。

按乐鑫文档安装：
[Windows / macOS / Linux 工具链](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.2/esp32s3/get-started/index.html)。

安装后在**同一个 Shell**里：

```bash
export IDF_PATH=/path/to/esp-idf   # 必须是 v6.0.2
source "${IDF_PATH}/export.sh"
idf.py --version                   # 必须输出: ESP-IDF v6.0.2
./scripts/check-firmware.sh
```

`./scripts/flash-firmware.sh <串口>` 会优先用环境里的 `IDF_PATH` / `IDF_PYTHON_ENV_PATH`。
两者都空时，才回退到维护者本机的 Espressif 路径。别人克隆后必须自己设置 `IDF_PATH`。

不要提交 `build/`、`sdkconfig`、`managed_components/` 或密钥。
配网优先用 SoftAP，不要依赖脆弱的串口输密码。
禁止臆造未验证键码（例如原厂面板档位 2 / 3）。

## 分层检查

| 层 | 命令 |
|---|---|
| 固件 host 测试 + 隔离构建 | `./scripts/check-firmware.sh`（需要 IDF v6.0.2） |
| 仅 host 测试 | `./scripts/check-height-decoder.sh` |
| 手机 App | `cd mobile/app && npm ci && npm run typecheck && npm test` |
| Apple Watch | `cd mobile/watch && swift test` |
| 小智 MCP | `python3 -m unittest discover -s integrations/xiaozhi-mcp/tests -p 'test_*.py' -v` |
| Ulanzi 插件逻辑 | `cd integrations/ulanzi-d200h && npm ci && npm test` |

编译或单元测试不能代替真桌运动、BLE 射频或 ToF。

## 代码风格

- 跟随仓库现有 C / CMake / Web 风格
- 新模块写清「为什么」和约束
- 标识符用英文；给人看的文档可以中英
- 有 README 的目录：英文 `README.md`，中文 `README.zh-CN.md`

## Pull request

- 写清改了什么、为什么
- 关联 Issue
- 附简短测试计划（构建 / 烧录 / SoftAP / Web 按住升降）
- 没做的真机验证不要写成已通过

## 许可证

提交即表示同意按 MIT License 授权，见 [LICENSE](./LICENSE)。
