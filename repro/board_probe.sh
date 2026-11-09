#!/usr/bin/env bash
# One-shot identification probe for an unknown ESP32-S3 board.
# Everything it prints is evidence for a board profile in
# esp32-skills/references/esp32-s3/boards/ — capture it, don't paraphrase it.
#
# Usage:  ./board_probe.sh [port]          # default: first Espressif-ish /dev/serial/by-id node
#         ./board_probe.sh /dev/ttyUSB0
#
# If "No serial data received" => the app on the board owns the USB port.
# Hold BOOT, tap RST, release BOOT (download mode), re-run.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
log="$here/probe-$(date +%Y%m%d-%H%M%S).txt"

PORT="${1:-$(ls -1 /dev/serial/by-id/* 2>/dev/null | head -1)}"
[ -n "${PORT:-}" ] || { echo "no serial port found; plug the board in (or pass the port)"; exit 2; }
PORT="$(readlink -f "$PORT")"
exec > >(tee "$log") 2>&1

echo "=== host: $(date -Is) ==================== kernel tail (hotplug) ="
journalctl -k -n 40 --no-pager 2>/dev/null | grep -iE "usb|tty|sd" | tail -25

echo
echo "=== usb identity ==============================================="
for vid in 303a 1a86 10c4 0403 067b; do lsusb -d "$vid:" -v 2>/dev/null | grep -E "idVendor|idProduct|iManufacturer|iProduct|iSerial|bDeviceClass|bNumInterfaces|bInterfaceClass|MaxPower"; done
echo "by-id:  $(readlink -f "/dev/serial/by-id/$(basename $(dirname $PORT))" 2>/dev/null)"
ls -l /dev/serial/by-id/ 2>/dev/null

echo
echo "=== esptool (download mode required) ============================"
esptool --chip auto --port "$PORT" --before default-reset --after no-reset chip-id 2>&1 | grep -vE "^$"
echo "--- flash-id"
esptool --chip esp32s3 --port "$PORT" --before default-reset --after no-reset flash-id 2>&1 | tail -6
echo "--- eFuse summary"
espefuse --chip esp32s3 --port "$PORT" --before default-reset --after no-reset summary 2>&1 | grep -iE "PSRAM|FLASH|MAC|PIN_POWER|VDD_SPI|USB|XTAL|PKG|WAFER|CHIP_DIS"
echo
echo "=== running-app console (no reset, 4 s) ========================"
timeout 8 python3 "$here/uart_tail.py" "$PORT" 4 2>&1 | head -40

echo
echo "saved -> $log"
