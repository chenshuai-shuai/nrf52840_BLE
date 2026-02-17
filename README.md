# nrf52840_BLE 框架说明（持续更新）

本文件是项目的唯一“进度与框架说明”入口。后续每一步的框架完善、接口变更、目录调整、关键设计决策，都需要在这里更新。

## 当前进度（截至 2026-02-17）

已完成的内容：
- 完成基础工程结构分层（`app/hal/driver/platform/common`）。
- 建立 HAL 抽象接口与驱动注册机制（`hal_audio`）。
- 建立平台初始化入口（`platform_init`），用于注册具体硬件驱动。
- 完成平台注册机制（platform registry），支持多平台可选与切换。
- 完成 PDM 采集链路的骨架实现（使用 Zephyr `dmic` API）。
- 建立驱动模板（template driver），用于统一驱动开发模式。
- 引入 Kconfig 配置项，将 HAL 与 driver 变成可配置模块。
- 建立多器件 HAL/driver 框架骨架（PPG/IMU/Flash/GPS/BLE/Mic/Speaker/PM）。
- 构建系统更新为递归编译所有层级源码，并加入对应的 include 路径。

未完成但计划完善的内容：
- 驱动实现规范（ISR 约束、内存分配策略、性能统计）继续落地。
- 平台能力表（按平台汇总支持的子系统能力）。
- Kconfig/设备树层面的进一步框架化配置（按设备树自动启用）。
- 高实时线程模板、音频统计监控与抖动评估。

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

- `CMakeLists.txt`：递归编译所有层级源码并注册头文件路径。
- `Kconfig`：框架级配置开关（HAL/driver/platform）。
- `src/platform/Inc/platform_registry.h`、`src/platform/Src/platform_registry.c`：平台注册与选择机制。
- `src/platform/Src/platform_init.c`：平台初始化与驱动注册入口。
- `src/common/Inc/types.h`、`src/common/Inc/error.h`：基础类型与统一错误码。

### HAL 接口
- `src/hal/Inc/hal_audio.h`
- `src/hal/Inc/hal_ppg.h`
- `src/hal/Inc/hal_imu.h`
- `src/hal/Inc/hal_flash.h`
- `src/hal/Inc/hal_gps.h`
- `src/hal/Inc/hal_ble.h`
- `src/hal/Inc/hal_mic.h`
- `src/hal/Inc/hal_spk.h`
- `src/hal/Inc/hal_pm.h`

### 驱动骨架
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

## 当前 HAL Audio 设计说明（摘要）

- 使用 `hal_audio_ops_t` 作为驱动操作表。
- `hal_audio_register()` 负责绑定具体驱动实现。
- 提供零拷贝读取接口：
  - `hal_audio_read_block()` 获取 DMA/驱动提供的 buffer
  - `hal_audio_release()` 释放 buffer
- 目标是降低延迟、避免多余拷贝，保证高实时响应。

---

## HAL 规范（语义与时序约束）

本节是 HAL 层统一规范，所有 HAL 接口与驱动实现必须遵守。

### 1. 通用语义规范

- **返回值规范**  
  - 成功：返回 `0` 或正数（正数用于表示实际处理字节数/元素数）。  
  - 失败：返回负错误码（统一使用 `common/error.h` 中的宏，如 `HAL_EINVAL`）。  
  - `HAL_ENOTSUP` 表示功能未实现，但接口已存在。  
  - `HAL_EBUSY` 表示接口已注册或设备处于忙态。  

- **线程安全与重入**  
  - HAL API 默认**非重入**，除非接口说明明确支持并发。  
  - 若需要并发访问，驱动必须提供内部同步或在 HAL 层声明“外部串行”。  

- **阻塞与超时**  
  - `timeout_ms < 0` 表示无限等待（对应 `SYS_FOREVER_MS`）。  
  - `timeout_ms == 0` 表示非阻塞。  
  - `timeout_ms > 0` 表示最多等待指定时间。  

- **内存与资源管理**  
  - 不允许在 ISR 中做动态内存分配。  
  - 实时路径必须使用固定内存池或预分配内存。  
  - HAL 层禁止直接使用 `malloc/free`，应通过驱动私有内存池管理。  

### 2. 音频 HAL 语义与时序约束

#### `hal_audio_register(const hal_audio_ops_t *ops)`
- **语义**：注册底层驱动实现，绑定 ops 表。  
- **时序**：必须在 `hal_audio_init()` 之前调用，建议由 `platform_init()` 完成。  
- **约束**：`ops->init` 必须有效。  
- **重复注册**：返回 `HAL_EBUSY`。  

#### `hal_audio_init(void)`
- **语义**：初始化底层音频驱动与硬件资源。  
- **时序**：调用一次即可；重复调用必须是幂等或返回 `HAL_EBUSY`。  
- **实时性约束**：不得阻塞超过 10 ms。  

#### `hal_audio_start(hal_audio_dir_t dir, const hal_audio_cfg_t *cfg)`
- **语义**：启动采集或播放流。  
- **时序**：`init` 之后调用；重复 start 必须返回 `HAL_EBUSY`。  
- **实时性约束**：不得进行耗时内存分配，配置必须在可预期时间内完成。  

#### `hal_audio_stop(hal_audio_dir_t dir)`
- **语义**：停止对应方向的流，释放硬件 DMA/中断。  
- **时序**：可在 start 后调用；重复 stop 必须可安全处理。  

#### `hal_audio_read_block(void **buf, size_t *len, int timeout_ms)`
- **语义**：从驱动获取一块“零拷贝”音频数据。  
- **时序**：必须在 start 后调用。  
- **返回**：成功返回 0，同时 `*buf` 与 `*len` 有效。  
- **约束**：返回的 `buf` 必须由 `hal_audio_release()` 释放。  

#### `hal_audio_release(void *buf)`
- **语义**：释放 `read_block` 获得的 buffer。  
- **时序**：必须与 `read_block` 成对调用。  
- **约束**：必须为 O(1) 操作，不允许阻塞。  

#### `hal_audio_read(void *buf, size_t len, int timeout_ms)`
- **语义**：拷贝式读取。  
- **时序**：可选；驱动未实现时返回 `HAL_ENOTSUP`。  
- **约束**：若实现，必须保证不会破坏实时性。  

#### `hal_audio_write(...)`
- **语义**：播放输出接口（后续实现）。  
- **时序**：只在输出方向 `start` 后使用。  

### 3. 驱动实现约束（必须遵守）

- ISR 只能做：清标志、入队、唤醒线程。  
- 禁止在 ISR 中做复杂运算、日志、动态内存分配。  
- 驱动应使用固定内存池（`k_mem_slab`），不得在实时路径使用 `k_malloc`。  
- 每个 HAL 驱动必须提供 `init/start/stop`，其余接口可返回 `HAL_ENOTSUP`。  

---

## 平台注册机制（多平台可选）

### 1. 目标

- 统一平台初始化流程，支持多平台注册与选择。  
- App 层无需感知平台差异，只调用 `platform_init()`。  

### 2. 使用方式（当前实现）

- 平台在启动时调用 `platform_register(name, init_fn)` 注册自身。  
- 通过 `platform_set_active(name)` 选择当前平台。  
- `platform_init_selected()` 调用已选择平台的 `init_fn`。  

默认平台名称为 `nrf52840`，由 `platform_init()` 内部设置。  

### 3. 约束

- 注册必须在 `platform_set_active` 前完成。  
- 名称必须唯一，重复注册返回 `HAL_EBUSY`。  
- 未选择平台时调用 `platform_init_selected()` 返回 `HAL_ENODEV`。  
- `platform_init()` 具备幂等性，重复调用直接返回成功。  

---

## 驱动模板（标准样板）

模板文件位于：
- `src/driver/Template/driver_template.c`
- `src/driver/Template/driver_template.h`

模板约定的结构：
- static 状态结构体  
- 固定内存池（实时路径）  
- ops 注册到 HAL  
- `init/start/stop` 作为必备接口  

使用方式：
1. 复制模板到 `src/driver/Src` 与 `src/driver/Inc`  
2. 统一改名为具体驱动名称  
3. 实现 HAL ops 表并注册  

---

## Kconfig 与编译开关

新增 `Kconfig`，提供以下配置项：
- `CONFIG_HAL_AUDIO`  
- `CONFIG_DRIVER_AUDIO_NRF`  
- `CONFIG_PLATFORM_REGISTRY`  
- `CONFIG_PLATFORM_NRF52840`  
- `CONFIG_HAL_PPG` / `CONFIG_DRIVER_PPG_NRF`
- `CONFIG_HAL_IMU` / `CONFIG_DRIVER_IMU_NRF`
- `CONFIG_HAL_FLASH` / `CONFIG_DRIVER_FLASH_NRF`
- `CONFIG_HAL_GPS` / `CONFIG_DRIVER_GPS_NRF`
- `CONFIG_HAL_BLE` / `CONFIG_DRIVER_BLE_NRF`
- `CONFIG_HAL_MIC` / `CONFIG_DRIVER_MIC_NRF`
- `CONFIG_HAL_SPK` / `CONFIG_DRIVER_SPK_NRF`
- `CONFIG_HAL_PM` / `CONFIG_DRIVER_PM_NRF`

构建系统根据这些开关选择性编译 HAL/driver/platform 源码。  

默认全部为 `y`，如需裁剪可在 `prj.conf` 中覆盖。  
当前工程已在 `prj.conf` 显式设置这些开关。  

---

## 当前构建与配置状态

- `prj.conf` 已启用日志/音频/PDM/I2S/GPIO/UART 等基础配置。
- 设备树 overlay 已启用 `pdm0`、`uart1`、`i2s0` 并屏蔽 DK 默认 LED/BUTTON。

---

## 下一步计划（待更新）

1. 明确 HAL 的线程模型与阻塞策略规范。（已完成）
2. 建立驱动实现规范（ISR 约束、内存池策略、统计与监控）。（已完成约束部分）
3. 完善平台注册机制与多平台选择。（已完成）
4. 完善 PDM 采集驱动为可测可用模块。（进行中）
5. 引入可配置化的 Kconfig/设备树框架。（已完成 HAL/driver/platform 编译开关）
6. 驱动模板（标准样板）。（已完成）
7. 多器件 HAL/driver 框架骨架。（已完成）

---

> 注意：本文件需要持续更新，保持与代码实际状态一致。
