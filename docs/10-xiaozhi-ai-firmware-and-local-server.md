# 小智 AI 硬件固件与本地 Server 部署

| 项 | 内容 |
| --- | --- |
| 文档编号 | DG-XIAOZHI-001 |
| 版本 | 0.1.0 |
| 日期 | 2026-08-15 |
| 状态 | 调研完成，固件刷写和服务部署待实机执行 |
| 适用硬件 | JC3636W518C、Xmini-C3 普通版 |

本文整理现有两台小智 AI 硬件的资料、固件选择、烧录方法，以及
`xiaozhi-esp32-server` 的本地部署和设备接入方式。文中的版本和链接以
2026-08-15 的仓库状态为基准；实际操作前应再检查最新 Release 是否改变了板卡命名或部署要求。

## 1. 结论

### 1.1 固件选择

| 硬件 | 已确认型号 | 推荐固件 | 不应刷写 |
| --- | --- | --- | --- |
| 1.8 inch 圆屏 | `JC3636W518C`，ESP32-S3R8、16 MB Flash、8 MB PSRAM | 产品标签批次号不大于 `2528` 时使用 [`v2.4.2_taiji-pi-s3.zip`](https://github.com/78/xiaozhi-esp32/releases/download/v2.4.2/v2.4.2_taiji-pi-s3.zip) | 未核对批次时不要刷 `taiji-pi-s3-pdm` |
| 1.8 inch 圆屏新批次 | 产品标签批次号大于 `2528` | [`v2.4.2_taiji-pi-s3-pdm.zip`](https://github.com/78/xiaozhi-esp32/releases/download/v2.4.2/v2.4.2_taiji-pi-s3-pdm.zip) | `taiji-pi-s3` 的 STD 麦克风版本 |
| 0.96 inch OLED 小板 | Xmini-C3 普通版，二维码内容为 `Xmini-C3-_250208_0104` | [`v2.4.2_xmini-c3.zip`](https://github.com/78/xiaozhi-esp32/releases/download/v2.4.2/v2.4.2_xmini-c3.zip) | [`v2.4.2_xmini-c3-v3.zip`](https://github.com/78/xiaozhi-esp32/releases/download/v2.4.2/v2.4.2_xmini-c3-v3.zip) |

当前小智官方最新 Release 是
[`v2.4.2`](https://github.com/78/xiaozhi-esp32/releases/tag/v2.4.2)，发布时间为 2026-08-06。
官方固件默认连接 `xiaozhi.me`；刷好官方固件后，可以在配网页面的“高级选项”中把 OTA
地址改为本地 Server，无需为了切换服务器重新编译固件。

### 1.2 源码和服务端边界

- 小智 ESP32 官方固件源码：[`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32)。
- 小智官方网站及控制台：[`xiaozhi.me`](https://xiaozhi.me/home/zh/)。
- 小智开发文档：[`xiaozhi.me/home/zh/docs`](https://xiaozhi.me/home/zh/docs/)。
- 本地兼容后端：[`xinnan-tech/xiaozhi-esp32-server`](https://github.com/xinnan-tech/xiaozhi-esp32-server)。

`xinnan-tech/xiaozhi-esp32-server` 是按小智通信协议实现的社区开源兼容后端，不是
`xiaozhi.me` 线上生产服务端的源码。它提供 Python Server、Java manager-api、Web
智控台、MySQL、Redis 和 MCP 等模块，适合在局域网内搭建独立服务。

## 2. JC3636W518C 硬件与原始资料

### 2.1 硬件信息

本地资料目录：

```text
/Users/dong4j/Synology/driver/Others/小智 AI
```

厂商 PDF 标识为深圳市晶彩智能有限公司，主要参数如下：

| 项目 | 参数 |
| --- | --- |
| SKU | `JC3636W518C` |
| 主控 | ESP32-S3R8，双核 240 MHz |
| 存储 | 16 MB Flash、8 MB PSRAM |
| 屏幕 | 1.8 inch、360 x 360、ST77916、QSPI |
| 触摸 | CST816 电容触摸 |
| 音频 | I2S 数字麦克风、I2S DAC |
| 其他 | TF 卡、无线供电、USB-C |

资料中没有给出由硬件厂商维护的公开 GitHub 仓库。可以使用以下三个不同性质的地址：

| 地址 | 性质 | 用途 |
| --- | --- | --- |
| [`guition.com`](https://www.guition.com/) | 晶彩智能厂商网站 | 厂商和产品资料入口 |
| [`78/xiaozhi-esp32/main/boards/taiji-pi-s3`](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/taiji-pi-s3) | 小智官方固件板卡适配源码 | 编译小智固件时的直接依据 |
| [`moononournation/JC3636W518`](https://github.com/moononournation/JC3636W518) | 社区示例，不是厂商官方仓库 | Arduino/LVGL/MJPEG 等硬件实验参考 |

不要把社区示例仓库描述为硬件厂商官方源码。当前最完整的原始硬件资料仍是本地保存的
PDF、原理图、Arduino Demo 和烧录包。

### 2.2 本地三个固件文件

已经在 ESP-IDF v6.0.2 环境中读取三个 BIN 的镜像头、分区表和 App 描述：

| 文件 | 内容 | 镜像信息 | 建议 |
| --- | --- | --- | --- |
| `小智固件/1.85_xiaozhi_new.bin` | 厂商提供的小智固件 | App `1.2.1`，ESP-IDF `v5.3.2`，构建于 2025-02-19，ESP32-S3，16 MB | 保留为原厂回退镜像，不作为当前首选 |
| `小智固件/xiaozhi-jc3636w518.bin` | 更早的小智固件 | App `1.1.2`，ESP-IDF `v5.4-dev-3951-g9106c43acc-dirty`，构建于 2025-02-12 | 不建议继续使用 |
| `9-烧录/Burn files/1.85_demo.bin` | 晶彩原始功能 Demo | Arduino 构建，ESP-IDF `v5.1.4-51-g442a798083-dirty`，构建于 2024-05-13 | 只用于恢复副屏、天气、音乐、相册等演示功能，不是小智固件 |

SHA-256：

```text
d81a5b21467b51941fd6e167392aa3d163b7c1b1c0ea68b42c1e32bdbff5b672  1.85_xiaozhi_new.bin
f7cbd69568c9ffe629f246fab1bfe0786d8fce2f511d10fcda8e714b311392d0  xiaozhi-jc3636w518.bin
ba39e0d0f1c631aa1433fd0269bf6e3940fdc1b3ecd597b18599e3964bb1e1ef  1.85_demo.bin
```

`2-样例/Demo_Arduino/ST77916_LVGL_DEMO` 中保存了硬件演示源码，原资料要求：

- Arduino ESP32 Core `3.0.1` 或以上；
- `ESP32_Display_Panel 0.1.4`；
- `ESP32_IO_Expander 0.0.2`；
- LVGL `8.4.0` 或以下。

这份 Arduino 示例可以用于屏幕和触摸开发，但不能证明它与 `1.85_demo.bin` 的完整产品
源码一一对应。原始 Demo 还包含 AIDA64 副屏、MP3、MJPEG、相册、天气和主题时钟等功能。

### 2.3 `taiji-pi-s3` 与 `taiji-pi-s3-pdm`

小智源码明确说明：2025 年 7 月之后，JC3636W518 更换了麦克风和屏幕玻璃。产品标签
批次号大于 `2528` 时必须选择 PDM 版本：

- 原麦克风版本：`taiji-pi-s3`，`CONFIG_TAIJIPAI_I2S_TYPE_STD=y`；
- 新麦克风版本：`taiji-pi-s3-pdm`，`CONFIG_TAIJIPAI_I2S_TYPE_PDM=y`。

当前本地原厂小智固件构建于 2025 年 2 月，说明配套资料来自更早批次，但最终仍以硬件
产品标签上的批次号为准。未发现批次号大于 `2528` 时，首选 `taiji-pi-s3`。

## 3. Xmini-C3 普通版

### 3.1 型号确认

这台设备是 Xmini-C3 普通版，不是 V3，依据如下：

1. 二维码内容为 `Xmini-C3-_250208_0104`，其中 `250208` 对应 2025-02-08；官方硬件页的
   V3.0 更新记录是 2025-07-17。
2. 实物照片中 RGB LED 和麦克风位于 PCB 顶部；V3.0 更新说明将 RGB LED 和 MIC 移到
   中间附近。
3. 普通版使用 `LGS4056`，V3 改为 `TP4057`；芯片丝印仍可作为最终交叉验证。

硬件及固件地址：

| 地址 | 用途 |
| --- | --- |
| [`oshwhub.com/tenclass01/xmini_c3`](https://oshwhub.com/tenclass01/xmini_c3) | Xmini-C3 官方开源硬件工程 |
| [`xiaozhi.me/home/zh/docs`](https://xiaozhi.me/home/zh/docs/) | 用户提供的 Xmini-C3/V3 教程来源 |
| [`main/boards/xmini/c3`](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/xmini/c3) | 普通版固件源码 |
| [`main/boards/xmini/c3-v3`](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/xmini/c3-v3) | V3 固件源码，仅用于对照 |

用户保存的旧教程以小智 v1.5.5/v1.8.8、ESP-IDF 5.3/5.4 为例。当前主线已经迁移到
ESP-IDF 6.0，官方首选稳定版为 ESP-IDF v6.0.2，因此不应继续按旧截图选择 SDK。

### 3.2 为什么不能试刷 V3 固件

普通版和 V3 的音频及 I2C 引脚不同。当前普通版源码还会在确认 ES8311 后执行：

```cpp
esp_efuse_write_field_bit(ESP_EFUSE_VDD_SPI_AS_GPIO);
```

这是不可逆的 eFuse 写入。官方源码注释明确提示，不兼容的板卡可能被永久损坏。因此：

- 本机只刷 `v2.4.2_xmini-c3.zip`；
- 不使用“普通版和 V3 各刷一次”的方式试错；
- 刷写前再次确认下载文件名中没有 `-v3` 或 `-4g`。

## 4. 固件备份和烧录

### 4.1 通用准备

1. 使用支持数据传输的 USB 线，不要使用只有供电功能的线。
2. 记录当前 Wi-Fi、设备绑定和服务器配置；写入完整合并镜像会覆盖相关分区。
3. 先备份整片 16 MB Flash，再写入新固件。
4. 下载 ZIP 后解压，官方 Release 包内的目标文件都叫 `merged-binary.bin`，不要混放。
5. 为解压目录增加板卡名称，例如 `taiji-pi-s3/merged-binary.bin` 和
   `xmini-c3/merged-binary.bin`。

在 macOS 上先确定串口：

```bash
ls /dev/cu.usb*
```

如果设备没有自动进入下载模式：

- 按住 `BOOT`；
- 插入 USB 或按一下 `RESET`；
- 识别到串口后释放 `BOOT`。

Xmini-C3 首次烧录时，可按住 `BOOT` 再打开板上电源开关。

下面的 CLI 命令需要在装有 `esptool` 的 Python 环境中执行。本机可以直接使用第 5 节的
ESP-IDF v6.0.2 环境；先运行 `python -m esptool version` 确认工具可用。

### 4.2 备份原始固件

以下命令中的串口需要替换为实际值。JC3636W518C：

```bash
python -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX \
  read-flash 0x0 0x1000000 jc3636w518c-original-16m.bin
```

Xmini-C3：

```bash
python -m esptool \
  --chip esp32c3 \
  --port /dev/cu.usbmodemXXXX \
  read-flash 0x0 0x1000000 xmini-c3-original-16m.bin
```

备份完成后计算校验值并另行保存：

```bash
shasum -a 256 *-original-16m.bin
```

### 4.3 推荐方式：ESP Launchpad

1. 使用 Chrome 或 Edge 打开 [ESP Launchpad](https://espressif.github.io/esp-launchpad/)。
2. 点击连接，选择对应 USB 串口。
3. 选择解压后的 `merged-binary.bin`。
4. 起始地址填写 `0x0`。
5. 开始烧录，等待成功提示后断电重启。
6. 不要在写入过程中拔线、关浏览器或切断电源。

### 4.4 CLI 方式

JC3636W518C 标准麦克风批次：

```bash
python -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX \
  --baud 460800 \
  write-flash 0x0 taiji-pi-s3/merged-binary.bin
```

Xmini-C3 普通版：

```bash
python -m esptool \
  --chip esp32c3 \
  --port /dev/cu.usbmodemXXXX \
  --baud 460800 \
  write-flash 0x0 xmini-c3/merged-binary.bin
```

### 4.5 首次配网和官方服务验证

1. 重启后连接设备创建的 `Xiaozhi-XXXXXX` Wi-Fi 热点。
2. 如果没有自动弹出配网页面，打开 `http://192.168.4.1`。
3. 选择 2.4 GHz Wi-Fi，输入密码并保存。
4. 先保持默认 OTA 地址，通过 `xiaozhi.me` 完成一次对话，验证麦克风、喇叭、屏幕和网络。
5. 确认硬件正常后，再把 OTA 地址改成本地 Server。

将“刷写成功”和“硬件功能正常”分开验收。程序成功写入只证明 Flash 写入完成，不能证明
麦克风类型、喇叭、屏幕、触摸和服务器地址全部正确。

## 5. 从源码编译固件

当前源码首选 ESP-IDF v6.0.2。下面命令使用本机已经安装的固定环境：

```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32

export IDF_PYTHON_ENV_PATH=/Users/dong4j/.espressif/tools/python/v6.0.2/venv
export IDF_PYTHON_CHECK_CONSTRAINTS=no
source /Users/dong4j/.espressif/v6.0.2/esp-idf/export.sh
idf.py --version
```

必须先看到：

```text
ESP-IDF v6.0.2
```

然后按板卡构建。JC3636W518C 标准版：

```bash
python scripts/build.py taiji-pi-s3 --name taiji-pi-s3 --zip
```

JC3636W518C PDM 新批次：

```bash
python scripts/build.py taiji-pi-s3 --name taiji-pi-s3-pdm --zip
```

Xmini-C3 普通版：

```bash
python scripts/build.py xmini/c3 --name xmini-c3 --zip
```

构建脚本会选择目标芯片、应用板卡配置、执行构建并生成 `build/merged-binary.bin`；带
`--zip` 时还会在 `releases/` 下生成带版本和板卡名的 ZIP。

需要连接本地 Server 时，优先使用第 7 节的配网高级选项，不必修改源码。只有要固定默认
OTA 地址、修改 UI、唤醒词或板载 MCP 工具时，才需要自行编译固件。

## 6. 本地部署 `xiaozhi-esp32-server`

### 6.1 推荐架构

Mac 本地完整部署包括：

| 组件 | 端口 | 作用 |
| --- | ---: | --- |
| `xiaozhi-esp32-server` | `8000` | 设备 WebSocket 语音连接 |
| `xiaozhi-esp32-server` HTTP | `8003` | 视觉接口；简化部署时也可提供 OTA |
| `manager-api` + `manager-web` | `8002` | 智控台、设备管理、全模块 OTA 接口 |
| MySQL | 容器内部 `3306` | 用户、设备、智能体和模型配置 |
| Redis | 容器内部 `6379` | 缓存和服务协作 |
| SenseVoiceSmall | 本地模型文件 | 默认本地 ASR |

本文选择“全模块安装”，因为需要本地智控台、设备绑定、多智能体和后续 MCP 管理。全模块
部署时使用：

```text
智控台：    http://<Mac局域网IP>:8002
OTA：       http://<Mac局域网IP>:8002/xiaozhi/ota/
WebSocket： ws://<Mac局域网IP>:8000/xiaozhi/v1/
```

设备不能使用 `127.0.0.1`、`localhost` 或 Docker 容器名访问 Mac。必须填写 Mac 在同一
局域网内的实际地址，例如 `192.168.21.10`，并为 Mac 设置 DHCP 地址保留或静态地址。

### 6.2 Apple Silicon 注意事项

项目文档说明，从 `0.8.2` 开始，项目发布的 Docker 镜像只支持 x86。M2 Ultra 是
ARM64，不建议直接把发布镜像当作原生镜像运行。推荐从源码在本机编译 ARM64 镜像，避免
依赖 Docker 的 amd64 模拟层。

另一种方式是所有模块直接从源码运行，但需要分别维护 JDK 21、Maven、Node.js、Python
3.10、MySQL、Redis、Opus、FFmpeg 和前后端进程。对于第一套可复现环境，优先使用本机构建
的 ARM64 Docker 镜像。

### 6.3 获取源码并构建 ARM64 镜像

```bash
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server

docker build \
  -f Dockerfile-server \
  -t local/xiaozhi-esp32-server:arm64 .

docker build \
  -f Dockerfile-web \
  -t local/xiaozhi-esp32-server-web:arm64 .
```

进入部署目录：

```bash
cd main/xiaozhi-server
mkdir -p data models/SenseVoiceSmall uploadfile mysql/data
cp config_from_api.yaml data/.config.yaml
cp docker-compose_all.yml docker-compose.local.yml
```

下载默认本地 ASR 模型：

```bash
curl -L \
  https://modelscope.cn/models/iic/SenseVoiceSmall/resolve/master/model.pt \
  -o models/SenseVoiceSmall/model.pt
```

编辑 `docker-compose.local.yml`：

```yaml
services:
  xiaozhi-esp32-server:
    image: local/xiaozhi-esp32-server:arm64

  xiaozhi-esp32-server-web:
    image: local/xiaozhi-esp32-server-web:arm64
```

同时修改示例中的 MySQL 默认密码 `123456`。以下两个值必须保持一致：

```yaml
- SPRING_DATASOURCE_DRUID_PASSWORD=<本地数据库强密码>
- MYSQL_ROOT_PASSWORD=<同一个本地数据库强密码>
```

不要提交 `data/.config.yaml`、模型 API Key、数据库密码或 `server.secret`。

### 6.4 第一次启动

```bash
docker compose -f docker-compose.local.yml up -d
docker compose -f docker-compose.local.yml ps
```

第一次启动时，`xiaozhi-esp32-server` 可能因为还没有 `server.secret` 而报错；此时先确认
`xiaozhi-esp32-server-web`、MySQL 和 Redis 正常。

浏览器打开：

```text
http://127.0.0.1:8002
```

注册第一个账号。第一个账号会成为超级管理员。登录后：

1. 打开“参数管理”。
2. 找到 `server.secret` 并复制参数值。
3. 编辑 `data/.config.yaml`：

```yaml
manager-api:
  url: http://xiaozhi-esp32-server-web:8002/xiaozhi
  secret: <刚才复制的 server.secret>
```

4. 重启 Server：

```bash
docker compose -f docker-compose.local.yml restart xiaozhi-esp32-server
docker logs -f -n 100 xiaozhi-esp32-server
```

日志中应出现 WebSocket 服务已经监听 `8000`。然后在智控台的“参数管理”中设置：

```text
server.websocket = ws://<Mac局域网IP>:8000/xiaozhi/v1/
server.ota       = http://<Mac局域网IP>:8002/xiaozhi/ota/
```

用浏览器访问 OTA 地址：

```text
http://<Mac局域网IP>:8002/xiaozhi/ota/
```

页面应显示 OTA 接口运行正常，并且存在可用的 WebSocket 集群。`ws://` 地址不是普通网页，
不能用“浏览器能打开”作为 WebSocket 验证方法。

### 6.5 模型配置与“全部本地”的含义

“Server 在本地运行”和“推理完全离线”是两件事：

| 模块 | 默认或可用方案 | 是否依赖互联网 |
| --- | --- | --- |
| ASR | 本地 `FunASR` / SenseVoiceSmall | 否 |
| LLM | 智谱、豆包等 API | 是 |
| LLM | `OllamaLLM` | 否，模型已在本地下载后可离线 |
| TTS | 默认 `EdgeTTS` 或其他云 API | 是 |
| TTS | FishSpeech、Index-TTS、PaddleSpeech 等本地服务 | 否，但需要额外部署和算力 |

建议分两步：

1. 先用一个可用的 LLM/TTS API 跑通设备、Server 和智控台链路。
2. 再切换 Ollama 和本地 TTS，最后通过断开外网验证是否真正离线。

当 Ollama 运行在 Mac、Server 运行在 Docker 中时，Ollama 地址应使用：

```text
http://host.docker.internal:11434
```

如果 Python Server 直接运行在 macOS 上，才使用 `http://localhost:11434`。

该社区项目的部署文档：

- [全模块部署](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment_all.md)
- [本地编译 Docker 镜像](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/docker-build.md)
- [使用预编译固件连接自定义 Server](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/firmware-setting.md)

## 7. 让两台硬件连接本地 Server

每台设备分别执行以下步骤：

1. 确认 Mac 和小智硬件位于同一个局域网，且硬件连接的是 2.4 GHz Wi-Fi。
2. 让设备进入重新配网模式。
3. 手机或电脑连接 `Xiaozhi-XXXXXX` 热点。
4. 打开 `http://192.168.4.1`。
5. 在“高级选项”中填写全模块 OTA 地址：

```text
http://<Mac局域网IP>:8002/xiaozhi/ota/
```

6. 保存 Wi-Fi 和 OTA 配置，等待设备重启。
7. 查看 `xiaozhi-esp32-server` 日志，确认设备连接到了：

```text
ws://<Mac局域网IP>:8000/xiaozhi/v1/
```

8. 如果设备播报六位验证码，打开本地智控台
   `http://<Mac局域网IP>:8002`，在本地智能体中添加设备。不要再到 `xiaozhi.me` 输入这个
   本地验证码。

如果设备仍然连接官方云，依次检查：

- 配网页面保存的 OTA 地址是否仍是 `https://api.tenclass.net/xiaozhi/ota/`；
- OTA 地址是否错误填写为 `127.0.0.1`；
- Mac 局域网 IP 是否变化；
- macOS 防火墙是否允许 Docker 访问 `8000`、`8002`、`8003`；
- `server.websocket` 是否填写了设备能访问的局域网地址；
- `server.secret` 是否与 `data/.config.yaml` 一致。

需要恢复官方云时，重新进入配网高级选项，将 OTA 地址改回：

```text
https://api.tenclass.net/xiaozhi/ota/
```

## 8. 通过小智控制 Desk Gateway

本地 Server 跑通语音对话后，通过 MCP 接入点增加受控的 Desk Gateway REST 工具层：

```mermaid
flowchart LR
    Device["小智 AI 硬件"] --> Server["本地 xiaozhi-esp32-server"]
    Server --> MCP["Desk MCP 工具 / 局域网桥接"]
    MCP --> REST["Desk Gateway REST + X-Desk-Key"]
    REST --> Core["desk_core 安全裁决"]
    Core --> Desk["升降桌"]
```

当前 Desk Gateway 已使用 TOF400C 完成最高安全高度和档位 1/4 的设备侧闭环。默认最高安全
高度为 `940 mm`，档位 1/4 默认为 `560 mm` 和 `870 mm`。“最高”和“站立档位”必须作为
两个不同动作处理；只有启用 ToF 的当前产品固件完成真桌验收后，才能开放持续上升到安全
上限的语音工具。

完整架构、MCP Endpoint 部署、桥接代码、工具语义、安全前提和验收步骤见
[通过小智 AI 控制升降桌](./11-xiaozhi-ai-desk-control.md)。

## 9. 验收清单

### 9.1 固件

- [ ] 两台设备的 16 MB 原始 Flash 已备份并保存 SHA-256。
- [ ] JC3636W518C 产品标签批次号已经核对。
- [ ] JC3636W518C 使用正确的 STD 或 PDM 固件，麦克风录音正常。
- [ ] Xmini-C3 只刷了 `xmini-c3`，没有刷 `xmini-c3-v3`。
- [ ] 两台设备的屏幕、按键、麦克风、喇叭和 2.4 GHz Wi-Fi 均已实测。

### 9.2 本地 Server

- [ ] ARM64 Server 和 Web 镜像在本机从源码构建完成。
- [ ] MySQL、Redis、manager-web/api、Server 容器健康。
- [ ] `server.secret` 已同步，且未提交到仓库。
- [ ] `server.websocket` 和 `server.ota` 使用固定局域网 IP。
- [ ] OTA 页面显示正常，Server 日志能看到两台设备连接。
- [ ] 两台设备已经在本地智控台完成绑定并能完整对话。
- [ ] 如果目标是完全离线，断开外网后 ASR、LLM、TTS 仍可工作。

### 9.3 升降桌控制

- [ ] `desk.raise_to_max` 只在 TOF400C 高度有效、最高安全高度已配置并完成真桌验收后启用。
- [ ] “升到最高”和“站立档位”分别映射到安全上限与档位 4，没有混用。
- [ ] 上升到最高、档位 1/4、传感器失效和右侧障碍均由 ESP32 本地闭环停止。
- [ ] `desk.stop` 不受对话状态或普通来源权限阻塞。
- [ ] 已完成真桌短行程、断网、Server 退出和紧急停止测试。

## 10. 参考资料

- [小智 AI 官方固件源码](https://github.com/78/xiaozhi-esp32)
- [小智 AI v2.4.2 Release](https://github.com/78/xiaozhi-esp32/releases/tag/v2.4.2)
- [JC3636W518C / 太极派板卡源码](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/taiji-pi-s3)
- [Xmini-C3 开源硬件](https://oshwhub.com/tenclass01/xmini_c3)
- [Xmini-C3 普通版固件源码](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/xmini/c3)
- [Xmini-C3 V3 固件源码](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/xmini/c3-v3)
- [小智 AI 开发文档](https://xiaozhi.me/home/zh/docs/)
- [社区本地后端源码](https://github.com/xinnan-tech/xiaozhi-esp32-server)
- [社区后端全模块部署文档](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment_all.md)
