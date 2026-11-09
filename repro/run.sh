#!/usr/bin/env bash
# Repro / verification kit for the esp32-skills defects D1-D8
# (ENV-REVIEW.md §5, ENV-REVIEW2.md §13 + §17). Pure software: no board needed.
#
# Usage: ./run.sh [path-to-esp32-skills]      # exit 0 only if every case passes
set -u
SKILL="${1:-../esp32-skills}"
C="./check.py"
FAIL=0

check() {  # every assertion is one check.py invocation
  python3 "$C" "$1" "$2" "$3" "$4" "$SKILL" || FAIL=1
}

echo "=========== Rev 1 §5 / Rev 2 §13 defects: expected FIXED ==========="
check validate-invalid "D1  GPIO33-37 on OPI-PSRAM S3 (this board)"  inputs/d1_s3_octal_psram_gpio33_37.json  PSRAM_PIN
check validate-invalid "D1b same board via psram.mode=octal"         inputs/d1b_s3_psram_field_octal.json     PSRAM_PIN
check validate-invalid "D2  GPIO22-25 do not exist on ESP32-S3"     inputs/d2_s3_nonexistent_gpio22_25.json  NON_EXISTENT_PIN
check validate-warn    "D3  real module string still emits JSON"     inputs/d3_real_module_name.json          MODULE_UNKNOWN
check code-has         "D4a WS2812 -> rgbLedWrite()"                 inputs/baseline_s3_devkit.json           rgbLedWrite
check code-lacks       "D4b no ledcAttach() for a WS2812"            inputs/baseline_s3_devkit.json           ledcAttach
check code-has         "D4c UART0 keeps its pins + right object"     inputs/baseline_s3_devkit.json           "Serial0.begin(115200, SERIAL_8N1, -1, CONSOLE_UART0_TX_PIN)"
check code-has         "D4d SSD1306 address 0x3C"                    inputs/baseline_s3_devkit.json           0x3C
check validate-warn    "D5  GPIO19/20 = native USB D-/D+ on S3"      inputs/d5_s3_native_usb_gpio19_20.json     USB_PIN
check validate-invalid "D8  ESP32-C6: GPIO30 is a flash pin"         inputs/d8_c6_flash_pins_gpio30.json       FLASH_PIN
check validate-gpio-clean "D8b ESP32-C6: GPIO27 is a user pin"       inputs/d8_c6_flash_pins_gpio30.json       27

echo
echo "=========== Guardrails that already worked (must keep working) ==========="
check validate-invalid "CTRL flash pin 26 + ADC2/WiFi 18 + strapping" inputs/guardrails_s3_should_fail.json    FLASH_PIN

echo
echo "=========== D7 documentation ==========="
grep -q "GPIO33-37" "$SKILL/references/esp32-s3/specs.md" \
  && echo "PASS  specs.md documents the octal PSRAM reservation" \
  || { echo "FAIL  specs.md missing GPIO33-37"; FAIL=1; }
grep -q "N16R8" "$SKILL/references/esp32-s3/modules.md" \
  && echo "PASS  modules.md carries the real ordering codes" \
  || { echo "FAIL  modules.md missing"; FAIL=1; }
grep -q "Never re-parent an MSPI pad" "$SKILL/references/esp32-s3/specs.md" \
  && echo "PASS  specs.md documents the MSPI re-parent hazard" \
  || { echo "FAIL  specs.md missing the re-parent hazard"; FAIL=1; }

echo
echo "=========== D9 board profiles (one per physical board) ==========="
BD="$SKILL/references/esp32-s3/boards"
for f in README.md zen-esp32-rig.md yuanxinwei-esp32s3-cam-sd.md; do
  [ -s "$BD/$f" ] && echo "PASS  boards/$f present" || { echo "FAIL  boards/$f missing"; FAIL=1; }
done
# every profile the index links to must exist, and every profile must name its module class
for f in $(grep -oE '\]\([a-z0-9._-]+[.]md\)' "$BD/README.md" | sed 's/^](//; s/)$//'); do
  [ -f "$BD/$f" ] && echo "PASS  index link resolves: $f" || { echo "FAIL  boards/README.md links a missing $f"; FAIL=1; }
done
for f in "$BD"/*-*.md; do
  grep -qE "N[0-9]+R[0-9]+|no PSRAM|module class" "$f" \
    && echo "PASS  $(basename "$f") states its module class" \
    || { echo "FAIL  $(basename "$f") has no module class (reserved pins depend on it)"; FAIL=1; }
done

echo
if [ "$FAIL" -eq 0 ]; then echo "ALL CASES PASS"; else echo "SOME CASES FAILED"; fi
exit $FAIL
