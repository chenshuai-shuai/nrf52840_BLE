# nRF52840 到 Android App 的 BLE 数据传输协议说明

本文档面向 Android App 开发同事，说明 nRF 通过 BLE 向 App 上传实时音频数据和传感器数据的协议、数据格式、发送节拍与实现注意事项。

目标是让 Android 同事拿到本文档后，能够直接完成 BLE 对接、数据解包与业务适配。

---

## 1. BLE 服务与特征

### 1.1 Service UUID

- `f0debc9a-7856-3412-7856-341234120000`

### 1.2 Characteristic UUID

- nRF -> App，Notify  
  `f2debc9a-7856-3412-7856-341234120000`

- App -> nRF，Write Without Response  
  `f1debc9a-7856-3412-7856-341234120000`

### 1.3 当前工程中的方向约定

- `BUTTON characteristic`
  - 实际用于 **nRF -> App** 上行通知

- `LED characteristic`
  - 实际用于 **App -> nRF** 下行写入

---

## 2. 上行数据分类

nRF -> App 上行数据分为三类：

1. 实时音频数据
2. 带统一包头的传感器数据
3. ASCII 控制文本

Android 端收到每个 Notify 包后，建议按以下顺序判断：

1. 如果前两字节是 `0xA5 0x5A`
   - 按音频分片包处理
2. 否则如果前两字节是 `0xC3 0x5C`
   - 按统一传感器包头处理
3. 否则
   - 按 ASCII 控制文本处理

---

## 3. 实时音频上行协议

### 3.1 采样参数

- 采样源：PDM 麦克风
- 采样率：`16000 Hz`
- 通道数：`1`
- 位宽：`16 bit`

### 3.2 发送节拍

每一帧原始 PCM：

- `160` 个采样点
- 单声道 16 bit
- 原始大小 `320 bytes`

因此：

- 每帧时长：`10 ms`
- 上行帧率：`100 帧/秒`

### 3.3 编码格式

nRF 默认优先使用：

- `IMA ADPCM`

回退格式：

- `PCM16_LE`

当前协议中：

- `codec = 2` 表示 `IMA ADPCM`
- `codec = 1` 表示 `PCM16_LE`

### 3.4 音频分片头格式

音频上行不走统一 14 字节传感器包头，而是使用专用 8 字节分片头：

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | magic0 | `0xA5` |
| 1 | 1 | magic1 | `0x5A` |
| 2 | 2 | seq | 帧序号，小端 |
| 4 | 1 | frag_idx | 当前分片序号 |
| 5 | 1 | frag_cnt | 总分片数 |
| 6 | 2 | payload_len | 当前分片负载长度，小端 |

其后紧跟当前分片 payload。

### 3.5 音频帧 payload 结构

#### IMA ADPCM payload

前 8 字节为 codec header：

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | codec | `2` |
| 1 | 1 | sample_rate_khz | 当前为 `16` |
| 2 | 1 | channels | 当前为 `1` |
| 3 | 1 | bits_per_sample | 当前为 `4` |
| 4 | 2 | predictor | 小端 |
| 6 | 1 | step_index | IMA ADPCM step index |
| 7 | 1 | pcm_samples | 当前为 `160` |

后面是 ADPCM 压缩数据。

#### PCM16_LE payload

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | codec | `1` |
| 1.. | N | pcm | 原始 16-bit little-endian PCM |

### 3.6 Android 端处理建议

1. 收到音频分片后按 `seq` 重组
2. 收齐全部分片后，拼出完整音频帧 payload
3. 根据 `codec` 判断是 ADPCM 还是 PCM16
4. 如果是 ADPCM，则按 codec header 解码
5. 解码后得到 16 kHz 单声道 PCM，再按业务需要处理

---

## 4. 统一传感器 uplink 包头

传感器数据使用固定 14 字节统一包头：

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | magic0 | `0xC3` |
| 1 | 1 | magic1 | `0x5C` |
| 2 | 1 | ver | 当前固定 `1` |
| 3 | 1 | part | 数据类型 |
| 4 | 2 | payload_len | payload 长度，小端 |
| 6 | 2 | rec_id | 记录 ID，小端 |
| 8 | 4 | ts_ms | nRF 时间戳，小端，毫秒 |
| 12 | 2 | reserved | 当前固定 `0` |

其后从偏移 `14` 开始是 payload。

---

## 5. part 枚举

| part 值 | 名称 | 说明 |
|---:|---|---|
| 0 | `APP_DATA_PART_CTRL` | 控制文本 |
| 1 | `APP_DATA_PART_AUDIO_UP` | 麦克风音频 |
| 2 | `APP_DATA_PART_AUDIO_DOWN` | App 到 nRF 的播放音频 |
| 3 | `APP_DATA_PART_PPG` | PPG/心率/血氧/HRV |
| 4 | `APP_DATA_PART_IMU` | IMU 原始数据 |
| 5 | `APP_DATA_PART_PM` | PM 数据 |
| 6 | `APP_DATA_PART_ATTITUDE` | 姿态融合数据 |
| 7 | `APP_DATA_PART_TEMP` | 温度数据 |

---

## 6. PPG / 心率 / 血氧 / HRV 数据

### 6.1 part

- `APP_DATA_PART_PPG = 3`

### 6.2 payload 结构

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | ver | 当前为 `3` |
| 1 | 1 | type | 当前为 `1` |
| 2 | 2 | hr_bpm | 心率 |
| 4 | 2 | conf | 心率置信度 |
| 6 | 2 | snr | 信噪比 |
| 8 | 4 | frame_id | 算法帧号 |
| 12 | 4 | ts_ms | 时间戳 |
| 16 | 2 | hrv | HRV |
| 18 | 2 | hrv_conf | HRV 置信度 |
| 20 | 2 | spo2 | 血氧 |
| 22 | 2 | spo2_conf | 血氧置信度 |
| 24 | 2 | spo2_valid | 血氧有效等级 |
| 26 | 2 | spo2_invalid | 血氧无效标志 |
| 28 | 2 | spo2_hb | 血氧心跳值 |

总长度：

- `30 bytes`

### 6.3 上报特点

PPG 上报的是算法结果流，不是固定高频原始波形流。  
Android 侧按事件驱动接收即可。

---

## 7. IMU 原始数据

### 7.1 part

- `APP_DATA_PART_IMU = 4`

### 7.2 payload 结构

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | ver | `1` |
| 1 | 1 | type | `3` |
| 2 | 2 | seq | 序号 |
| 4 | 2 | ax | 加速度 X，LSB |
| 6 | 2 | ay | 加速度 Y，LSB |
| 8 | 2 | az | 加速度 Z，LSB |
| 10 | 2 | gx | 陀螺仪 X，LSB |
| 12 | 2 | gy | 陀螺仪 Y，LSB |
| 14 | 2 | gz | 陀螺仪 Z，LSB |
| 16 | 2 | temp | 温度原始值 |
| 18 | 4 | ts_ms | 时间戳 |

总长度：

- `22 bytes`

### 7.3 上报速率

- IMU 原始采样率：`100 Hz`
- uplink 频率：约 `25 Hz`

---

## 8. 姿态融合数据

### 8.1 part

- `APP_DATA_PART_ATTITUDE = 6`

### 8.2 payload 结构

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | ver | `1` |
| 1 | 1 | type | `1` |
| 2 | 2 | seq | 序号 |
| 4 | 4 | qw_q30 | 四元数 w |
| 8 | 4 | qx_q30 | 四元数 x |
| 12 | 4 | qy_q30 | 四元数 y |
| 16 | 4 | qz_q30 | 四元数 z |
| 20 | 4 | gx_q16 | 重力向量 x |
| 24 | 4 | gy_q16 | 重力向量 y |
| 28 | 4 | gz_q16 | 重力向量 z |
| 32 | 4 | lax_q16 | 线性加速度 x |
| 36 | 4 | lay_q16 | 线性加速度 y |
| 40 | 4 | laz_q16 | 线性加速度 z |
| 44 | 1 | acc_accuracy | 加速度精度 |
| 45 | 1 | gyr_accuracy | 陀螺精度 |
| 46 | 1 | mag_accuracy | 磁力计精度 |
| 47 | 1 | flags | 标志位 |

总长度：

- `48 bytes`

### 8.3 flags 定义

- bit0：`VALID`
- bit1：`MOVING`
- bit2：`BIAS_READY`

### 8.4 上报速率

- 姿态 uplink 默认约 `50 Hz`

---

## 9. 温度数据

### 9.1 part

- `APP_DATA_PART_TEMP = 7`

### 9.2 payload 结构

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | ver | `1` |
| 1 | 1 | type | `1` |
| 2 | 2 | seq | 序号 |
| 4 | 2 | raw | 原始值 |
| 6 | 4 | micro_c | 微摄氏度 |
| 10 | 4 | ts_ms | 时间戳 |

总长度：

- `14 bytes`

### 9.3 上报速率

- 约 `1 Hz`

---

## 10. 控制文本上行

Android 端至少需要识别这些上行文本：

- `NRF_READY`
- `BUF_FULL`
- `BUF_LOW`
- `PLAY_DONE`

---

## 11. App -> nRF 下行控制命令

当前 Android 侧常用下行控制命令包括：

- `APP_READY?`
- `APP_PLAY_START`
- `APP_PLAY_END`
- `APP_SPK_VOL:<n>`

维护命令包括：

- `NRF:ESP_DL`
- `NRF:ESP_BOOT`

---

## 12. Android 端第一版建议优先支持

建议第一版先完成：

1. BLE 服务与特征接入
2. 音频分片重组
3. IMA ADPCM 解码
4. PPG 数据解包
5. IMU / 姿态 / 温度数据解包
6. 控制文本识别
7. 下行控制命令发送

这样后续无论具体某版固件打开哪些上传项，App 都不需要再改协议层。

