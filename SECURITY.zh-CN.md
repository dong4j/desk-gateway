# 安全政策

**语言：** [English](./SECURITY.md) · 简体中文

## 支持的版本

本项目处于早期阶段。安全修复按尽力原则打在默认分支上，不承诺维护多条发布线。

## 如何报告漏洞

请**不要**把可能危害用户的漏洞发到公开 Issue，也不要附带利用细节。例如：

- 局域网 Web 鉴权绕过或未授权控桌
- 凭证、密钥或私有网络数据泄露
- 可能导致桌子失控或异常长时间运动的协议行为
- 绕过停止、超时或其他运动安全控制

**优先：** 请用本仓库已打开的 GitHub
[私下报告漏洞](https://docs.github.com/zh/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-vulnerability)
（Security 页 → Report a vulnerability）。

**否则：** 使用 [维护者 GitHub 主页](https://github.com/dong4j) 上公布的私信方式。
若没有私信渠道，请开 `Private contact request` 表单，**不要**写技术细节、日志、抓包或 PoC。
然后再约定私密通道。

报告请尽量包含：

- 已知的 commit / tag
- 最小复现步骤和所需硬件
- 影响（例如局域网未认证即可控桌）
- 是在真机上验证，还是仅模拟
- 若有修复建议或披露约束，一并说明

早期项目，处理为尽力而为，不保证响应或修复时限。请给维护者调查和协调披露的时间，不要抢先公开细节。

## 使用者侧加固

- 首次登录后改掉默认 Web 密码（`desk-gateway`）
- 设备留在可信局域网；**不要**做公网端口映射或把 HTTP 暴露到互联网
- 发运动命令时人要在桌旁；运动超时只是后盾，不是唯一安全措施
- 逆向协议视为不完整；控制盒若换成未记录的固件，行为可能不同
