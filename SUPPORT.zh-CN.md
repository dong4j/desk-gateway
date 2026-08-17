# 支持

**语言：** [English](./SUPPORT.md) · 简体中文

Desk Gateway 是早期、社区维护的硬件项目。支持按尽力原则提供，不承诺响应时限，也没有一对一私密支持通道。

## 提问前

1. 先读 [README.zh-CN.md](README.zh-CN.md)、
   [文档索引](docs/README.zh-CN.md)、
   [本地多端部署清单](docs/guides/local-multi-client-setup.md)、
   [多种方式控制升降桌](docs/guides/control-methods.md)、
   [真机验收清单](docs/guides/bringup-checklist.md)，以及 `docs/` 里相关协议笔记。
2. 搜索已有 Issue，看是否同一硬件、同一固件或同一现象。
3. 固件构建失败时运行 `./scripts/check-firmware.sh`（需要 ESP-IDF v6.0.2）。
4. 分享日志或抓包前去掉 Wi-Fi 密码、token、MAC 和其他隐私数据。

## 选对渠道

| 需求 | 渠道 |
|------|------|
| 文档写明的行为失败，或有可复现缺陷 | 用 **Bug report** 表单开 Issue |
| 缺能力、缺 Driver、缺集成或文档流程 | 用 **Feature request** 表单开 Issue |
| 一般安装或用法问题 | 先查文档和已有 Issue；若文档路径失败，再用 Bug report 写全环境 |
| 潜在漏洞或不安全的未授权控制 | 按 [SECURITY.zh-CN.md](SECURITY.zh-CN.md)，**不要**把细节发到 Issue |
| 社区行为问题 | 按 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) |

## 硬件和协议证据

报告桌体运动、Driver 或逆向协议时，请带上：

- 控制盒 / 面板型号和 ESP32 开发板
- 固件 commit 或 tag，以及相关配置改动
- 接线和供电方式（不要暴露私有网络凭证）
- 最小复现步骤和实际结果
- 脱敏后的串口日志、逻辑分析仪抓包或照片（如有）
- 哪些是真机验证，哪些仍是推测

测试运动时人要在桌旁，并保留物理停止或断电手段。不要把固件超时当成唯一安全措施。
