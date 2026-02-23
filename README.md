# nrf52840_BLE 框架说明（持续更新）

本文件是项目的唯一“进度与框架说明”入口。后续每一步的框架完善、接口变更、目录调整、关键设计决策，都需要在这里更新。

## 当前进度（截至 2026-02-23）

已完成的内容：
- 完成基础工程结构分层（`app/hal/driver/platform/common`）。
- 建立 HAL 抽象接口与驱动注册机制（`hal_audio` + 多器件 HAL）。
- 建立平台初始化入口（`platform_init`），用于注册具体硬件驱动。
- 完成平台注册机制（platform registry），支持多平台可选与切换。
- 完成 PDM 采集链路的骨架实现（使用 Zephyr `dmic` API）。
- 建立驱动模板（template driver），用于统一驱动开发模式。
- 引入 Kconfig 配置项，将 HAL 与 driver 变成可配置模块。
- 建立多器件 HAL/driver 框架骨架（PPG/IMU/Flash/GPS/BLE/Mic/Speaker/PM）。
- 平台能力表（platform_caps）已完成，可反映当前平台支持的子系统。
- 驱动性能统计模块（driver_stats）已落地，并接入模板驱动。
- 高实时线程模板（rt_thread）与音频抖动评估（audio_jitter）已加入。
- 构建系统更新为按 Kconfig 选择性编译，并加入对应 include 路径。
- BLE HAL/驱动已实现：自定义 GATT RX/TX、Notify、MTU 交换、配对/绑定与设置存储加载。
- Mic HAL/驱动已实现：PDM(16kHz/16bit/mono) 采集、mem_slab 缓冲、阻塞读与释放。
- Speaker HAL/驱动已实现：I2S TX、功放 GPIO 控制、静音预填充与稳定启动。
- App 层音频流程已接通：`app_rtc` + `app_state` 管理 UPLOAD/PLAY/BOOT 模式。
- BLE 上传链路已接通：Mic 采集 → 分片打包 → BLE Notify 上传。
- BLE 下行播放链路已接通：BLE RX → 缓冲 → I2S 播放（含简单 DSP/噪声门/限幅）。
- 启动提示音（boot tone）已加入，便于验证喇叭链路。
- 设备树 overlay 已补齐 PDM/I2S 引脚、功放控制别名、设置分区、禁用 DK LED/Button 与 QSPI。
- `prj.conf` 已启用 BLE/NVS/DMIC/I2S/GPIO 等运行所需基础配置。

未完成但计划完善的内容：
- 音频链路“可测可用”版本：稳定性、错误恢复、抖动统计闭环与长期运行验证。
- BLE 上/下行协议与 Android 端对接规范（payload/ACK/控制消息/重传策略）。
- Speaker 下行流的缓冲与欠载保护再调优（低水位/预缓存策略）。
- 其它器件驱动的真实实现（PPG/IMU/Flash/GPS/PM）。
- Kconfig/设备树自动启用的实际绑定（目前仅有 DT_HAS_* 占位符）。
- 高实时性能验证（端到端延迟、抖动、丢包统计）与测试基线。

---

## 工程结构与职责

```
src/
  app/
    Inc/        应用层对外头文件（业务逻辑）
    Src/        应用业务逻辑（只调用 HAL，不直接依赖驱动）
  hal/
    Inc/        HAL 抽象接口（稳定 API）
    Src/        HAL 分发器实现（ops 注册与转发）
  driver/
    Inc/        驱动层内部头文件（不暴露给 app）
    Src/        具体硬件驱动实现（nRF52840）
    Template/   驱动模板（复制后改名使用）
  platform/
    Inc/        平台初始化与适配接口
    Src/        平台驱动注册与绑定
  common/
    Inc/        通用类型、错误码等基础定义
    Src/        公共工具实现
```

### 分层原则
- **app 仅依赖 HAL**：应用层禁止直接引用驱动层或设备树。
- **driver 只暴露 HAL 接口**：驱动对外只通过 HAL 注册，不暴露内部实现。
- **platform 负责绑定**：平台层统一注册驱动，实现跨平台可替换。

---

## 关键文件说明

- `CMakeLists.txt`：按 Kconfig 选择性编译源码。
- `Kconfig`：框架级配置开关（HAL/driver/platform）。
- `src/platform/Inc/platform_registry.h`、`src/platform/Src/platform_registry.c`：平台注册与选择机制。
- `src/platform/Inc/platform_caps.h`、`src/platform/Src/platform_caps.c`：平台能力表。
- `src/platform/Src/platform_init.c`：平台初始化与驱动注册入口。
- `src/common/Inc/types.h`、`src/common/Inc/error.h`：基础类型与统一错误码。
- `src/common/Inc/driver_stats.h`、`src/common/Src/driver_stats.c`：驱动统计模块。
- `src/common/Inc/rt_thread.h`、`src/common/Src/rt_thread.c`：高实时线程模板。
- `src/common/Inc/audio_jitter.h`、`src/common/Src/audio_jitter.c`：音频抖动评估。
- `src/app/Src/main.c`：应用入口（平台初始化 + HAL 初始化）。
- `src/app/Src/app_rtc.c`：音频应用流程（Mic 上传 / Speaker 下行 / 控制消息）。
- `src/app/Src/app_state.c`：应用状态机（UPLOAD/PLAY/BOOT）。
- `src/driver/Src/ble_nrf.c`：BLE GATT 驱动（RX/TX、Notify、MTU、配对）。
- `src/driver/Src/mic_nrf.c`：PDM Mic 驱动（DMIC + mem_slab）。
- `src/driver/Src/spk_nrf.c`：I2S Speaker 驱动（功放 GPIO + I2S TX）。

---

## HAL 接口列表

- `src/hal/Inc/hal_audio.h`
- `src/hal/Inc/hal_ppg.h`
- `src/hal/Inc/hal_imu.h`
- `src/hal/Inc/hal_flash.h`
- `src/hal/Inc/hal_gps.h`
- `src/hal/Inc/hal_ble.h`
- `src/hal/Inc/hal_mic.h`
- `src/hal/Inc/hal_spk.h`
- `src/hal/Inc/hal_pm.h`

---

## 驱动骨架列表

- `src/driver/Src/audio_nrf.c`
- `src/driver/Src/ppg_nrf.c`
- `src/driver/Src/imu_nrf.c`
- `src/driver/Src/flash_nrf.c`
- `src/driver/Src/gps_nrf.c`
- `src/driver/Src/ble_nrf.c`
- `src/driver/Src/mic_nrf.c`
- `src/driver/Src/spk_nrf.c`
- `src/driver/Src/pm_nrf.c`

---

## Kconfig/设备树自动启用说明

已加入 DT 相关占位符号（`DT_HAS_*`），并在 Kconfig 中用于默认开启对应 HAL/driver。
当前这些 `DT_HAS_*` 仍为占位，尚未与具体设备树绑定。
后续需要：
- 在 DTS/overlay 中为具体器件节点添加 compatible，并在 Kconfig 中使用 `DT_HAS_..._ENABLED` 自动派生。

---

## 高实时线程模板与音频统计

新增基础设施：
- `rt_thread`：统一实时线程启动封装（命名 + 统一入口）
- `driver_stats`：驱动级统计（ok/err/drop/overrun）
- `audio_jitter`：音频抖动评估（min/max/样本计数）

当前统计接入点：
- `hal_audio.c` 在 `init/start/stop/read_block/release/read/write` 中记录统计与抖动样本。

---

## 当前构建与配置状态

- `prj.conf` 已启用日志、DMIC、I2S、GPIO、BLE、NVS settings 等基础配置。
- 默认启用 `HAL_BLE/MIC/SPK` 与对应 nRF 驱动，包含 BLE 测试/流模式配置。
- BLE 使用自定义 128-bit GATT（RX/Notify），默认 MTU=247（按配置）。
- NVS settings 分区启用，用于 BLE 绑定信息存储。
- 设备树 overlay 已启用 `pdm0`、`i2s0`，配置 PDM/I2S 引脚并禁用 DK LED/BUTTON/ QSPI。
- Overlay 中提供 `amp-sd-mode` 别名，便于功放 SD 控制。

---

## 下一步计划（待更新）

1. 完善音频链路稳定性：异常恢复、长时间运行验证、统计闭环。
2. 固化 BLE 协议与 Android 端对接规范（控制/数据/时序）。
3. 完成设备树自动启用的真实绑定（DT compatible + Kconfig）。
4. 逐个实现 PPG/IMU/Flash/GPS/PM 驱动。
5. 建立实时性能验证基线（延迟/抖动/丢包）。

---

> 注意：本文件需要持续更新，保持与代码实际状态一致。
