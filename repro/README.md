# repro/ — defect reproduction + verification kit for `esp32-skills`

Backs section 5 of `../ENV-REVIEW.md` (rev 1) and sections 13/17 of
`../ENV-REVIEW2.md` (rev 2). Every case here was executed; nothing is inferred.

As of the D1-D8 patch run, `run.sh` is both the repro *and* the verification:
it asserts the fixed behaviour and exits non-zero if any case regresses.

```bash
./run.sh                      # uses ../esp32-skills   -> "ALL CASES PASS", exit 0
./run.sh /path/to/esp32-skills
python3 check.py validate-invalid <label> <input.json> <CODE> ../esp32-skills   # one case
```

## Cases

| Input | Defect | Before the patch | Asserted now |
|---|---|---|---|
| `inputs/d1_s3_octal_psram_gpio33_37.json` | D1 — OPI PSRAM (this board: 8 MB octal) reserves GPIO33-37 | `valid=True` (false negative) | error `PSRAM_PIN` ×5 |
| `inputs/d1b_s3_psram_field_octal.json` | D1 via the explicit `psram.mode` field | no such option | error `PSRAM_PIN` |
| `inputs/d2_s3_nonexistent_gpio22_25.json` | D2 — GPIO22-25 do not exist on ESP32-S3 | `valid=True` | error `NON_EXISTENT_PIN` ×4 |
| `inputs/d3_real_module_name.json` | D3 — module vocabulary was WROOM/WROVER only | hard error, **no JSON**, exit 1 | JSON + `MODULE_UNKNOWN` warning |
| `inputs/baseline_s3_devkit.json` | D4 — codegen fidelity | `ledcAttach(NEOPIXEL_PIN,…)`, `Serial1.begin(9600)`, `0x00` addresses | `rgbLedWrite()`, `Serial0.begin(…, CONSOLE_UART0_TX_PIN)`, `0x3C` |
| `inputs/d5_s3_native_usb_gpio19_20.json` | D5 — GPIO19/20 are native USB D-/D+ | no warning | warning `USB_PIN` ×2 |
| `inputs/d8_c6_flash_pins_gpio30.json` | D8 (new) — C6 MSPI pads | GPIO30 treated as free, GPIO27 treated as flash | `FLASH_PIN` at 30, GPIO27 clean |
| `inputs/guardrails_s3_should_fail.json` | control | worked | still works (`FLASH_PIN`, `ADC2_WIFI`, `STRAPPING_PIN`) |

D6 (test blind spot) is closed by `../esp32-skills/tests/test_module_and_pinmap_defects.py`
and `test_code_fidelity.py`: 50 → 96 tests, one per fixed behaviour.
D7 (documentation) is checked by grepping `references/esp32-s3/specs.md` and
`references/esp32-s3/modules.md`.

## Board-touching helpers (only these need the ESP32-S3 connected)

`uart_tail.py` — pure-`termios` capture, **no DTR/RTS pulse**, so it never resets
the board. `uart_probe.py` — same but pulses DTR/RTS (it *does* reboot the target);
positional arguments, not flags.

```bash
PORT=/dev/serial/by-path/pci-0000:0d:00.0-usb-0:4.4:1.0   # resolve with: ls -l /dev/serial/by-path/
python3 uart_tail.py $PORT 5
```

USB-Serial/JTAG (`303a:1001`) ignores the baud rate entirely; the CH9102 needs
115200. The by-path node changes with the physical port — always list it first
(`ENV-REVIEW2.md` §9/§17).

## Firmware kept as evidence

`firmware/psram_smoke/` (heap + OPI PSRAM read/write report) and
`firmware/rgb_blink/` (WS2812 colour cycle on GPIO48 = `PIN_RGB_LED`). Both build
with the FQBN recorded in `../ENV-REVIEW2.md` §10.1.
