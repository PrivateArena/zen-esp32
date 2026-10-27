#!/usr/bin/env bash
# Repro kit for ENV-REVIEW.md section 5 (esp32-skills defects).
# Usage: ./run.sh [path-to-esp32-skills]
set -u
SKILL="${1:-../esp32-skills}"
V="$SKILL/scripts/validate_pinmap.py"
G="$SKILL/scripts/generate_config.py"
run() { printf '\n=== %s\n' "$1"; printf 'expected: %s\n' "$2"; }
show() { python3 "$V" --format json < "$1" 2>&1 | python3 -c 'import json,sys
raw=sys.stdin.read()
try:
    d=json.loads(raw)
except Exception:
    print("  !! non-JSON output (callers cannot parse):", raw.strip()[:120]); raise SystemExit
print("  valid=%s errors=%s warnings=%s" % (d["valid"], [e["code"]+":"+str(e["gpio"]) for e in d["errors"]], sorted({str(w["gpio"])+":"+w["code"] for w in d["warnings"]})))'
}
run "D1 GPIO33-37 on OPI-PSRAM S3 (this board)" "invalid -> PSRAM pins reserved"      ; show inputs/d1_s3_octal_psram_gpio33_37.json
run "D2 GPIO22-25 do not exist on ESP32-S3"     "invalid -> non-existent pins"       ; show inputs/d2_s3_nonexistent_gpio22_25.json
run "D3 real module string 'WROOM-18N Octal PSRAM'" "warning + JSON"                 ; show inputs/d3_real_module_name.json
run "D5 GPIO19/20 = native USB D-/D+ on S3"     "warn reserved USB pins"             ; show inputs/d5_s3_native_usb_gpio19_20.json
run "Guardrails that DO work (flash/ADC2/strap)" "invalid -> FLASH_PIN + ADC2_WIFI"   ; show inputs/guardrails_s3_should_fail.json
printf '\n=== D4 codegen: WS2812 + UART pin fidelity\n'; printf 'expected: RMT/neopixelWrite + UART0 on GPIO1\nactual:\n'
python3 "$G" --format json --framework arduino < inputs/baseline_s3_devkit.json | python3 -c 'import json,sys
c=json.load(sys.stdin)["init_code"]
print("\n".join("  "+l for l in c.splitlines() if "ledc" in l.lower() or "Serial" in l or "NEOPIXEL_PIN" in l))'
