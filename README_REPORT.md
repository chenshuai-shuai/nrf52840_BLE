# nrf52840_BLE + Android_nRF_BLE 代码分析报告

报告日期：2026-03-13

本文件基于对当前固件工程（`nrf52840_BLE`）与配套 Android App（`Android_nRF_BLE`）的通读分析整理，涵盖工程运行方式、结构与职责、关键模块作用、数据链路与性能/实时性评估，并给出后续开发建议。

---

## 1. 总览结论

- **固件端（nRF52840）**：基于 Zephyr 的分层架构（`app/hal/driver/platform/common`），已实现 BLE 音频上行（PDM Mic → BLE Notify）与 BLE 下行播放（BLE Write → I2S Speaker），并具备驱动统计、实时线程模板与音频抖动评估基础设施。
- **Android 端**：基于 Nordic nRF Blinky 架构改造，形成 **BLE 数据通道 + gRPC 语音与传感器上云** 的组合链路，BLE UUID 与固件一致，支持 MTU=247、2M PHY 与 High Priority 连接策略。
- **系统形态**：当前是一个端到端原型，强调实时性，但下行播放仍偏“缓冲后批播放”，需要进一步收敛为稳定的实时流式链路。

---

## 2. 工程运行方式

### 2.1 固件工程（`nrf52840_BLE`）

- **构建系统**：Zephyr + `CMakeLists.txt`
- **设备树**：`app.overlay`，同时存在 `nrf52840_ble.overlay` 作为硬件映射配置
- **关键配置**：`prj.conf` 启用 BLE、DMIC、I2S、NVS、GPIO、SPI/I2C 等
- **典型构建命令**
  - `west build -b nrf52840dk_nrf52840 -p auto .`
  - `west flash`
- **应用入口**：`src/app/Src/main.c`

### 2.2 Android App（`Android_nRF_BLE`）

- **Gradle 多模块**：`:app`, `:scanner`, `:blinky:ble`, `:blinky:spec`, `:blinky:ui`
- **运行方式**：Android Studio 打开 `Android_nRF_BLE`，运行 `app` 模块
- **权限**：
  - `RECORD_AUDIO`
  - `ACCESS_FINE_LOCATION` / `ACCESS_COARSE_LOCATION`

---

## 3. 固件工程架构与职责

### 3.1 分层结构

```
app/      业务逻辑（只调用 HAL）
hal/      统一接口，注册/分发
driver/   具体硬件实现（nRF52840）
platform/ 平台驱动绑定与能力表
common/   通用类型、统计、实时线程等
```

### 3.2 启动流程（`src/app/Src/main.c`）

1. `platform_init()`：注册驱动
2. `system_state_init()` + `app_db_init()` + `spi_bus_arbiter_init()`
3. `app_bus_start()` 事件总线
4. `app_lifecycle_start_all()`（PM → UPLINK → RTC → 传感器）
5. 可选 `boot_tone_play()` + `app_spk_diag`

### 3.3 关键模块作用

- **BLE 驱动**：`src/driver/Src/ble_nrf.c`
  - 自定义 128-bit UUID：
    - Service：`f0debc9a-...-0000`
    - RX（Write No Response）：`f1debc9a-...-0000`
    - TX（Notify）：`f2debc9a-...-0000`
  - 支持 MTU 交换、连接参数更新、PHY/数据长度更新日志
  - RX 队列 `K_MSGQ`（32 深度）

- **Mic 驱动**：`src/driver/Src/mic_nrf.c`
  - PDM 采集，16kHz / 16bit / mono
  - `mem_slab` 缓冲，单帧 10ms（160 samples）

- **Speaker 驱动**：`src/driver/Src/spk_nrf.c`
  - I2S TX，16kHz / 16bit / stereo
  - mono → stereo 拷贝，带功放 GPIO 使能

- **Uplink 服务**：`src/app/Src/app_uplink_service.c`
  - BLE 上行三优先级队列 + 下行队列
  - 传感器数据使用 `UPLINK_WIRE_HEADER`
  - MTU 自适应有效 payload

- **RTC / 音频主逻辑**：`src/app/Src/app_rtc.c`
  - Mic → BLE：采集、帧队列、分片、发送
  - Speaker：接收音频分片，重组，播放
  - 半双工控制：`APP_READY?` / `NRF_READY` / `APP_PLAY_START` / `APP_PLAY_END`

---

## 4. Android App 架构与职责

### 4.1 BLE 层（`BlinkyManager`）

- Nordic Android BLE Library
- BLE UUID 与固件一致
- 启用：
  - **MTU=247**（多次延时请求）
  - **2M PHY**（Android O+）
  - **High connection priority**

### 4.2 数据解析

- **音频包**：识别 `A5 5A` 头（固件上行）
- **Uplink 包**：识别 `C3 5C` 头（传感器/控制）
- **控制消息**：`BUF_FULL / BUF_LOW / PLAY_DONE / NRF_READY`

### 4.3 gRPC 模块

- `GrpcAudioClient` + `GrpcSensorClient`
- BLE → gRPC：nRF 麦克风数据上云
- gRPC → BLE：云端音频下行到 nRF 播放
- 会话控制：Push-to-Talk

### 4.4 UI（`BlinkyScreen`）

- 会话状态、gRPC 状态
- Audio Stats
- GPS / PPG / IMU 显示与保存状态

---

## 5. 关键数据链路

### 5.1 上行（nRF → Android）

```
PDM Mic → mic_nrf → app_rtc mic_capture → K_MSGQ →
mic_upload → ADPCM(8k) / PCM16 →
app_uplink_service → BLE Notify → Android
```

编码策略：`audio_encode_ima_adpcm_8k()`  
16k PCM → 8k 下采样 → IMA ADPCM  
10ms 帧约 48B（含编码头），显著降低 BLE 带宽压力。

### 5.2 下行（Android → nRF）

```
gRPC audio → BLE Write (RX characteristic) →
app_uplink_service downlink queue →
app_rtc spk_rx → 缓冲 → I2S 播放
```

半双工控制消息：
- Android 发 `APP_READY?`
- nRF 回 `NRF_READY`
- Android 发 `APP_PLAY_START`
- nRF 播放完成回 `PLAY_DONE`

---

## 6. 性能与实时性分析

### 6.1 线程与优先级（固件）

- `mic_capture`：prio=4  
- `mic_upload`：prio=5  
- `uplink`：prio=6  
- `spk_play`：prio=5  
- `app_bus`：prio=9  

优先级设置倾向于保证采集链路的实时性，BLE 发送处于稍低优先级，合理。

### 6.2 BLE 带宽与音频负载

- PCM16 16k：约 256 kbps
- ADPCM 8k：约 32 kbps + 头部开销
- 在 MTU=247、连接间隔 7.5~15ms 情况下，当前 ADPCM 上行负载可稳定承载。

### 6.3 主要实时性风险

- **下行播放偏批处理**：当前使用 `g_spk_test_buf` 收集后再播放，延迟较高。
- **BLE 回压时丢帧风险**：`AUDIO_PAYLOAD_MAX=200` + 队列深度有限，抖动时可能 drop。
- **上/下行状态切换**：依赖多处 `volatile` 变量与字符串控制消息，状态机尚未统一。

### 6.4 稳定性现状

已有 driver_stats 与 audio_jitter 统计入口，但未形成完整 **长期运行监测基线**。

---

## 7. 架构优点与短板

### 优点

- 固件端分层清晰，HAL/driver/平台绑定完善
- BLE / Mic / Spk 三个核心链路已闭环
- Android 端 BLE/gRPC/AI 会话路径清晰

### 短板

- 下行播放未达到严格流式实时性
- 协议规范未文档化，依赖硬编码字符串与 magic header
- 全链路状态机未统一，恢复策略不足

---

## 8. 后续开发建议

1. **固化 BLE 协议规范**
   - 控制消息、ACK、重传策略、payload 结构与版本号

2. **下行播放流式化**
   - 以 frame queue 为核心，不等待大块完整缓冲
   - 明确低水位/高水位策略

3. **建立性能基线**
   - 端到端延迟、BLE 丢包、断连恢复、长期运行统计

4. **统一状态机**
   - nRF `app_state` 与 Android 会话状态对齐
   - 明确 `READY / PLAY / UPLOAD / WAIT` 的状态转换

---

## 10. 近期问题定位与结论（2026-03-13）

### 10.1 现象
- Android 端保存的 10s WAV 文件大小固定且播放无声。
- nRF 端上行链路统计显示 `ready=0` 或 `MIC_UP peak/rms` 极低。

### 10.2 定位过程
1. 增加 nRF 上行帧 `MIC_UP` 峰值/RMS 打印，发现：
   - `ready=0` 时全部丢帧，并非静音采集。
   - `ready=1` 且采样通道错误时，峰值/RMS 很小。
2. 将单通道采样从 `PDM_CHAN_RIGHT` 切换到 `PDM_CHAN_LEFT` 后：
   - `peak/rms` 明显增大
   - Android 端 WAV 可正常听到语音

### 10.3 结论
- **根因：单通道采样通道选择错误。**
- 已固定为 **Left 单麦**：
  - `mic_nrf.c` 使用 `PDM_CHAN_LEFT`
  - 单通道采样，带宽与 RAM 负担最小

### 10.4 当前状态
- nRF 上行音频正常
- Android 自动保存 10s WAV 正常
- 可直接播放验证音频内容

## 9. 建议的下一步确认点

1. 是否将下行播放改为 **实时流式**（不再等待完整缓冲）？
2. 是否开始输出并固定 **BLE 协议文档**？
3. gRPC 侧是否需要增强 **重连/超时策略**？

确定后即可进入下一轮具体开发。
