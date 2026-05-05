# ESP32 Audio Handoff Requirements Specification

## 1. Scope

This document defines what the ESP32 coprocessor firmware must implement to support the shared-audio handoff feature with the nRF52840 main controller.

This document is intended to be handed directly to the ESP32 firmware developer.

The ESP32 side is not the owner arbiter.

It must:

- respond to UART commands from nRF
- release shared audio ownership on request
- take shared audio ownership on request
- keep shared GPIOs high-z whenever it is not the owner
- report state and fault information over UART

## 2. System Assumptions

Normal runtime assumptions:

- both nRF and ESP32 are powered
- nRF is the master controller
- nRF decides when audio belongs to BLE or Wi-Fi
- ESP32 must not autonomously drive the shared audio bus unless nRF has requested it

## 3. Shared Nets Seen From ESP32

ESP32 side shared GPIOs:

- `IO3 -> MIC_CLK`
- `IO4 -> MIC_DOUT`
- `IO5 -> SD_MODE`
- `IO6 -> AMP_DIN`
- `IO7 -> AMP_BCLK`
- `IO10 -> AMP_LRCLK`

Mandatory rule:

When ESP32 is not the audio owner, all six GPIOs above must be configured as input/high-z.

Stopping the audio peripheral alone is not sufficient.

## 4. ESP32 Local State Model

ESP32 firmware must at minimum support the following local states:

- `SAFE_HANDOFF`
- `ESP_AUDIO_OWNER`
- `FAULT`

### 4.1 `SAFE_HANDOFF`

Requirements:

- stop audio capture/playback path
- stop any tasks that keep driving the shared bus
- set `IO3/IO4/IO5/IO6/IO7/IO10` to input/high-z

### 4.2 `ESP_AUDIO_OWNER`

Requirements:

- configure shared GPIOs to audio mode
- start local audio path
- keep driving the shared bus only while this state is active

### 4.3 `FAULT`

Requirements:

- stop audio
- set shared GPIOs high-z
- keep reporting state/fault correctly over UART if possible

## 5. UART Protocol To Implement

UART uses ASCII line protocol terminated by `\r\n`.

### 5.1 Commands ESP32 Must Accept

| Command | Meaning | Required response |
|---|---|---|
| `ESP_PING` | liveness check | `ESP_PONG` |
| `STATE?` | runtime state query | `STATE:*` |
| `REQ_AUDIO_RELEASE` | release shared audio ownership | `ACK_AUDIO_RELEASE` or `FAULT:*` |
| `REQ_AUDIO_TAKE` | take shared audio ownership | `ACK_AUDIO_TAKE` or `FAULT:*` |

### 5.2 Required Responses

| Response | Meaning |
|---|---|
| `ESP_PONG` | UART path and runtime are alive |
| `STATE:SAFE_HANDOFF` | ESP32 is not driving shared audio GPIOs |
| `STATE:ESP_AUDIO_OWNER` | ESP32 currently owns shared audio |
| `STATE:FAULT:<reason>` | ESP32 is in local fault state |
| `ACK_AUDIO_RELEASE` | release completed successfully |
| `ACK_AUDIO_TAKE` | take completed successfully |
| `FAULT:<reason>` | command processing failed |

### 5.3 Standard Fault Reasons

Please use these exact fault strings unless a later joint change updates the protocol:

- `FAULT:NOT_READY`
- `FAULT:INVALID_STATE`
- `FAULT:AUDIO_RELEASE_FAIL`
- `FAULT:AUDIO_START_FAIL`
- `FAULT:GPIO_CONFIG_FAIL`
- `FAULT:DOWNLOAD_MODE_ACTIVE`
- `FAULT:TIMEOUT`

## 6. Detailed Command Behavior

### 6.1 `ESP_PING`

ESP32 behavior:

1. do not modify any ownership state
2. reply:

```text
ESP_PONG
```

### 6.2 `STATE?`

ESP32 behavior:

1. inspect current local state
2. reply with one of:

```text
STATE:SAFE_HANDOFF
STATE:ESP_AUDIO_OWNER
STATE:FAULT:<reason>
```

### 6.3 `REQ_AUDIO_RELEASE`

ESP32 required behavior:

1. validate current runtime state
2. stop local audio capture/playback
3. set `IO3/IO4/IO5/IO6/IO7/IO10` to input/high-z
4. update local state to `SAFE_HANDOFF`
5. reply:

```text
ACK_AUDIO_RELEASE
```

If any step fails:

```text
FAULT:AUDIO_RELEASE_FAIL
```

### 6.4 `REQ_AUDIO_TAKE`

ESP32 required behavior:

1. verify firmware is in normal runtime mode
2. verify firmware is not in download mode
3. configure `IO3/IO4/IO5/IO6/IO7/IO10` to audio mode
4. optionally wait small local guard delay
5. start local audio pipeline
6. update local state to `ESP_AUDIO_OWNER`
7. reply:

```text
ACK_AUDIO_TAKE
```

If not allowed:

```text
FAULT:NOT_READY
```

If GPIO configuration fails:

```text
FAULT:GPIO_CONFIG_FAIL
```

If audio start fails:

```text
FAULT:AUDIO_START_FAIL
```

## 7. ESP32 GPIO Mode Table

### 7.1 GPIO Modes In `SAFE_HANDOFF`

| GPIO | Required mode |
|---|---|
| `IO3` | input/high-z |
| `IO4` | input/high-z |
| `IO5` | input/high-z |
| `IO6` | input/high-z |
| `IO7` | input/high-z |
| `IO10` | input/high-z |

### 7.2 GPIO Modes In `ESP_AUDIO_OWNER`

| GPIO | Required mode |
|---|---|
| `IO3` | audio function for `MIC_CLK` |
| `IO4` | audio function for `MIC_DOUT` |
| `IO5` | GPIO/peripheral as required for `SD_MODE` |
| `IO6` | audio function for `AMP_DIN` |
| `IO7` | audio function for `AMP_BCLK` |
| `IO10` | audio function for `AMP_LRCLK` |

### 7.3 Fault State Rule

On local fault, ESP32 must behave the same as `SAFE_HANDOFF` for shared GPIO drive:

- stop audio
- set all shared GPIOs high-z

## 8. Recommended ESP32 Internal Module Split

### 8.1 `esp_audio_owner`

Responsibilities:

- manage local audio owner state
- switch shared GPIO modes
- stop/start local audio path

Suggested APIs:

```c
int esp_audio_owner_init(void);
int esp_audio_owner_enter_safe(void);
int esp_audio_owner_enter_audio(void);
bool esp_audio_owner_is_active(void);
const char *esp_audio_owner_state_string(void);
```

### 8.2 `esp_uart_proto`

Responsibilities:

- receive ASCII line commands from nRF
- dispatch command handlers
- send replies

Suggested APIs:

```c
int esp_uart_proto_init(void);
void esp_uart_proto_task(void);
```

### 8.3 `esp_runtime_state`

Responsibilities:

- indicate whether firmware is in normal runtime
- indicate whether download mode is active

Suggested APIs:

```c
bool esp_runtime_is_normal(void);
bool esp_runtime_is_download_mode(void);
const char *esp_runtime_fault_reason(void);
```

## 9. ESP32 Pseudocode

### 9.1 UART Command Dispatcher

```c
on_uart_line(cmd)
{
    if (strcmp(cmd, "ESP_PING") == 0) {
        uart_send("ESP_PONG");
        return;
    }

    if (strcmp(cmd, "STATE?") == 0) {
        uart_send(current_state_string());
        return;
    }

    if (strcmp(cmd, "REQ_AUDIO_RELEASE") == 0) {
        if (esp_audio_owner_enter_safe() == OK) {
            uart_send("ACK_AUDIO_RELEASE");
        } else {
            uart_send("FAULT:AUDIO_RELEASE_FAIL");
        }
        return;
    }

    if (strcmp(cmd, "REQ_AUDIO_TAKE") == 0) {
        if (!esp_runtime_is_normal()) {
            uart_send("FAULT:NOT_READY");
            return;
        }
        if (esp_runtime_is_download_mode()) {
            uart_send("FAULT:DOWNLOAD_MODE_ACTIVE");
            return;
        }
        if (esp_audio_owner_enter_audio() == OK) {
            uart_send("ACK_AUDIO_TAKE");
        } else {
            uart_send("FAULT:AUDIO_START_FAIL");
        }
        return;
    }

    uart_send("FAULT:INVALID_STATE");
}
```

### 9.2 Safe Entry

```c
int esp_audio_owner_enter_safe(void)
{
    esp_audio_stop_all();
    esp_gpio_to_hiz_all();
    state = SAFE_HANDOFF;
    return OK;
}
```

### 9.3 Audio Entry

```c
int esp_audio_owner_enter_audio(void)
{
    esp_gpio_to_audio_mode();
    delay_ms(5);
    esp_audio_start_all();
    state = ESP_AUDIO_OWNER;
    return OK;
}
```

## 10. Required Behavior Summary

The ESP32 firmware must deliver all of the following:

1. respond to `ESP_PING`
2. respond to `STATE?`
3. release audio ownership cleanly on `REQ_AUDIO_RELEASE`
4. take audio ownership cleanly on `REQ_AUDIO_TAKE`
5. guarantee high-z on shared GPIOs when not owner
6. avoid driving shared GPIOs autonomously when not owner
7. report fault strings in agreed protocol format

## 11. Important Board Constraint

On this PCB, nRF-side `P1.02/P1.04` are physically reused for ESP32 boot-control circuitry.

Because of that:

- normal audio handoff must happen entirely through UART command coordination
- ESP32 must not assume nRF will toggle boot-control pins during routine audio switching
- boot/reset/download behavior is a separate maintenance path driven by nRF

## 12. Development Checklist For ESP32 Developer

Implementation is complete only when all items below are done:

- UART line receiver implemented
- `ESP_PING` reply implemented
- `STATE?` reply implemented
- `REQ_AUDIO_RELEASE` implemented
- `REQ_AUDIO_TAKE` implemented
- shared GPIO high-z safe mode implemented
- shared GPIO audio mode implemented
- fault response strings implemented
- local state tracking implemented

If the checklist above is complete, nRF-side integration can proceed without additional protocol redesign.

## 13. ESP32 Acceptance Criteria

The ESP32 implementation is accepted only if all items below are satisfied.

### 13.1 GPIO and Owner Acceptance

1. In `SAFE_HANDOFF`, `IO3/IO4/IO5/IO6/IO7/IO10` must all be input/high-z.
2. In `ESP_AUDIO_OWNER`, the shared GPIOs must be configured for audio function before the audio path starts.
3. On any local fault, ESP32 must stop audio and return shared GPIOs to high-z.

### 13.2 UART Acceptance

1. ESP32 must respond to `ESP_PING` with `ESP_PONG`.
2. ESP32 must respond to `STATE?` with the correct local state string.
3. ESP32 must respond to `REQ_AUDIO_RELEASE` only after release is complete.
4. ESP32 must respond to `REQ_AUDIO_TAKE` only after audio owner entry is complete.
5. ESP32 must return agreed `FAULT:*` strings on failure.

### 13.3 Runtime Acceptance

1. ESP32 must never actively drive the shared audio bus unless it is in `ESP_AUDIO_OWNER`.
2. ESP32 must not autonomously take audio ownership.
3. ESP32 must refuse `REQ_AUDIO_TAKE` when not in normal runtime or when download mode is active.

## 14. ESP32 Validation Checklist

| Test ID | Check Item | Pass Condition |
|---|---|---|
| `ESP-01` | ping response | `ESP_PONG` returned within timeout |
| `ESP-02` | state query in safe | `STATE:SAFE_HANDOFF` returned |
| `ESP-03` | state query in owner | `STATE:ESP_AUDIO_OWNER` returned |
| `ESP-04` | release command | all shared GPIOs high-z before `ACK_AUDIO_RELEASE` |
| `ESP-05` | take command | audio starts before `ACK_AUDIO_TAKE` is sent |
| `ESP-06` | not-ready protection | `REQ_AUDIO_TAKE` returns `FAULT:NOT_READY` when not allowed |
| `ESP-07` | download-mode protection | returns `FAULT:DOWNLOAD_MODE_ACTIVE` in download mode |
| `ESP-08` | local fault handling | shared GPIOs return high-z and `FAULT:*` is reported |

## 15. ESP32 Development Completion Gate

The ESP32 side is not complete unless:

1. all checklist items in Section 14 pass
2. shared GPIO high-z behavior is implemented explicitly, not implicitly assumed
3. ESP32 never drives the shared bus outside `ESP_AUDIO_OWNER`
4. all protocol replies match the command contract exactly
