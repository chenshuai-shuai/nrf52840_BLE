# ESP32-C3 Linux 烧录操作说明

适用场景：

- `ESP32-C3-WROOM-02U-N4`
- PC 通过 USB-TTL 接共享串口
- nRF 负责控制 `BOOT/EN`
- ESP 烧录和日志都走 `/dev/ttyUSB0`

## 硬件连接

- `TTL TX -> P0.28 -> WIFI_RXD`
- `TTL RX -> P0.29 -> WIFI_TXD`
- `TTL GND -> 板子 GND`

## 前提条件

- nRF 已烧录当前 `wifi_boot_ctrl isolated` 固件
- nRF 上电后会先压住 ESP，不让默认启动
- Linux 环境已安装 `esptool.py`
- 已进入 ESP-IDF 开发环境
- 当前目录为 ESP 工程根目录

## 一、进入下载模式

先让 nRF 把 ESP 切到下载模式：

```bash
python3 - <<'PY'
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
ser.write(b'NRF:ESP_DL\r\n')
ser.flush()
time.sleep(0.3)
ser.close()
PY
```

nRF 侧 RTT 理想应看到：

```text
rx line: NRF:ESP_DL
cmd match: ESP_DL
```

## 二、可选：先验证是否成功进入 ROM 下载模式

建议首次联调时先执行：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --before no_reset --after no_reset chip_id
```

成功时会看到类似：

```text
Chip is ESP32-C3
MAC: xx:xx:xx:xx:xx:xx
```

如果这一步不通，不要继续烧录。

## 三、烧录固件

在 ESP 工程目录执行：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 -b 115200 --before no_reset --after no_reset write_flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/esp_wifi_collar.bin
```

说明：

- `--before no_reset`
- `--after no_reset`

这两个参数必须保留，因为当前 `BOOT/EN` 是由 nRF 控，不是 `esptool` 自动控。

## 四、切回正常启动

烧录完成后，让 nRF 把 ESP 切回正常启动模式：

```bash
python3 - <<'PY'
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
ser.write(b'NRF:ESP_BOOT\r\n')
ser.flush()
time.sleep(0.3)
ser.close()
PY
```

nRF 侧 RTT 理想应看到：

```text
rx line: NRF:ESP_BOOT
cmd match: ESP_BOOT
```

## 五、查看运行日志

切回正常启动后，查看 ESP 运行日志：

```bash
idf.py -p /dev/ttyUSB0 monitor
```

## 六、推荐标准操作流程

建议以后统一按下面顺序执行：

```bash
python3 - <<'PY'
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
ser.write(b'NRF:ESP_DL\r\n')
ser.flush()
time.sleep(0.3)
ser.close()
PY

esptool.py --chip esp32c3 --port /dev/ttyUSB0 --before no_reset --after no_reset chip_id

esptool.py --chip esp32c3 --port /dev/ttyUSB0 -b 115200 --before no_reset --after no_reset write_flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/esp_wifi_collar.bin

python3 - <<'PY'
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
ser.write(b'NRF:ESP_BOOT\r\n')
ser.flush()
time.sleep(0.3)
ser.close()
PY

idf.py -p /dev/ttyUSB0 monitor
```

## 七、注意事项

- 不要直接先跑 `idf.py flash`
- 先由 nRF 切到下载模式，再执行 `esptool.py`
- 当前推荐先使用 `esptool.py write_flash`，不要急着恢复成原生自动 reset 流程
- 如果下载过程正常但程序启动后崩溃，那是 ESP 应用自身问题，不是下载链路问题

## 八、当前已验证通过的部分

- nRF 接收 Linux 串口命令
- nRF 控制 ESP 进入下载模式
- `esptool.py chip_id`
- `write_flash`
- 烧录后切回正常启动
- `monitor` 查看运行日志
