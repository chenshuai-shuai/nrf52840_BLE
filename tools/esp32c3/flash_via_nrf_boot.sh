#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <esp-serial-port> [idf.py args...]"
  echo "example: $0 /dev/ttyUSB0 flash"
  echo "example: $0 /dev/ttyUSB0 flash monitor"
  exit 1
fi

PORT="$1"
shift || true

RUN_MONITOR=0
IDF_ARGS=()

if [[ $# -eq 0 ]]; then
  IDF_ARGS=(flash)
else
  for arg in "$@"; do
    if [[ "$arg" == "monitor" ]]; then
      RUN_MONITOR=1
      continue
    fi
    IDF_ARGS+=("$arg")
  done
fi

send_cmd() {
  local cmd="$1"
  python3 - "$PORT" "$cmd" <<'PY'
import sys
import time
import serial

port = sys.argv[1]
cmd = sys.argv[2]

with serial.Serial(port=port, baudrate=115200, timeout=0.2) as ser:
    ser.write((cmd + "\r\n").encode("ascii"))
    ser.flush()
    time.sleep(0.2)
PY
}

echo "[1/3] ask nRF to enter ESP download mode"
send_cmd "NRF:ESP_DL"

echo "[2/3] run idf.py -p $PORT ${IDF_ARGS[*]}"
idf.py -p "$PORT" "${IDF_ARGS[@]}"

echo "[3/3] ask nRF to boot ESP normally"
send_cmd "NRF:ESP_BOOT"

if [[ "$RUN_MONITOR" -eq 1 ]]; then
  echo "[4/4] run idf.py -p $PORT monitor"
  idf.py -p "$PORT" monitor
fi

echo "done"
