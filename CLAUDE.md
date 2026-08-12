# Desk Gateway Claude Instructions

## 项目开发规范（强制）

所有 AI Agent 在开始分析、修改、测试或提交代码前，必须阅读并遵守 [`docs/standards/`](docs/standards/README.md) 下的全部规范。目录中的规范适用于后续所有开发任务，不得跳过。

- [代码提交规范](docs/standards/git-commit-convention.md)

## ESP-IDF 固件构建环境（强制）

本项目固定使用 ESP-IDF v6.0.2，Python venv 位于项目机器的非默认目录。执行任何 ESP32 固件构建、检查、烧录或监视命令前，必须先在**同一个 Shell 进程**中加载以下环境：

```bash
export IDF_PYTHON_ENV_PATH=/Users/dong4j/.espressif/tools/python/v6.0.2/venv
export IDF_PYTHON_CHECK_CONSTRAINTS=no
source /Users/dong4j/.espressif/v6.0.2/esp-idf/export.sh >/dev/null
```

执行隔离固件构建时，直接使用下面这条完整命令：

```bash
zsh -lc 'export IDF_PYTHON_ENV_PATH=/Users/dong4j/.espressif/tools/python/v6.0.2/venv IDF_PYTHON_CHECK_CONSTRAINTS=no; source /Users/dong4j/.espressif/v6.0.2/esp-idf/export.sh >/dev/null && ./scripts/check-firmware.sh'
```

- 禁止先在未激活 ESP-IDF 的终端中执行一次构建，再根据失败结果补环境重试。
- 激活后必须先确认 `idf.py --version` 输出 `ESP-IDF v6.0.2`；未确认版本不得开始构建。
- `ESP-IDF not active`、`IDF_PATH` 缺失或 Python venv 路径错误属于环境准备失败，不能记为代码构建失败。
- 只有进入 CMake/Ninja 编译阶段后产生的错误，才能归因于固件代码或构建配置。
