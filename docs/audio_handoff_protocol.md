# Audio Handoff Protocol Specification

## 1. Purpose

This document is the shared source of truth for the audio-bus handoff feature between:

- `nRF52840` as the main controller
- `ESP32` as the Wi-Fi coprocessor

All later firmware work on both sides must follow this document.

This specification defines:

- global state model
- owner handoff rules
- UART protocol
- timing and timeout requirements
- fault handling
- bring-up and integration order

## 2. Hardware Constraints

The current PCB has all `0R` resistors populated, so the shared digital nets are physically tied between nRF and ESP32.

Shared functional nets:

- `MIC_CLK`
- `MIC_DOUT`
- `SD_MODE`
- `AMP_DIN`
- `AMP_BCLK`
- `AMP_LRCLK`

Critical board-specific constraint:

- the shared audio bus remains physically tied between nRF and ESP32 on this PCB
- ESP32 `BOOT/EN` control has been moved off the shared audio pins and now uses dedicated nRF GPIOs:
  - `P0.02 -> wifi_boot_ctrl`
  - `P0.05 -> wifi_en_ctrl`

Because of this:

1. normal BLE/Wi-Fi runtime audio switching must use UART coordination only
2. shared audio pins must not be reused for `BOOT/EN` control during normal audio handoff
3. `BOOT/EN` control is reserved for recovery / reset / firmware-download flow only and is driven through `P0.02/P0.05`

## 3. Design Principles

1. `nRF52840` is always the owner arbiter.
2. `ESP32` must never autonomously seize the shared audio bus.
3. At any time, only one side may actively drive the shared audio nets.
4. Any owner switch must pass through a fully released state called `SAFE_HANDOFF`.
5. Before ownership ACK is sent, the current owner must:
   - stop audio peripherals
   - stop related DMA/tasks
   - set all shared audio GPIOs to input/high-z
6. The requester may start its own audio path only after receiving the expected ACK.
7. On any protocol or runtime fault, both sides must fall back to `SAFE_HANDOFF`.

## 4. Global State Model

There are 4 logical states in the full system:

- `SAFE_HANDOFF`
- `NRF_AUDIO_OWNER`
- `ESP_AUDIO_OWNER`
- `NRF_ESP_BOOTCTRL`

Only the first 3 are used in the normal runtime path.

### 4.1 `SAFE_HANDOFF`

Definition:

- nRF shared audio GPIOs are high-z
- ESP32 shared audio GPIOs are high-z
- audio peripherals are stopped on both sides

Use cases:

- startup default safe state
- owner switching transition state
- fault fallback state

### 4.2 `NRF_AUDIO_OWNER`

Definition:

- nRF drives the shared audio bus
- ESP32 shared GPIOs are high-z
- BLE-side audio is handled by nRF

### 4.3 `ESP_AUDIO_OWNER`

Definition:

- ESP32 drives the shared audio bus
- nRF shared GPIOs are high-z
- Wi-Fi-side audio is handled by ESP32

### 4.4 `NRF_ESP_BOOTCTRL`

Definition:

- nRF temporarily drives dedicated ESP32 `BOOT/EN` control pins
  - `P0.02 -> wifi_boot_ctrl`
  - `P0.05 -> wifi_en_ctrl`
- shared audio path is not active

Use cases only:

- ESP32 recovery
- ESP32 reset
- ESP32 firmware download mode entry/exit

This state is not part of normal BLE/Wi-Fi handoff.

## 5. Ownership Policy

Owner target selection policy:

- `BLE connected` -> target owner = `NRF_AUDIO_OWNER`
- `BLE disconnected` -> target owner = `ESP_AUDIO_OWNER`

Important:

- BLE events determine the desired target owner
- actual owner change is completed only after UART command/ACK flow succeeds

## 6. UART Transport

### 6.1 Framing

ASCII line protocol terminated by `\r\n`.

Examples:

```text
ESP_PING\r\n
ESP_PONG\r\n
REQ_AUDIO_RELEASE\r\n
ACK_AUDIO_RELEASE\r\n
REQ_AUDIO_TAKE\r\n
ACK_AUDIO_TAKE\r\n
STATE?\r\n
STATE:SAFE_HANDOFF\r\n
FAULT:AUDIO_START_FAIL\r\n
```

### 6.2 Sender Policy

- nRF is the active command initiator
- ESP32 is the command responder
- ESP32 may send state/fault replies only as responses or reports after local failure

## 7. Command Set

### 7.1 Commands Sent By nRF

| Command | Purpose | Expected success reply | Failure reply |
|---|---|---|---|
| `ESP_PING` | verify ESP32 runtime availability | `ESP_PONG` | timeout or `FAULT:*` |
| `STATE?` | query ESP32 runtime state | `STATE:*` | timeout or `FAULT:*` |
| `REQ_AUDIO_RELEASE` | ask ESP32 to stop driving shared audio and go high-z | `ACK_AUDIO_RELEASE` | `FAULT:*` |
| `REQ_AUDIO_TAKE` | ask ESP32 to take audio ownership after nRF already entered safe | `ACK_AUDIO_TAKE` | `FAULT:*` |

### 7.2 Replies Sent By ESP32

| Reply | Meaning |
|---|---|
| `ESP_PONG` | ESP32 UART path and main loop are alive |
| `ACK_AUDIO_RELEASE` | ESP32 has stopped audio and set shared GPIOs high-z |
| `ACK_AUDIO_TAKE` | ESP32 has configured audio GPIOs and started its audio path |
| `STATE:SAFE_HANDOFF` | ESP32 audio GPIOs are high-z and audio is stopped |
| `STATE:ESP_AUDIO_OWNER` | ESP32 currently owns audio |
| `STATE:FAULT:<reason>` | ESP32 is in local fault state |
| `FAULT:<reason>` | command handling failed |

### 7.3 Standard Fault Reasons

These fault reasons should be used exactly as listed below unless a later revision changes them:

- `FAULT:NOT_READY`
- `FAULT:INVALID_STATE`
- `FAULT:UART_BUSY`
- `FAULT:AUDIO_RELEASE_FAIL`
- `FAULT:AUDIO_START_FAIL`
- `FAULT:GPIO_CONFIG_FAIL`
- `FAULT:DOWNLOAD_MODE_ACTIVE`
- `FAULT:TIMEOUT`

## 8. Timing Requirements

Recommended initial values:

- `PING_TIMEOUT_MS = 300`
- `CMD_TIMEOUT_MS = 500`
- `RETRY_COUNT = 3`
- `HANDOFF_GUARD_MS = 5~20`

Interpretation:

- `HANDOFF_GUARD_MS` is the delay between receiving the expected release ACK and enabling the new owner's audio path
- if later bench validation shows instability, this guard time may be increased

## 9. Required Owner Handoff Behavior

### 9.1 Release Semantics

Before a side is allowed to send release ACK, it must complete all of the following:

1. stop capture and playback tasks using the shared audio bus
2. stop audio peripheral blocks
3. stop or flush related DMA/buffers as needed by the local implementation
4. set all shared audio GPIOs to input/high-z
5. update local state to `SAFE_HANDOFF`

### 9.2 Take Semantics

Before a side is allowed to send take ACK, it must complete all of the following:

1. verify it is allowed to become owner
2. configure all required shared GPIOs to audio mode
3. wait local guard time if needed
4. start local audio path
5. update local state to owner state

## 10. Normal Runtime Sequences

### 10.1 BLE Connected -> nRF Takes Audio

Trigger:

- nRF detects BLE connection established

Sequence:

1. nRF sets target owner to `NRF_AUDIO_OWNER`
2. if current system owner is already nRF, stop
3. nRF sends `REQ_AUDIO_RELEASE`
4. ESP32 executes release semantics
5. ESP32 replies `ACK_AUDIO_RELEASE`
6. nRF enters `SAFE_HANDOFF` locally if not already there
7. nRF waits `HANDOFF_GUARD_MS`
8. nRF configures local audio GPIOs to audio mode
9. nRF starts local audio path
10. system enters `NRF_AUDIO_OWNER`

### 10.2 BLE Disconnected -> ESP32 Takes Audio

Trigger:

- nRF detects BLE disconnection

Sequence:

1. nRF sets target owner to `ESP_AUDIO_OWNER`
2. if current owner is already ESP32, stop
3. nRF stops local audio path
4. nRF sets local shared GPIOs high-z
5. nRF enters `SAFE_HANDOFF`
6. nRF sends `REQ_AUDIO_TAKE`
7. ESP32 executes take semantics
8. ESP32 replies `ACK_AUDIO_TAKE`
9. nRF records remote owner = ESP32
10. system enters `ESP_AUDIO_OWNER`

## 11. Startup Policy

After power-up, the default policy is:

1. nRF initializes first
2. nRF enters `SAFE_HANDOFF`
3. nRF initializes UART link to ESP32
4. nRF sends `ESP_PING`
5. if BLE is already connected, request `NRF_AUDIO_OWNER`
6. else if ESP32 is alive and in normal runtime, request `ESP_AUDIO_OWNER`
7. else remain in `SAFE_HANDOFF` and decide whether recovery is needed

This startup policy avoids immediate audio bus contention at boot.

## 12. Fault Handling

Any of the following must be treated as a fault:

- UART timeout
- unexpected reply
- invalid state transition
- local GPIO configuration failure
- local audio start failure
- remote not ready

Fault fallback rule:

1. local side stops audio
2. local side sets shared GPIOs high-z
3. local side enters `SAFE_HANDOFF`
4. nRF decides whether to:
   - retry command
   - query state again
   - enter `NRF_ESP_BOOTCTRL`

## 13. Recovery and Boot-Control Rule

Use `NRF_ESP_BOOTCTRL` only when:

- ESP32 does not answer `ESP_PING`
- ESP32 must be reset
- ESP32 must enter download mode
- ESP32 must exit download mode

Recovery sequence:

1. nRF enters `SAFE_HANDOFF`
2. nRF enters `NRF_ESP_BOOTCTRL`
3. nRF performs reset/boot/download control
4. nRF releases `BOOT/EN`
5. nRF returns local GPIOs to high-z
6. system returns to `SAFE_HANDOFF`
7. normal UART-based handoff resumes

## 14. State Transition Table

| Current State | Trigger | Condition | Action | Next State |
|---|---|---|---|---|
| `SAFE_HANDOFF` | BLE connected | ESP32 release path succeeds | request release, wait ACK, start nRF audio | `NRF_AUDIO_OWNER` |
| `SAFE_HANDOFF` | BLE disconnected | ESP32 ready | request take, wait ACK | `ESP_AUDIO_OWNER` |
| `NRF_AUDIO_OWNER` | BLE disconnected | normal path | stop nRF audio, high-z, request take | `ESP_AUDIO_OWNER` |
| `ESP_AUDIO_OWNER` | BLE connected | normal path | request release, start nRF audio | `NRF_AUDIO_OWNER` |
| any runtime state | UART/protocol fault | recoverable | stop local audio, high-z | `SAFE_HANDOFF` |
| `SAFE_HANDOFF` | ESP32 ping fail | recovery needed | boot control sequence | `NRF_ESP_BOOTCTRL` |
| `NRF_ESP_BOOTCTRL` | recovery complete | local release complete | release boot pins, high-z | `SAFE_HANDOFF` |

## 15. Integration Order

Recommended implementation order:

1. make both sides support `SAFE_HANDOFF`
2. implement `ESP_PING`
3. implement `REQ_AUDIO_RELEASE` + `ACK_AUDIO_RELEASE`
4. validate `BLE connected -> NRF_AUDIO_OWNER`
5. implement `REQ_AUDIO_TAKE` + `ACK_AUDIO_TAKE`
6. validate `BLE disconnected -> ESP_AUDIO_OWNER`
7. implement boot/recovery handling
8. validate timeout/fault fallback

## 16. Change Control

This file is the protocol contract.

If any command name, reply text, timeout, or state meaning changes, both:

- `docs/nrf_audio_handoff_design.md`
- `docs/esp32_audio_handoff_requirements.md`

must be updated in the same change.

## 17. System Acceptance Criteria

This section defines the minimum system-level acceptance criteria for this feature.

The feature is considered accepted only if all items below pass.

### 17.1 Functional Acceptance

1. After power-up, the system must be able to enter `SAFE_HANDOFF` without shared audio bus contention.
2. `ESP_PING` must succeed when ESP32 is in normal runtime.
3. `BLE connected` must result in `NRF_AUDIO_OWNER` after successful UART coordination.
4. `BLE disconnected` must result in `ESP_AUDIO_OWNER` after successful UART coordination.
5. On `REQ_AUDIO_RELEASE`, the current owner must stop audio and set all shared GPIOs high-z before ACK.
6. On `REQ_AUDIO_TAKE`, the requester must start audio only after the expected ACK.
7. If UART timeout occurs, the system must fall back to `SAFE_HANDOFF`.
8. If ESP32 is unavailable, nRF must be able to use `NRF_ESP_BOOTCTRL` for recovery and then return to `SAFE_HANDOFF`.

### 17.2 Electrical Safety Acceptance

1. During `NRF_AUDIO_OWNER`, ESP32 shared audio GPIOs must remain high-z.
2. During `ESP_AUDIO_OWNER`, nRF shared audio GPIOs must remain high-z.
3. During `SAFE_HANDOFF`, both sides' shared audio GPIOs must remain high-z.
4. During normal audio handoff, `BOOT/EN` control must not be toggled.
5. only `P0.02/P0.05` may be used as `BOOT/EN` in `NRF_ESP_BOOTCTRL`; `P1.02/P1.04` remain audio-only pins.

### 17.3 Robustness Acceptance

1. Repeated handoff cycling must not leave the system stuck in an invalid state.
2. Fault fallback must always drive the system back to `SAFE_HANDOFF`.
3. Invalid or unexpected UART responses must not cause either side to drive the shared audio bus incorrectly.
4. Local audio start failure on either side must not leave shared GPIOs actively driven in the wrong owner state.

## 18. Joint Validation Matrix

| Test ID | Scenario | Expected Result |
|---|---|---|
| `PROTO-01` | power-up default path | system reaches `SAFE_HANDOFF` without contention |
| `PROTO-02` | `ESP_PING` in normal runtime | `ESP_PONG` returned within timeout |
| `PROTO-03` | `STATE?` in `SAFE_HANDOFF` | correct `STATE:SAFE_HANDOFF` returned |
| `PROTO-04` | BLE connect handoff | final state `NRF_AUDIO_OWNER` |
| `PROTO-05` | BLE disconnect handoff | final state `ESP_AUDIO_OWNER` |
| `PROTO-06` | repeated connect/disconnect cycles | all cycles succeed without deadlock |
| `PROTO-07` | forced UART timeout | fallback to `SAFE_HANDOFF` |
| `PROTO-08` | ESP32 not responding | nRF recovery path enters `NRF_ESP_BOOTCTRL` and returns safe |
| `PROTO-09` | invalid UART reply | local side rejects transition and returns safe |
| `PROTO-10` | local audio start failure | fault raised, bus released, state returns safe |

## 19. Release Gate

This feature must not be declared complete unless:

1. protocol implementation on both sides matches this document
2. all tests in Section 18 pass
3. no known bus-contention issue remains
4. no normal handoff path depends on `BOOT/EN`
5. both side owners and fault logs are traceable in runtime logs
