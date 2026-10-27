# ESP32-S3 Environment Review — zen-esp32

Probe date: session on Linux Mint 22.2. All items below are `[VERIFIED]` by an actual
command run in this session unless marked `[HYPOTHESIS]`.

---

## 1. Board detected (via bundled esptool v5.3.1 on /dev/ttyACM0)

| Item | Value |
|---|---|
| Chip | ESP32-S3 (QFN56) **rev v0.2**, dual-core Xtensa LX7 + LP core, 240 MHz |
| Wireless | Wi-Fi 4 (802.11 b/g/n), BLE 5 (LE) — **no Bluetooth Classic** |
| PSRAM | **8 MB embedded Octal (OPI) PSRAM, `AP_3v3`** |
| Flash | **16 MB**, mfr `0x1C`, device `0x7018`, **eFuse = quad (4 data lines), 3.3 V** |
| Crystal | 40 MHz |
| MAC | `68:ee:8f:5a:c4:44` |
| Module guess | **ESP32-S3-WROOM-2-N16R8** class (16 MB flash + 8 MB OPI PSRAM) |
| USB bridge | `1a86:55d3` QinHeng **CH9102/CH343** "USB Single Serial" → `/dev/ttyACM0` |
| Native USB | **not enumerated** (no `303a:` device) — only the UART bridge is wired up |
| Auto-reset | **works** (DTR/RTS). esptool connected + "Hard resetting via RTS pin" with no button press |

Boot log captured from the sketch currently flashed (Arduino core 3.3.12):

```
ESP-ROM:esp32s3-20210327  rst:0x1 (POWERON), boot:0x8 (SPI_FAST_FLASH_BOOT)
mode:DIO, clock div:1
E (25) qio_mode: Failed to set QIE bit, not enabling QIO mode
```

Consequences for config:
- Flash is **quad-only** → use **DIO/QOUT**, not QIO (`CONFIG_ESPTOOLPY_FLASHMODE_DIO`).
  The `qio_mode` error above is expected with QIO and is the thing to fix first.
- PSRAM must be explicitly enabled as **OPI** (`CONFIG_SPIRAM_MODE_OCT=y`,
  `CONFIG_SPIRAM_TYPE_AUTO`→AP, Arduino: `-DBOARD_HAS_PSRAM -DPSRAM_CFG_...`).
- GPIO **33–37 are consumed by OPI PSRAM** (SPIIO4–7 / SPIQS0). Never assign them.
- GPIO 26–32 = flash bus. GPIO 22–25 **do not exist** on S3.
- GPIO 19/20 = native USB D-/D+ — free to use only because native USB isn't brought out,
  but using them blocks `usb_jtag` console/flashing later.
- Console: UART0 on GPIO43/44 via CH9102. `ARDUINO_USB_CDC_ON_BOOT=0` for headless
  `arduino-cli` work (otherwise `Serial` goes to the unused native USB).

---

## 2. Blockers found

### 2.1 Serial port not writable by my session `[VERIFIED]`
```
/dev/ttyACM0 root:dialout crw-rw----
getent group dialout -> dialout:x:20:jang        # you ARE in the group
id -nG (running session) -> ... no dialout ...    # but the session predates the add
python3 open("/dev/ttyACM0") -> Errno 13 Permission denied
```
Fix: **full logout/login (or reboot)** — closing the terminal is not enough.
Workaround I used for every probe below (no re-login required):
```bash
sg dialout -c '<command>'          # verified working: PORT OPEN OK
```
Permanent alternative without re-login: a udev rule `MODE="0666"`, or `sudo setfacl -m u:jang:rw /dev/ttyACM0` (resets on replug).

### 2.2 No headless toolchain on PATH `[VERIFIED]`
`pio`, `platformio`, `idf.py`, `arduino-cli`, `esptool`, `esptool.py`, `python3 -m serial`
all MISSING. `cmake`, `ninja`, `git`, `uv 0.7.3`, `go`, `node` present.
The AppImage-mounted `arduino-cli` is **not reachable** (FUSE mount `/tmp/.mount_arduin*`
is `Permission denied` for my user), so a standalone `arduino-cli` binary is required.

---

## 3. Already installed and reusable — do NOT reinstall `[VERIFIED]`

`~/.arduino15` = 8.1 GB and already contains everything the compiler side needs:

| Asset | Path | Status |
|---|---|---|
| Arduino ESP32 core **3.3.12** | `~/.arduino15/packages/esp32/hardware/esp32/3.3.12` | present |
| Xtensa GCC **14.2.0** (`esp-14.2.0_20260121`) | `~/.arduino15/packages/esp32/tools/esp-x32/2601/bin/xtensa-esp32s3-elf-gcc` | **compiles OK** (test `.o` built) |
| **esptool v5.3.1** (+ espefuse, espsecure) | `~/.arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool` | **flashed-probe OK** |
| OpenOCD-ESP32 `v0.12.0-esp32-20260424` | `.../tools/openocd-esp32/*/bin` | present |
| xtensa-esp-elf-gdb, riscv32-esp-elf-gdb, mklittlefs, esp32s3-libs | `.../tools/*` | present |

Skill test suite runs green: `uv run --with pytest --with pyserial -q pytest tests/` → **50 passed**.
(`uv` is a sufficient Python strategy — no system pip pollution needed.)

Note: `/tmp/esptool`-style venv not needed; the bundled esptool is used by absolute path.

---

## 4. Tools I need — install list (in priority order)

### P0 — required to build, flash, and read logs headlessly

1. **arduino-cli** (headless build/upload/lib/boards; core + toolchains already on disk)
   ```bash
   curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin INSTALL_DIR=~/.local/bin sh
   ~/.local/bin/arduino-cli config init --overwrite --config-file ~/.arduinoCLI/arduino-cli.yaml   # keep your existing data/user dirs
   ```
   Needed so I can run `compile`, `upload`, `boards list`, `lib install` non-interactively.

2. **pyserial + esptool on PATH** (for monitor / efuse / secure boot work outside the IDE)
   ```bash
   uv tool install esptool        # gives `esptool`, `espsecure`, `espefuse`
   uv tool install pyserial       # gives `miniterm.py` -> our fallback serial monitor
   ```
   `uv tool install idf-component-manager`/`esp-idf-diag` optional.

3. **Re-login for `dialout`** (see 2.1) — or keep using `sg dialout -c`.

### P1 — strongly recommended for this specific board

4. **ESP-IDF v5.3.x or v5.4.x** (not installed) — Arduino alone gives no access to the two
   things this board needs tuned: `CONFIG_SPIRAM_MODE_OCT` and `CONFIG_ESPTOOLPY_FLASHMODE_DIO`.
   ```bash
   git clone -b v5.3.2 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
   ~/esp/esp-idf/install.sh esp32s3        # reuses its own tool downloads (~4-5 GB)
   # usage:  . ~/esp/esp-idf/export.sh
   ```
   cmake + ninja are already installed, which IDF needs.
5. **`idf-exe`/`psutil`/`pyelftools`** come with IDF's own venv — nothing extra to do.

### P2 — pick one, avoid duplicating P0/P1

6. **PlatformIO Core** (`uv tool install platformio`) — only if we want the `pio` workflow.
   It re-downloads its own core + toolchain (~2 GB). Skip if arduino-cli + IDF are installed.

### P3 — nice to have

7. **`espup`/`espflash`** (Rust, `cargo install espflash cargo-generate` needed — `rustc` MISSING).
   Only if we go the embedded-Rust route. Otherwise ignore.
8. **LVGL + drivers via arduino-cli lib**: `arduino-cli lib install "lvgl" "TJpg_Decoder" "ESP32_Servo"`.
9. **`zbar`/`qrcode`** — not needed. **`dfu-util`** — not needed (no ESP32-S2/esp32 BOOTROM DFU).
10. **`~/Arduino` sketchbook is empty** — I will scaffold the project there or in-repo; tell me which.

Disk budget: 44 GB free. P0 ≈ 60 MB, P1 ≈ 5 GB, P2 ≈ 2 GB. OK.

---

## 5. Skill review (`esp32-skills`) — real defects with repro

Reference loading, guardrail prose, and per-variant specs are good quality; the S3 strapping
and ADC2 rules fire correctly (`GPIO26 → FLASH_PIN`, `GPIO18 → ADC2_WIFI`, strapping warnings).
`references/` content (protocol, electrical, devices, LVGL, Waveshare) is the strongest part.

The **scripts + S3 data tables** have bugs that matter for *this* board:

### D1 (CRITICAL) OPI PSRAM pin conflict is not modelled — false negative
`scripts/platforms/esp32.py`:
```python
_PSRAM_PINS = { "esp32": {16, 17}, "esp32s2": set(), "esp32s3": set(), ... }
#                                          ^ "PSRAM uses dedicated pins on S3"  <- WRONG for OPI
```
On S3 modules with Octal PSRAM (N8R8 / **N16R8 = this board**) GPIO **33–37** are PSRAM
SPIIO4–7/SPIQS0 and are unusable. Repro:
```bash
python3 scripts/validate_pinmap.py --format json < /tmp/psram_clash.json
# -> {"valid": true, "errors": [], "warnings": []}   # GPIO33-37 as RGB data lines
```
Silently broken PSRAM. `references/esp32-s3/specs.md:26` only says
"Octal PSRAM: May reserve additional pins (check specific module)" — no pin list.

### D2 (HIGH) ESP32-S3 pin DB is a generic stub
```
esp32s3 db pins: 49 (GPIO0-48)          # silicon has 45: 0-21, 26-48
free pins: [..., 22, 23, 24, 25, 33, ..., 48]
with alt_functions: 8 of 49
```
- **GPIO22/23/24/25 don't exist on S3 but are reported `usability='free'`** — `get_pin(22)`
  returns an auto-generated placeholder. Validation of a pin on a non-existent GPIO passes.
- **GPIO33–37 also reported `free`** (see D1).
- 41 of 49 S3 pins have empty `alternate_functions`, so no matrix/peripheral-aware checking.

### D3 (HIGH) `module` field rejects real module names and aborts hard
```json
{"variant":"esp32s3","module":"WROOM-18N Octal PSRAM"}
```
→ `Validation error: Unsupported module 'WROOM-18N OCTAL PSRAM'. Supported: WROOM, WROVER`,
non-zero exit, **no JSON emitted** (callers can't parse a failure). Module vocabulary
WROOM/WROVER is original-ESP32-era; S3 modules are `N8R8 / N16R8 / N16R2 / NH` etc.

### D4 (MED) `generate_config.py` emits non-working Arduino code
- WS2812 treated as generic LEDC PWM: `ledcAttach(NEOPIXEL_PIN, 1000, 8)` — WS2812 needs
  ~800 kHz timing (RMT / NeoPixelBus / `neopixelWrite` on core 3.x).
- UART assignment ignored: spec was `GPIO1 = UART0_TX`, output `Serial1.begin(9600);` with
  comment `// Using default UART pins` — wrong object, wrong baud, pins dropped.
- I2C placeholder `const uint8_t SSD1306_ADDR = 0x00;` (real default 0x3C, and `common-devices.md` knows it).

### D5 (MED) Missing S3 reserved-pin checks
`{"gpio":19,"function":"USB_D_MINUS"}` and GPIO20 produce **no warning** despite being
native USB D-/D+ on S3. On boards that flash over native USB this is a footgun.

### D6 (LOW) Test suite blind spot
50 tests pass with all of D1–D5 present: no fixture for OPI PSRAM, S3 module naming,
non-existent S3 GPIOs, or WS2812 codegen. `tests/fixtures/` has `regression_esp32s3_flash_pin.json`
and strapping, but nothing PSRAM-related.

### D7 (LOW) `specs.md` omissions for S3
No mention of: GPIO22–25 absence, GPIO33–37 OPI reservation, LP core, flash-vs-PSRAM
voltage/eFuse interaction, 40 vs 26 MHz crystal differences, USB pins 19/20 in the safety section.

Suggested fixes if you want me to patch the skill:
`_PSRAM_PINS["esp32s3"] = set()` → make it module-aware (`"N*R8"`/`"OPI"` → `{33..37}`);
add `_NONEXISTENT_PINS = {"esp32s3": {22,23,24,25}}`; accept real module strings (normalise
`N16R8`, `WROOM-2`, unknown → warning + continue, still emit JSON); add USB pin rule;
WS2812/RMT and UART codegen fixes; then regression fixtures per fix.

---

## 6. What I can already do without new installs

| Capability | Status |
|---|---|
| Detect chip / flash / PSRAM / MAC, efuse read | ✅ `sg dialout -c '<bundled esptool> ...'` |
| Erase / write raw `.bin` images | ✅ same (stub flasher verified) |
| Read/monitor UART console at 115200, pulse reset | ✅ raw `termios` helper `/tmp/serread.py` (no pyserial) |
| Skill pinmap validation + config codegen | ✅ (with the D1–D5 caveats above) |
| Skill test suite | ✅ `uv run --with pytest -q pytest tests/` (50 passed) |
| Compile Arduino sketch headlessly | ❌ needs `arduino-cli` |
| Compile ESP-IDF project / sdkconfig tuning | ❌ needs ESP-IDF |
| GDB/OpenOCD source-level debug | ❌ tools present, but needs OpenOCD config + (for on-chip JTAG) the native USB port |

## 7. Recommended immediate sequence

1. Re-login (activates `dialout`), install **P0.1 + P0.2**.
2. I scaffold a project targeting `esp32s3` with `--flash-mode dio --flash-size 16mb` and PSRAM
   **OPI** (`-DBOARD_HAS_PSRAM`). Exact FQBN + build flags to be read from
   `arduino-cli board details` after install — not guessed.
   **`USB CDC On Boot` stays Disabled**: the only connected console is UART0 (GPIO43/44) behind
   the CH9102, and enabling CDC-on-boot redirects `Serial` to the unconnected native USB.
3. Blink-on-GPIO48 + heap/PSRAM report over UART0 as the smoke test (validates toolchain,
   flash mode, and PSRAM init in one shot).
4. Patch skill D1/D2/D3 (highest blast radius) with regression fixtures.
