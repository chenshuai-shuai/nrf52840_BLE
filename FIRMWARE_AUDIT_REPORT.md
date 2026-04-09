# nRF52840 固件稳定性与安全审计报告

## 审计范围
- 全部 app/ 层业务代码 (main, lifecycle, bus, uplink, ppg_hr, imu_test, gps, rtc, pm_service, data_store, state, db)
- 全部 driver/ 层驱动 (ble_nrf, imu_nrf, ppg_nrf, mic_nrf, spk_nrf, pm_nrf, temp_nrf, gps_nrf)
- 全部 hal/ 层硬件抽象 (hal_ble, hal_ppg, hal_imu, hal_mic, hal_spk, hal_pm, hal_temp)
- 全部 common/ 公共模块 (rt_thread, spi_bus_arbiter, audio_jitter, driver_stats)
- 全部 platform/ 层 (platform_init, platform_registry, boot_tone)
- prj.conf 配置

---

## 🔴 严重问题 (可导致系统卡死/功能失效)

### 1. `CONFIG_LOG_MODE_IMMEDIATE=y` — 最高优先级修复项
**文件**: `prj.conf:11`
**问题**: 立即模式日志会在 **中断上下文和高优先级线程** 中同步输出到 RTT，导致：
- 音频采集/播放线程被日志阻塞数百微秒甚至毫秒
- SPI/I2C 总线操作期间如果触发日志，可能导致 **时序违规**
- BLE 协议栈内部回调中产生日志时可能导致 **BLE 连接超时断开**
- 高负载下多个线程同时输出日志导致 **优先级反转**

**修复**:
```
CONFIG_LOG_MODE_IMMEDIATE=n
CONFIG_LOG_MODE_DEFERRED=y
CONFIG_LOG_PROCESS_THREAD=y
CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=1024
CONFIG_LOG_BUFFER_SIZE=2048
```

### 2. `CONFIG_STACK_CANARIES=n` — 栈溢出无保护
**文件**: `prj.conf:50`
**问题**: 栈金丝雀保护被关闭。多个线程栈空间偏紧：
- `app_bus` 仅 1536 字节，同时要做 mutex 加锁、遍历订阅者、调用回调
- `app_uplink` 仅 2560 字节，要做 BLE 发送、wire 打包、下行接收
- `imu_test` 仅 2048 字节，但使用了浮点数学 (sqrtf, fabsf) 和 AGM 算法
- `ppg_hr` 仅 2304 字节，包含复杂的 HRV 估算逻辑和浮点运算

**一旦栈溢出，会直接踩坏相邻内存，症状表现为随机卡死或数据错乱，极难复现和调试。**

**修复**:
```
CONFIG_STACK_CANARIES=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=30
CONFIG_THREAD_NAME=y
```
同时建议增大以下栈：
- `APP_BUS_STACK_SIZE`: 1536 → **2048**
- `IMU_TEST_STACK_SIZE`: 2048 → **3072** (含 AGM 浮点运算)
- `APP_PPG_HR_STACK_SIZE`: 2304 → **3072** (含浮点 HRV 计算)

### 3. PPG 线程中 `k_thread_abort()` 可导致资源泄漏和锁残留
**文件**: `src/driver/Src/ppg_nrf.c:207`
```c
static int ppg_nrf_stop(void)
{
    g_ppg.run = false;
    k_thread_abort(&g_gh_thread);  // ⚠️ 强制终止线程
    ...
}
```
**问题**: `k_thread_abort()` 会立即终止线程，不管线程当前处于什么状态：
- 如果线程正持有 `g_spi_bus_lock` mutex（调用 Gh3x2xDemoInterruptProcess() 时会走 SPI 通信），**mutex 将永远不会被释放**
- 后续 IMU 等其他 SPI 设备尝试 `spi_bus_lock()` 时会 **永久死锁**
- PPG recovery 机制中频繁调用 `hal_ppg_stop()` + `hal_ppg_start()`，此问题被反复触发

**修复**: 用协作退出代替强制终止：
```c
static int ppg_nrf_stop(void)
{
    if (!g_ppg.started) return HAL_OK;
    g_ppg.run = false;
    k_sem_give(&g_gh_irq_sem);  // 唤醒线程使其检查 run 标志
    // 等待线程自然退出，设超时保护
    for (int i = 0; i < 50; i++) {
        if (!g_ppg.started) break;
        k_msleep(20);
    }
    Gh3x2xDemoStopSampling(...);
    g_ppg.started = false;
    return HAL_OK;
}
```
同时在 `gh3026_irq_worker` 循环结尾设置 `g_ppg.started = false` 标志。

### 4. SPI 总线仲裁器 owner 检查存在竞态
**文件**: `src/common/Src/spi_bus_arbiter.c:48-58`
```c
int spi_bus_unlock(spi_bus_client_t client)
{
    if (g_spi_bus_owner != client) {
        LOG_WRN("spi bus unlock by non-owner");  // 仅警告，仍然会解锁！
    }
    g_spi_bus_owner = SPI_BUS_CLIENT_OTHER;
    return k_mutex_unlock(&g_spi_bus_lock);
}
```
**问题**: 
- owner 检查和赋值不在 mutex 保护内，存在 TOCTOU 竞态
- 非 owner 解锁时仅打警告但仍然执行 unlock，可能让另一个正在使用 SPI 的线程的操作被打断
- 与问题 #3 (k_thread_abort 导致锁残留) 组合时，会导致 **SPI 总线数据损坏**

### 5. `app_uplink_service` 中 `g_uplink_tx_lock` K_FOREVER 死锁风险
**文件**: `src/app/Src/app_uplink_service.c:422,458`
```c
k_mutex_lock(&g_uplink_tx_lock, K_FOREVER);
```
**问题**: 
- `app_uplink_publish()` 和 `app_uplink_publish_batch()` 均使用 `K_FOREVER` 等待互斥锁
- PPG (prio 7)、IMU (prio 8)、PM (prio 7) 等多个线程都会调用这些函数
- 如果低优先级线程 (如 GPS prio 8) 先获取锁，又被高优先级线程 (如 PPG prio 7) 抢占，此时另一个同优先级线程尝试获取锁，可能导致 **优先级反转延迟**
- Zephyr mutex 有内建优先级继承，但与 `K_FOREVER` 组合时，如果持锁线程被阻塞在 BLE 发送上，会导致所有调用者全部卡住

**修复**: 使用带超时的锁：
```c
int ret = k_mutex_lock(&g_uplink_tx_lock, K_MSEC(50));
if (ret != 0) {
    return HAL_EBUSY;
}
```

---

## 🟡 中等问题 (可导致功能异常或性能下降)

### 6. BLE 断开后未清空下行队列
**文件**: `src/driver/Src/ble_nrf.c:335-349`
**问题**: BLE 断开回调中只重置了 `g_conn` 和 `g_notify_enabled`，但 `g_ble_rx_q` 中可能残留旧数据。重新连接后，上层会读到上一次连接的残留命令，可能导致状态错乱。

**修复**: 在 `ble_disconnected()` 中加入：
```c
k_msgq_purge(&g_ble_rx_q);
```

### 7. `app_data_store` 环形缓冲区 ticket 可能读到过期数据
**文件**: `src/app/Src/app_data_store.c:135-163`
**问题**: `app_data_store_read()` 通过 `rec_id` 遍历整个 ring 查找数据。但 ring 是覆盖写入的，如果上行发送延迟较大，ticket 中的 `rec_id` 对应的 slot 可能已经被新数据覆盖。此时 `rec_id` 不匹配会返回 `-ENOENT`，上行数据丢失但不报错。

**影响**: 高负载时 PPG/IMU/态度数据可能静默丢失。

**改进**: 
- 增大 ring 容量（当前 PPG/IMU 各 12 slots 偏少）
- 或在 `put` 时检测是否有 ticket 未消费就被覆盖，增加统计计数

### 8. GPS 线程中 `hal_gps_init` 失败后线程直接退出，永不重试
**文件**: `src/app/Src/app_gps.c:139-149`
```c
int ret = hal_gps_init();
if (ret != HAL_OK) {
    LOG_ERR("gps app: hal_gps_init failed: %d", ret);
    return;  // ⚠️ 线程永久退出
}
```
**问题**: 如果 GPS 模块上电慢或 UART 初始化时序问题，线程退出后无法恢复。

**修复**: 加重试循环：
```c
int ret;
for (int i = 0; i < 10; i++) {
    ret = hal_gps_init();
    if (ret == HAL_OK) break;
    k_msleep(500);
}
if (ret != HAL_OK) {
    LOG_ERR("gps init failed after retries");
    return;
}
```

### 9. IMU FIFO ring buffer 无互斥保护
**文件**: `src/driver/Src/imu_nrf.c:162-191`
**问题**: `imu_fifo_ring_push()` 和 `imu_fifo_ring_pop()` 操作 `fifo_ridx/fifo_widx/fifo_count` 时没有加锁。虽然当前只有 ISR handler → `imu_nrf_read_common` 路径访问，但：
- `imu_fifo_ring_push` 在 `imu_fifo_fetch_samples` 中调用，此时已在线程上下文
- 如果 ISR 在 `imu_fifo_ring_pop` 操作中途触发并间接导致状态变化，可能导致索引不一致

**改进**: 对 push/pop 操作使用 `irq_lock()/irq_unlock()` 或确保只在同一上下文调用。

### 10. `ppg_nrf_on_spo2_result` 无并发保护
**文件**: `src/driver/Src/ppg_nrf.c:90-105`
**问题**: `g_spo2_latest` 结构体在中断/算法回调中直接赋值，没有原子操作或 mutex 保护。如果 `ppg_nrf_on_hr_result` 在读取 `g_spo2_latest` 的同时 `ppg_nrf_on_spo2_result` 正在写入，可能读到 **半更新的数据**（例如旧的 spo2 搭配新的 confidence）。

### 11. 下行播放期间暂停了 PPG/IMU/GPS，但恢复时序不保证
**文件**: `src/app/Src/app_rtc.c:426-467`
**问题**: `app_rtc_playback_critical_enter()` 暂停了 IMU、GPS、PPG 以给播放让出 SPI/CPU 资源。但：
- `app_ppg_hr_pause()` 调用 `hal_ppg_stop()` → `k_thread_abort()` (问题 #3)
- 恢复时 `app_ppg_hr_resume()` 调用 `hal_ppg_start()` 但不重建线程，可能失效
- 如果播放频繁（实时对话模式），PPG 会被反复 pause/resume，recovery 机制也会被反复触发

### 12. BLE TX buffer 数量偏少
**文件**: `prj.conf:93-94`
```
CONFIG_BT_BUF_ACL_TX_COUNT=3
CONFIG_BT_L2CAP_TX_BUF_COUNT=3
```
**问题**: 当音频上行 + 传感器数据 + 控制命令并发发送时，3 个 TX buffer 可能不够用，导致 `bt_gatt_notify()` 频繁返回 `-ENOMEM`。uplink 线程会重试 20 次 (每次 2ms sleep)，占用 40ms，期间下行数据也无法处理。

**修复**:
```
CONFIG_BT_BUF_ACL_TX_COUNT=6
CONFIG_BT_L2CAP_TX_BUF_COUNT=6
```

---

## 🟢 轻度问题 (影响较小但应改进)

### 13. `app_bus_publish` 丢弃最旧事件时无日志
**文件**: `src/app/Src/app_bus.c:117-136`
**问题**: 队列满时丢弃最旧事件再插入新事件，但无任何日志或统计。如果事件总线持续过载，系统状态更新会静默丢失。

### 14. `system_state` 使用同一 mutex 保护所有状态
**文件**: `src/app/Src/system_state.c`
**问题**: PM、PPG、GPS 三种状态共享 `g_state_lock`。PPG 线程更新频率高时会阻塞 PM 状态读取。建议拆分为三个独立 mutex 或使用原子操作。

### 15. `app_imu_test` 中浮点运算未检查 NaN/Inf
**文件**: `src/app/Src/app_imu_test.c:167-174`
**问题**: `sqrtf()` 和 `fabsf()` 输入如果因传感器异常为负数或异常大值，可能产生 NaN，传递给 AGM 算法可能导致不可预期的行为。

### 16. `pm_service` 中 `hal_pm_init` 无限重试
**文件**: `src/app/Src/pm_service.c:134-139`
```c
int ret = hal_pm_init();
while (ret != HAL_OK) {
    k_msleep(200);
    ret = hal_pm_init();
}
```
**问题**: 如果 PMIC 硬件损坏，此处会永久阻塞 PM 线程。虽然 PM 线程不影响其他线程运行，但 PM 服务永远不会变为 ready，lifecycle 依赖关系可能影响后续模块启动。建议加最大重试次数。

### 17. `mic_dsp_process` 使用 static 局部变量
**文件**: `src/app/Src/app_rtc.c:180-211`
**问题**: DC blocker 状态 `dc_x1` 和 `dc_y1` 是 static 变量。如果 mic 被 pause/resume（播放期间），这些状态不会被重置，恢复后可能产生一个短暂的直流偏移爆音。

### 18. Watchdog 未启用
**文件**: `prj.conf`
**问题**: 没有配置硬件看门狗。如果系统因为任何未预料到的原因卡死（例如 I2C 总线死锁、SPI 干扰），系统将永远不会自动恢复。

**修复**: 添加 Zephyr 看门狗支持：
```
CONFIG_WATCHDOG=y
CONFIG_WDT_NRFX=y
CONFIG_WDT_DISABLE_AT_BOOT=n
```
并在主线程中定期喂狗，或使用 Zephyr 的 task watchdog 监控各关键线程。

---

## 📋 修复优先级总结

| 优先级 | 问题编号 | 描述 | 影响 |
|--------|---------|------|------|
| **P0** | #1 | LOG_MODE_IMMEDIATE 阻塞所有线程 | 全局延迟，BLE 断连 |
| **P0** | #3 | k_thread_abort 导致 SPI mutex 死锁 | PPG recovery 后 SPI 总线永久死锁 |
| **P0** | #18 | 无硬件看门狗 | 任何卡死无法自恢复 |
| **P1** | #2 | 栈溢出无保护 | 随机崩溃/数据损坏 |
| **P1** | #5 | uplink mutex K_FOREVER | 多线程竞争时级联阻塞 |
| **P1** | #12 | BLE TX buffer 不足 | 高负载时频繁丢包 |
| **P2** | #4 | SPI arbiter 非owner可解锁 | SPI 数据损坏 |
| **P2** | #6 | BLE 断开不清队列 | 重连后状态错乱 |
| **P2** | #7 | data_store ticket 过期 | 高负载静默丢数据 |
| **P2** | #9 | IMU FIFO ring 无互斥 | 罕见数据损坏 |
| **P2** | #10 | SPO2 结果无并发保护 | 半更新数据 |
| **P2** | #11 | 播放暂停 PPG 恢复异常 | 实时对话后 PPG 失效 |
| **P3** | #8 | GPS init 无重试 | GPS 永久失效 |
| **P3** | #13-17 | 各类小问题 | 性能/健壮性 |

---

## ✅ 代码质量优秀的方面

1. **分层架构清晰**: HAL → Driver → App 三层解耦，每层职责明确
2. **app_bus 事件总线**: 发布/订阅模式避免了模块间直接耦合
3. **app_lifecycle 启动管理**: 依赖链和启动顺序管理完善
4. **uplink 优先级队列**: 高/中/低三级队列保证关键数据优先发送
5. **PPG 自恢复机制**: 检测到锁定/超时会自动 restart，容错性好
6. **BLE 连接参数协商**: 主动请求低延迟参数，MTU 协商到最大
7. **SPI 总线仲裁**: IMU 和 PPG 共享 SPI 总线有 mutex 保护
8. **PM 电源看门狗**: 定期重新配置 PMIC 防止寄存器漂移
