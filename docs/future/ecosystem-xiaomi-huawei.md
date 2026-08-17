# 生态接入调研：小米米家 & 华为智慧生活 / 鸿蒙智联

| 项 | 内容 |
|---|---|
| 文档 | DG-ARCH-ECO-001 |
| 日期 | 2026-08-06 |
| 状态 | 调研结论（会随官方政策变更；接入前再核实） |
| 关联 | [平台设计定稿](../architecture/platform-design.md)、[需求](../status/requirements.md) |

> 问题：Desk Gateway **后续接入小米、华为智能家居**时，是只要在现有 ESP32 上实现协议，还是必须加其他硬件？  
> 结论摘要见 §1；细节与推荐路径见后文。

---

## 1. 一句话结论

| 目标 | 现有 ESP32-S3 固件「只写协议」够不够？ | 是否通常要额外硬件？ |
|---|---|---|
| **米家 App 里出现「正品级」设备**（官方配网、场景、语音） | **不够** | **通常要**：小米 IoT 开放平台 + **MIIO 认证 Wi‑Fi/BLE 模组**（MCU↔模组串口）+ 产品定义与认证 |
| **华为智慧生活 / HarmonyOS Connect 正品级设备** | **不够** | **通常要**：鸿蒙智联合作伙伴流程 + **HiLink / HarmonyOS Connect 认证模组** + SDK/Profile/认证 |
| **用小米/华为手机控桌，但不强求出现在米家/智慧生活原生列表** | 可能够 | 走 **Matter**（同一颗 ESP32）或 **Home Assistant / 自有 Web** 中转；体验依赖 App 对 Matter 设备类型的支持 |
| **极客 DIY、不量产上架** | 社区逆向 / 私有云桥 | 不稳定、不合规风险高，**不做产品默认路径** |

**对 Desk Gateway 平台的含义**：  
生态层不要假设「在现有板上实现 miio/HiLink 私有协议就能进米家/智慧生活」。  
应规划 **两条轨**：

1. **开放互联轨（优先，与现硬件对齐）**：Matter（+ 已有 Web / 未来 HA MQTT）——多数情况 **不需要换主控**，在 ESP32-S3 上加软件栈（Flash/RAM 要评估）。  
2. **品牌原生轨（产品化/上架时）**：小米、华为各自 **认证模组 + 合作与认证**——属于 **硬件 SKU / 选配**，不是 Phase 1–2 必做。

---

## 2. 小米（米家 / MIoT）

### 2.1 官方路径（要进米家）

- 入口：[小米 IoT 开发者平台](https://iot.mi.com/)（智能硬件接入、自动化、渠道等）。  
- 常见硬件形态（开放文档与行业实践）：
  - **主控 MCU**（跑升降桌业务，即我们的 `desk_core`）  
  - **+ 小米 MIIO Wi‑Fi / BLE / 双模认证模组**（连云、配网、米家协议）  
  - MCU 与模组之间多为 **串口 MIoT/MIIO 规范**（功能 SIID/PIID 由产品定义）  
- 还需要：产品功能定义、指示灯/重置键等规范、**认证测试**（含 [AIoT 认证测试平台](https://autotest.iot.mi.com/) 一类流程）、量产与模组供应链。

→ **不是**「在自研 ESP32 固件里自己实现一套米家云协议就能合法上架」。  
自研 ESP32 **可以**充当 MCU，但仍需 **外挂小米模组**（或整机采用小米认可的联网方案），并走开放平台与认证。

### 2.2 非官方 / 旁路（不推荐作产品承诺）

- 社区对存量小米设备的 **本地/云端控制**（如 Home Assistant `hass-xiaomi-miot`）是「HA 控制米家设备」，**方向相反**，不能让 Desk Gateway 自动出现在米家里。  
- 破解/仿冒 miio 上架：合规与账号风险高，文档明确 **不做默认方案**。

### 2.3 Matter 与米家

- 小米生态在推进 Matter（网关/部分设备作 Bridge 或控制器能力随版本变化）。  
- 若用户用 **支持 Matter 的米家/小米中枢** 添加 **Matter 配件**，则 Desk Gateway 以 Matter 设备（如 Window Covering / 自定义集群，选型待定）加入——**可走现有 ESP32 + esp-matter 类栈**，无需小米模组。  
- **限制**：设备类型映射、语音与自动化能力、国内 App 版本支持度需实测；**不等于**「米家里的官方升降桌品类页」。

---

## 3. 华为（智慧生活 / HarmonyOS Connect，原 HiLink）

### 3.1 官方路径（要进智慧生活）

- 品牌已归并到 **HarmonyOS Connect（鸿蒙智联）**；开发者文档仍可见 HiLink SDK / 智能硬件说明。  
- 典型流程（[HiLink Codelab](https://developer.huawei.com/consumer/cn/codelab/HiLink/)、伙伴平台 [devicepartner.huawei.com](https://devicepartner.huawei.com/)）：
  1. 合作伙伴 / 开发者平台 **创建产品**、拿产品 ID / Profile  
  2. 在 **认证 Wi‑Fi（等）模组** 上集成 HiLink / HarmonyOS Connect SDK  
  3. UI+ 等工具做智慧生活控制页  
  4. **模组认证 / 整机认证**（选用已认证模组可缩短周期）  
- Linux 类主控也可集 HiLink SDK，但对 ESP32 消费级方案，主流仍是 **「业务 MCU + 华为认证联网模组」**。

→ 与小米类似：**正品进 App ≈ 合作 + 认证模组 + SDK，不是纯自研协议堆在现有 ESP32 上即可。**

### 3.2 Matter 与华为

- 华为手机 / 智慧生活对 Matter 的支持随系统与 App 演进；作为 **Matter 控制器添加配件** 时，同样可能 **不需 HiLink 模组**。  
- 能力与品类支持需真机验证；上架「华为生态认证」标识仍走鸿蒙智联认证，与「能被 Matter 发现」不是一回事。

---

## 4. 三条可选架构（给平台决策用）

```text
                    ┌─────────────────────────────┐
                    │     desk_core（已有）          │
                    └─────────────┬───────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
   A. Matter                   B. 品牌模组                C. HA / MQTT
   (ESP32 软件)                (额外硬件 SKU)              (软件中转)
        │                         │                         │
   米家/华为/苹果等             米家 或 智慧生活            用户自建桥
   凡支持 Matter 的 App         原生体验 + 认证            可玩性高、非大众
```

| 方案 | 额外硬件 | 软件 | 体验 | 成本/门槛 | 建议 |
|---|---|---|---|---|---|
| **A. Matter** | 一般不需要（ESP32-S3 评估资源） | `esp-matter` 等 + `desk_core` 适配 | 跨生态；品类/语音看控制器 | CSA/测试若量产上标则有费用；DIY 可先无标 | **平台默认生态轨** |
| **B1. 小米 MIIO 模组** | **要** 小米认证模组 + 串口 | 模组固件 + MCU 侧 MIoT 属性映射 | 米家原生 | 合作、认证、模组采购 | 产品化米家系 SKU |
| **B2. 华为认证模组** | **要** 鸿蒙智联认证模组 | HiLink SDK + Profile | 智慧生活原生 | 伙伴资质、认证 | 产品化华为系 SKU |
| **C. HA MQTT** | 不需要 | MQTT discovery / 自定义集成 | 极客友好 | 低 | Phase 3 已规划，可补强 |

**注意**：B1 与 B2 模组通常 **不能二合一共用一颗「既是小米又是华为」的官方模组**；双原生上架往往是 **两套联网模组方案或两个 SKU**，或只做 Matter 一条轨覆盖两边。

---

## 5. 对 Desk Gateway 文档与实现的约束

1. Phase 1–2 **不**实现米家/智慧生活原生接入；需求里保持 Backlog。  
2. 架构上预留 `connectivity/matter`（及未来 `connectivity/miot_uart` 对接外置模组）边界，**不**把 miio/HiLink 写死进 `desk_core`。  
3. 硬件路线图预留：  
   - **标准版**：仅 ESP32-S3（Web + 日后 Matter）  
   - **米家版 / 华为版（可选）**：板载对应认证模组排座或模组焊盘  
4. 升降桌在 Matter 中的设备类型（Window Covering vs 自定义）在立项 Matter 时再定；小米/华为 App 对 Cover 的自动化能力要实测。  
5. 接入前复核：iot.mi.com、华为伙伴平台当期政策、Matter 控制器支持列表（易变）。

---

## 6. 参考链接

| 主题 | URL |
|---|---|
| 小米 IoT 开发者平台 | https://iot.mi.com/ |
| 小米开放文档镜像（MIIO 模组规范等） | https://www.bookstack.cn/read/miio_open/README.md |
| 小米 AIoT 认证测试 | https://autotest.iot.mi.com/ |
| 华为智能硬件 / HiLink 概览 | https://developer.huawei.com/consumer/cn/doc/overview/Smart_Home |
| HiLink Codelab（模组二次开发） | https://developer.huawei.com/consumer/cn/codelab/HiLink/ |
| 鸿蒙智联伙伴 | https://devicepartner.huawei.com/ |
| Espressif Matter SDK | https://github.com/espressif/esp-matter |
| Matter 中文（认证概览） | https://matter.cn/dev |

---

## 7. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| 0.1 | 2026-08-06 | 初稿：米家/华为官方需认证模组；推荐 Matter 为现有 ESP32 主生态轨 |
