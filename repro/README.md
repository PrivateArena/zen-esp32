# repro/ — defect reproduction kit for `esp32-skills`

Backs section 5 of `../ENV-REVIEW.md`. Every case was run in-session.

```bash
./run.sh                      # uses ../esp32-skills
./run.sh /path/to/esp32-skills
```

| Input | Defect | Actual today |
|---|---|---|
| `inputs/d1_s3_octal_psram_gpio33_37.json` | D1 — OPI PSRAM (this board: 8 MB octal) reserves GPIO33–37 | `valid=True` (false negative) |
| `inputs/d2_s3_nonexistent_gpio22_25.json` | D2 — GPIO22–25 do not exist on ESP32-S3 | `valid=True` |
| `inputs/d3_real_module_name.json` | D3 — module vocabulary is WROOM/WROVER only | hard error, **no JSON**, exit 1 |
| `inputs/d5_s3_native_usb_gpio19_20.json` | D5 — GPIO19/20 are native USB D-/D+ on S3 | no warning |
| `inputs/guardrails_s3_should_fail.json` | control: working guardrails | `FLASH_PIN:26`, `ADC2_WIFI:18`, strapping warns |
| `inputs/baseline_s3_devkit.json` | D4 — codegen fidelity | WS2812 → `ledcAttach(pin,1000,8)`; UART GPIO ignored → `Serial1.begin(9600)` |

`uart_probe.py` — pyserial-free UART reader + DTR/RTS reset pulse (used to capture the board's
boot log before pyserial was installed):

```bash
sg dialout -c 'python3 uart_probe.py /dev/ttyACM0 115200 5'
```
