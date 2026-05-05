# nRF Audio Handoff Design Specification

## 1. Scope

This document defines the `nRF52840` implementation requirements for the shared-audio handoff feature.

`nRF52840` is the system master.

It is responsible for:

- determining target audio owner from BLE state
- driving the UART protocol toward ESP32
- switching local GPIO modes
- stopping and starting the local audio path
- entering ESP32 boot/recovery control when required

This document must be used as the nRF-side implementation checklist.

## 2. Hardware View

Shared nets from the nRF side:

- `P0.26 -> MIC_CLK`
- `P1.00 -> MIC_DOUT`
- `P1.11 -> SD_MODE`
- `P1.06 -> AMP_DIN`
- `P1.02 -> AMP_BCLK`
- `P1.04 -> AMP_LRCLK`

Board constraint:

- `P1.02` and `P1.04` are physically tied to ESP32 `BOOT/EN` related circuitry on this PCB.

Therefore:

1. in normal runtime handoff, treat `P1.02/P1.04` as audio lines only
2. use `P1.02/P1.04` as `BOOT/EN` only inside `NRF_ESP_BOOTCTRL`
3. never perform normal audio handoff by toggling `BOOT/EN`

## 3. nRF Responsibilities Summary

The nRF firmware must:

1. start from a safe GPIO state
2. choose target audio owner based on BLE state
3. coordinate normal handoff with ESP32 over UART
4. ensure local pins are high-z whenever nRF is not owner
5. recover ESP32 through `BOOT/EN` only when UART/runtime path is unavailable

## 4. nRF State Model

Use the following local state enum:

```c
typedef enum {
    AUDIO_STATE_SAFE_HANDOFF = 0,
    AUDIO_STATE_NRF_AUDIO_OWNER,
    AUDIO_STATE_ESP_AUDIO_OWNER,
    AUDIO_STATE_NRF_ESP_BOOTCTRL,
} audio_state_t;
```

### 4.1 `SAFE_HANDOFF`

nRF requirements:

- stop local audio tasks/peripherals
- set all shared audio GPIOs to input/high-z
- ensure no shared bus is actively driven

### 4.2 `NRF_AUDIO_OWNER`

nRF requirements:

- configure shared GPIOs to audio function
- start local audio path
- keep ESP32 out of shared bus ownership through protocol

### 4.3 `ESP_AUDIO_OWNER`

nRF requirements:

- stop local audio path
- keep all shared audio GPIOs high-z
- locally record that ESP32 is the active owner

### 4.4 `NRF_ESP_BOOTCTRL`

nRF requirements:

- audio already stopped before entering
- all non-boot shared GPIOs high-z
- temporarily drive:
  - `P1.02 -> wifi_boot_ctrl`
  - `P1.04 -> wifi_en_ctrl`

Exit rule:

- release both pins back to high-z
- return to `SAFE_HANDOFF`

## 5. GPIO Mode Table

### 5.1 nRF GPIOs In `SAFE_HANDOFF`

| Pin | Function | Required mode |
|---|---|---|
| `P0.26` | `MIC_CLK` | input/high-z |
| `P1.00` | `MIC_DOUT` | input/high-z |
| `P1.11` | `SD_MODE` | input/high-z |
| `P1.06` | `AMP_DIN` | input/high-z |
| `P1.02` | `AMP_BCLK` | input/high-z |
| `P1.04` | `AMP_LRCLK` | input/high-z |

### 5.2 nRF GPIOs In `NRF_AUDIO_OWNER`

| Pin | Required mode |
|---|---|
| `P0.26` | PDM/I2S audio function |
| `P1.00` | PDM/I2S audio function |
| `P1.11` | GPIO/peripheral as required by amplifier control |
| `P1.06` | I2S audio function |
| `P1.02` | I2S audio function |
| `P1.04` | I2S audio function |

### 5.3 nRF GPIOs In `ESP_AUDIO_OWNER`

All shared audio GPIOs must remain input/high-z.

### 5.4 nRF GPIOs In `NRF_ESP_BOOTCTRL`

| Pin | Required mode |
|---|---|
| `P1.02` | GPIO output for `wifi_boot_ctrl` |
| `P1.04` | GPIO output for `wifi_en_ctrl` |
| `P0.26/P1.00/P1.11/P1.06` | input/high-z |

## 6. Ownership Policy

- `BLE connected` -> target owner `NRF_AUDIO_OWNER`
- `BLE disconnected` -> target owner `ESP_AUDIO_OWNER`

Actual state transition completes only after the expected UART ACK.

## 7. Required nRF Modules

Recommended nRF module split:

### 7.1 `app_audio_route`

Responsibilities:

- maintain local handoff state
- switch nRF GPIO modes
- suspend/resume local audio path
- execute owner transitions

Recommended public APIs:

```c
int app_audio_route_init(void);
audio_state_t app_audio_route_get_state(void);

int app_audio_route_enter_safe(void);
int app_audio_route_enter_nrf_audio(void);
int app_audio_route_enter_esp_audio_shadow(void);
int app_audio_route_enter_bootctrl(void);

int app_audio_route_request_nrf_audio(void);
int app_audio_route_request_esp_audio(void);
```

### 7.2 `app_esp_link`

Responsibilities:

- UART line send/receive
- command/response matching
- timeout/retry handling

Recommended public APIs:

```c
int app_esp_link_init(void);
bool app_esp_link_is_ready(void);
int app_esp_link_ping(void);
int app_esp_link_get_state(char *buf, size_t len);
int app_esp_link_cmd(const char *tx, char *rx, size_t rx_len, uint32_t timeout_ms);
```

### 7.3 `app_wifi_boot_ctrl`

Responsibilities:

- reset ESP32
- enter normal boot
- enter/exit download mode

Rule:

- must not be used in the normal handoff path
- must be used only for recovery or firmware-update flow

### 7.4 `app_rtc`

Responsibilities:

- local audio runtime

Required hooks:

```c
int app_rtc_audio_suspend(void);
int app_rtc_audio_resume(void);
bool app_rtc_audio_is_suspended(void);
```

## 8. nRF Runtime Procedures

### 8.1 Procedure A: Startup

1. initialize platform and UART link
2. initialize audio-route state
3. enter `SAFE_HANDOFF`
4. ping ESP32
5. choose target owner:
   - if BLE already connected -> request nRF audio
   - else if ESP32 is alive -> request ESP audio
   - else remain safe and consider recovery path

### 8.2 Procedure B: BLE Connected

Target: `NRF_AUDIO_OWNER`

Sequence:

1. if already in `NRF_AUDIO_OWNER`, return success
2. send `REQ_AUDIO_RELEASE`
3. wait for `ACK_AUDIO_RELEASE`
4. call `app_audio_route_enter_safe()`
5. wait `HANDOFF_GUARD_MS`
6. call `app_audio_route_enter_nrf_audio()`

Failure handling:

1. keep `SAFE_HANDOFF`
2. retry up to `RETRY_COUNT`
3. if still failing, log fault and keep audio disabled

### 8.3 Procedure C: BLE Disconnected

Target: `ESP_AUDIO_OWNER`

Sequence:

1. if already in `ESP_AUDIO_OWNER`, return success
2. call `app_audio_route_enter_safe()`
3. send `REQ_AUDIO_TAKE`
4. wait for `ACK_AUDIO_TAKE`
5. call `app_audio_route_enter_esp_audio_shadow()`

Failure handling:

1. keep `SAFE_HANDOFF`
2. send `ESP_PING`
3. if ping fails, escalate to recovery

### 8.4 Procedure D: ESP32 Recovery

1. call `app_audio_route_enter_safe()`
2. call `app_audio_route_enter_bootctrl()`
3. perform one of:
   - `app_wifi_boot_ctrl_reset_only()`
   - `app_wifi_boot_ctrl_boot_normal()`
   - `app_wifi_boot_ctrl_enter_download()`
4. release `P1.02/P1.04`
5. return to `SAFE_HANDOFF`
6. ping ESP32
7. if ping succeeds and BLE is disconnected, retry Procedure C

## 9. nRF State Transition Table

| Current State | Trigger | Condition | Action | Next State |
|---|---|---|---|---|
| `SAFE_HANDOFF` | startup complete | BLE connected | release ESP audio, start nRF audio | `NRF_AUDIO_OWNER` |
| `SAFE_HANDOFF` | startup complete | BLE disconnected and ESP alive | request ESP take audio | `ESP_AUDIO_OWNER` |
| `NRF_AUDIO_OWNER` | BLE disconnected | normal path success | stop local audio, request ESP take | `ESP_AUDIO_OWNER` |
| `ESP_AUDIO_OWNER` | BLE connected | normal path success | request ESP release, start nRF audio | `NRF_AUDIO_OWNER` |
| any state | local/runtime fault | recoverable | stop local audio, high-z | `SAFE_HANDOFF` |
| `SAFE_HANDOFF` | ESP ping fail | recovery needed | enter bootctrl | `NRF_ESP_BOOTCTRL` |
| `NRF_ESP_BOOTCTRL` | recovery finished | release done | release boot pins, high-z | `SAFE_HANDOFF` |

## 10. Error Handling Requirements

Any of the following must force nRF to `SAFE_HANDOFF`:

- unexpected UART reply
- reply timeout
- local GPIO switch failure
- local audio start failure
- detected invalid state transition

Escalate to `NRF_ESP_BOOTCTRL` only when:

- `ESP_PING` fails
- ESP32 must be reset
- firmware-download flow requires it

## 11. Development Order

Implement in this order:

1. `SAFE_HANDOFF` local GPIO support
2. `app_esp_link` with `ESP_PING`
3. `REQ_AUDIO_RELEASE` path
4. `BLE connected -> NRF_AUDIO_OWNER`
5. `REQ_AUDIO_TAKE` path
6. `BLE disconnected -> ESP_AUDIO_OWNER`
7. boot/recovery path
8. timeout and fault fallback

## 12. Current Codebase Mapping

Current nRF project already has partial building blocks:

- `src/app/Src/app_wifi_boot_ctrl.c`
  current ESP32 UART/boot-control logic
- `src/app/Src/app_rtc.c`
  local audio runtime and suspend/resume hooks
- `src/app/Src/app_audio_route.c`
  initial local ownership draft

Recommended next project changes:

1. split UART command/response logic out of `app_wifi_boot_ctrl` into dedicated `app_esp_link`
2. keep `app_wifi_boot_ctrl` for boot/recovery only
3. rework `app_audio_route` to implement the exact 4-state model in this document
4. connect BLE connect/disconnect events to `request_nrf_audio()` / `request_esp_audio()`

## 13. nRF Acceptance Criteria

The nRF implementation is accepted only if all items below are satisfied.

### 13.1 State and GPIO Acceptance

1. `app_audio_route_enter_safe()` must stop local audio and set all shared audio GPIOs high-z.
2. `app_audio_route_enter_nrf_audio()` must configure shared GPIOs to audio mode only after safe transition is complete.
3. `app_audio_route_enter_esp_audio_shadow()` must leave all shared audio GPIOs high-z.
4. `app_audio_route_enter_bootctrl()` must drive only `P1.02/P1.04` for `BOOT/EN` control and leave other shared pins high-z.

### 13.2 UART/Protocol Acceptance

1. nRF must send `ESP_PING` and correctly parse `ESP_PONG`.
2. nRF must send `REQ_AUDIO_RELEASE` and wait for `ACK_AUDIO_RELEASE`.
3. nRF must send `REQ_AUDIO_TAKE` and wait for `ACK_AUDIO_TAKE`.
4. nRF must reject invalid replies and return to `SAFE_HANDOFF`.

### 13.3 Runtime Acceptance

1. On BLE connect, nRF must reach `NRF_AUDIO_OWNER` if ESP32 release succeeds.
2. On BLE disconnect, nRF must reach `ESP_AUDIO_OWNER` shadow state if ESP32 take succeeds.
3. On timeout or local error, nRF must return to `SAFE_HANDOFF`.
4. On failed `ESP_PING`, nRF must be able to enter the recovery path.

## 14. nRF Validation Checklist

| Test ID | Check Item | Pass Condition |
|---|---|---|
| `NRF-01` | safe entry | all shared audio pins high-z |
| `NRF-02` | nRF audio entry | local audio starts only after release ACK |
| `NRF-03` | ESP shadow entry | nRF shared pins remain high-z |
| `NRF-04` | bootctrl entry | only `P1.02/P1.04` are driven for boot control |
| `NRF-05` | BLE connect flow | local state becomes `NRF_AUDIO_OWNER` |
| `NRF-06` | BLE disconnect flow | local state becomes `ESP_AUDIO_OWNER` |
| `NRF-07` | invalid reply handling | state falls back to `SAFE_HANDOFF` |
| `NRF-08` | timeout handling | state falls back to `SAFE_HANDOFF` |
| `NRF-09` | recovery path | `NRF_ESP_BOOTCTRL` path executes and exits cleanly |

## 15. nRF Development Completion Gate

The nRF side is not complete unless:

1. all checklist items in Section 14 pass
2. all state transitions are logged
3. no normal handoff path uses `BOOT/EN`
4. all shared audio GPIOs are explicitly configured in every state transition
