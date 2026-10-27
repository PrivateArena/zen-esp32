# ESP32-S3 Environment Review — Rev 2 (zen-esp32)

Continuation of `ENV-REVIEW.md` (rev 1). Section numbering continues from rev 1 §7.
Probe date: session on Linux Mint 22.2, board physically connected and flashed during this session.
Everything below is `[VERIFIED]` by an actual command run in this session unless marked `[HYPOTHESIS]`.

**Headline: P0 + P1 are installed and the full loop build → flash → monitor → blink-the-RGB-LED
is proven on hardware. Rev 1 §1/§7.2 console guidance is now wrong for the current cable
position (see §9). 4 new blockers, none fatal; the only one that needs you is the `dialout` re-login.**

---

## 0. Status matrix

| Rev 1 item | Status | Evidence |
|---|---|---|
| P0.1 `arduino-cli` | ✅ done | `arduino-cli version` = 1.5.2-rc.1, `core list` = `esp32:esp32 3.3.12` |
| P0.2 `esptool` + pyserial | ✅ done | `esptool v5.6.0`, `pyserial-miniterm`, `pyserial-ports` in `~/.local/bin` |
| P0.3 re-login for `dialout` | ✅ **done (user, after PC restart)** | `id -nG` → `jang adm dialout cdrom sudo dip plugdev users lpadmin sambashare docker`; no `sg` wrapper needed any more this session |
| P1.4 ESP-IDF | ✅ done, **v5.5.6 not v5.3.2**, portable `./idf` + `./tools` | `idf.py build` of `hello_world` → `Project build complete`, exit 0 |
| P1.5 IDF venv extras | ✅ done | `tools/python_env` present, `idf.py`/`idf_size.py`/`idf_monitor.py` run |
| P2 PlatformIO | ➖ skipped (correct call) | arduino-cli + IDF cover both workflows |
| Rev 1 §7.3 smoke test | ✅ done | PSRAM 8 MB detected + 1 MB write OK; `E (25) qio_mode` gone (DIO image header) |
| Rev 1 §7.4 skill D1–D7 patches | ✅ **done** | all D1-D7 fixed + D8 found, 96/96 tests, `repro/run.sh` asserts and exits 0 — see §17 |

---

## 8. Installed toolchain inventory `[VERIFIED]`

| Asset | Path | Version | Note |
|---|---|---|---|
| arduino-cli | `/home/jang/.local/bin/arduino-cli` | 1.5.2-rc.1 (fef6e48df, 2026-07-23) | release-candidate build; works fine headless |
| arduino-cli config | `~/.arduino15/arduino-cli.yaml` | — | **file does not exist** → built-in defaults; `data=/home/jang/.arduino15`, `user=/home/jang/Arduino` |
| ESP32 Arduino core | `~/.arduino15/packages/esp32/hardware/esp32/3.3.12` | 3.3.12 | intact after `cache clean`; compiled + flashed after the clean |
| Xtensa GCC | `.../packages/esp32/tools/esp-x32/2601` | esp-14.2.0_20260121 | |
| bundled esptool | `.../packages/esp32/tools/esptool_py/5.3.1` | 5.3.1 | used by `arduino-cli upload` |
| prebuilt libs (S3) | `.../packages/esp32/tools/esp32s3-libs/3.3.12/{qio,dio}_{opi,qspi}/` | — | **the variant dir is the real config switch** (§10.2) |
| esptool (CLI) | `~/.local/bin/{esptool,espefuse,espsecure}` | 5.6.0 | from `uv tool install esptool` |
| pyserial | `~/.local/bin/{pyserial-miniterm,pyserial-ports,esp_rfc2217_server}` | 3.5 | command is `pyserial-miniterm`, *not* `miniterm.py` (rev 1 §4.2 was wrong) |
| ESP-IDF | `/media/jang/home/Deve/zen-esp32/idf` | **v5.5.6** | 545 MB, **no `.git`** (release tarball) |
| IDF tools | `/media/jang/home/Deve/zen-esp32/tools` | 3.9 GB | `xtensa-esp-elf esp-14.2.0_20260121`, `riscv32-esp-elf`, `esp32ulp-elf`, `esp-rom-elfs`, `openocd-esp32 v0.12.0-esp32-20260831`, `xtensa-esp-elf-gdb 17.1_20260402`, `python_env` |
| Env entry | `. ./activate.sh` | — | sets `IDF_PATH`, `IDF_TOOLS_PATH`, sources `export.sh` |
| OpenOCD | `tools/tools/openocd-esp32/v0.12.0-esp32-20260831/openocd-esp32/bin/openocd` | v0.12.0-esp32-20260831 | **on PATH after activate** (as `openocd`, not `openocd-esp32`) |
| GDB | `tools/tools/xtensa-esp-elf-gdb/17.1_20260402/xtensa-esp-elf-gdb/bin` | 17.1 | on disk, **NOT on PATH** — installed with `-e core`, lacks the `dbg` feature set |

After `. ./activate.sh`: `idf.py` ✅ `openocd` ✅ `xtensa-esp32s3-elf-gcc` ✅ `esptool` ✅ `idf_size.py` ✅ —
`xtensa-esp-elf-gdb` ❌ `riscv32-esp-elf-gdb` ❌. `tools/idf-env.json` records `"features": ["core"]`, and
`export.sh` only publishes PATH entries for the recorded feature set — that is why the two GDB dirs are
on disk but invisible. Two fixes (both no-download):
```bash
# (a) explicit, deterministic — append to activate.sh or the shell:
export PATH="$IDF_TOOLS_PATH/tools/xtensa-esp-elf-gdb/17.1_20260402/xtensa-esp-elf-gdb/bin:$PATH"
# (b) let the installer re-register the extra feature set (features are passed as --enable-<name>,
#     see idf/tools/install_util.py:14):
. ./activate.sh && ./idf/install.sh esp32s3 --enable-gdb --enable-openocd
```
`idf.py openocd` / `idf.py gdb` resolve these tools internally, so (b) is only needed for standalone use.

Disk: 410 GB free on `/media/jang/home` (rev 1's "44 GB" no longer applies to this mount).

---

## 9. Board & USB topology — rev 1 corrections `[VERIFIED]`

Rev 1 §1 concluded "native USB not enumerated — only the UART bridge is wired up". That was an
artefact of which end of the cable chain was active. The truth:

**The board's CH9102 and the ESP32-S3 native USB share ONE host connection and alternate on it.**
Both appear and disappear on the same physical port `usb 1-4.4`:

```
13:30:27 usb 1-4.4: USB disconnect, device number 42          # was 303a:1001
13:30:30 usb 1-4.4: new device 1a86:55d3 "USB Single Serial" 5D01162178   # app running, no USB claim
13:44:03 usb 1-4.4: USB disconnect, device number 45
13:44:07 usb 1-4.4: new device 303a:1001 "USB JTAG/serial debug unit" 68:EE:8F:5A:C4:44
```

| State of the ESP32-S3 | Host sees | `Serial` reaches host? |
|---|---|---|
| ROM download mode entered via USB | `303a:1001` (USB-Serial-JTAG) | ROM log yes (ROM console), app no |
| App with `CDCOnBoot=default` (Serial=UART0) | `1a86:55d3` (CH9102, GPIO43/44) | yes — but only in that state |
| App with `CDCOnBoot=cdc` (HWCDC) | `303a:1001`, **stable across reflash** | yes |

`[HYPOTHESIS]` mechanism = USB-2.0 auto-select switch on D+/D− chosen by whichever side asserts the
DP pull-up first; not needed for any conclusion below.

**Consequences — read this before copying rev 1 commands:**

1. **`CDCOnBoot=cdc` is now the right default**, the opposite of rev 1 §7.2. It keeps the same USB
   identity (`303a:1001`, MAC as iSerialNumber) across flashing *and* running, so `/dev/ttyACM0`
   never changes role and no cable swap / BOOT button is ever needed. UART0 (`GPIO43/44`) stays free
   for a second console or for `Serial0` use.
2. Flashing over the native USB is **fast**: `Wrote 308080 bytes (177592 compressed) at 0x00010000 in
   2.1 seconds (1183.4 kbit/s)`. Auto-reset works (`rst:0x15 (USB_UART_CHIP_RESET)`), so rev 1 §1's
   "auto-reset works via CH9102" is no longer the only path.
3. **Port names are volatile.** `/dev/ttyACM0` momentarily *disappears* at each mux flip — a probe
   run caught `FileNotFoundError: '/dev/ttyACM0'` at 13:44. Pin the path instead of the letter; it is
   identical for both bridges:
   ```bash
   PORT=/dev/serial/by-path/pci-0000:0d:00.0-usb-0:4.4:1.0   # same for 1a86 and 303a
   ```
4. **On-chip JTAG is now physically available** (interface 1 of `303a:1001`), which rev 1 §6 marked
   ❌ permanently. Blocked only by USB device permissions — see B2.

### 9.1 On-board LED / RGB facts (new, from core sources + user confirmation)

| Fact | Value | Source |
|---|---|---|
| `PIN_RGB_LED` | **GPIO 48** | `variants/esp32s3/pins_arduino.h:12` |
| `LED_BUILTIN` / `RGB_BUILTIN` | **97** = `SOC_GPIO_PIN_COUNT (49) + 48` → a *pseudo-pin*, not a GPIO | same file, runtime print |
| `RGB_BRIGHTNESS` | 64 | same file |
| API | `rgbLedWrite(RGB_BUILTIN, r, g, b)`; **`neopixelWrite()` is `[[deprecated]]`** in 3.3.12 | `cores/esp32/esp32-hal-rgb-led.h:29-34` |
| `digitalWrite(LED_BUILTIN, …)` | works, but only white on/off on that strip | variant comment + codegen |
| Board-design warning | core comment: some boards have **too low voltage on GPIO48**; then move the strip to a 3V3-referenced pin (e.g. 38) | `pins_arduino.h:11-12` |

User confirmed the cycle **RED → GREEN → BLUE → WHITE → OFF**, 150 ms per step, on the onboard LED.
Board model is still not stated — rev 1's "ESP32-S3-WROOM-2-N16R8 class" guess stands, now additionally
consistent with "single USB-C + CH9102 auto-mux + WS2812 on 48".

---

## 10. Verified pipeline (the commands that actually worked)

### 10.1 Build / flash / monitor

```bash
cd /media/jang/home/Deve/zen-esp32
# NOTE (§17): the by-path node changed after the PC restart — resolve it, do not copy a literal:
PORT=$(ls -1 /dev/serial/by-path/* | head -1)     # was usb-0:4.4, now pci-0000:0d:00.0-usb-0:12:1.0
FQ='esp32:esp32:esp32s3:FlashMode=dio,FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadMode=default,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'

arduino-cli compile --fqbn "$FQ" --build-path build sketch/         # 4 s incremental
arduino-cli upload --input-dir build --fqbn "$FQ" --port $PORT -v    # dialout is active now (B1 closed)
python3 repro/uart_tail.py $PORT 5                                  # NON-destructive capture: no DTR/RTS pulse
python3 repro/uart_probe.py $PORT 115200 5                          # capture WITH a board reset (see note)
# repro/uart_tail.py = pure termios reader, no pyserial, prints raw bytes for N seconds.
# USB-Serial-JTAG (HWCDC) ignores the baud rate entirely — no 115200 handshake needed
```

`--port` with the by-path **symlink** is accepted by `arduino-cli upload` `[VERIFIED]` (re-running the
upload with no source change prints `No changed sectors found` + `... verified.` → idempotent).

Measured: blink sketch `302768 B (9%)` flash / `22816 B (6%)` RAM, 16M partition table
(`app0 0x300000`, `app1 0x300000`, `ffat 0x9E0000`, `coredump @0xFF0000`) → fits 16 MB flash.
`Hash of data verified` on every upload.

### 10.2 Flash-mode / PSRAM fix, confirmed two ways

* Image header (`esptool image-info build/*.bin`): `Flash mode: DIO`, `Flash size: 16MB`, `80m`, `Chip ID: 9`.
* Boot log no longer contains `E (25) qio_mode: Failed to set QIE bit` (rev 1 §1 target achieved).
* The real switch is the **prebuilt-lib variant**: `FlashMode=dio` + `PSRAM=opi` → include path
  `esp32s3-libs/3.3.12/dio_opi/include` which contains `CONFIG_SPIRAM_MODE_OCT 1` +
  `CONFIG_ESPTOOLPY_FLASHMODE_DIO 1`.
* **Trap:** the `sdkconfig` written into the Arduino build dir still says
  `# CONFIG_SPIRAM_MODE_OCT is not set` / `CONFIG_SPIRAM_MODE_QUAD=y`. It is a copy of the core
  default and is *not* used for PSRAM — do not "fix" or trust it. Grep `compile_commands.json` for
  `esp32s3-libs/3.3.12/dio_opi/include` instead.
* Runtime evidence (`psram_smoke`): `PSRAM total=8388608 free=8386096 -> OPI PSRAM OK`,
  `PSRAM 1MB alloc+write ok`, `internal free=345688 min=340540`, `flash=16MB psramFound=1`.

### 10.3 Artefacts kept

`repro/firmware/psram_smoke/psram_smoke.ino` (heap + OPI PSRAM read/write report) and
`repro/firmware/rgb_blink/rgb_blink.ino` (Blink.ino adapted to the WS2812 on GPIO48, colour cycle +
serial log). Both compile with the FQBN above. Also added: `repro/uart_tail.py` — the pure-`termios`
capture helper used for every log read this session (replaces rev 1's throw-away `/tmp/serread.py`).
Move them into the real project once §16.2 is decided.

---

## 11. ESP-IDF portable install — verified + 3 traps

`. ./activate.sh && idf.py --version` → `ESP-IDF v5.5.6` (prints `WARNING: Git version unavailable,
reading from source` because `idf/` has no `.git`). `hello_world` for `esp32s3` built clean
(`Project build complete`, bootloader 0x4ff0 B, image header DIO / Chip ID 9).

**Trap 1 — `CONFIG_*` on the `idf.py` command line is silently ignored.**
`idf.py -D CONFIG_SPIRAM=y build` after `set-target` produces nothing; the already-materialised
`sdkconfig` wins. Use `sdkconfig.defaults` (+ `rm sdkconfig`) or `menuconfig`.

**Trap 2 — PSRAM symbols do not exist unless `esp_psram` is a component dependency.** In a fresh
example the component list is trimmed, so the defaults file is rejected with:
```
warning: unknown kconfig symbol 'SPIRAM' assigned to 'y' in sdkconfig.defaults
warning: unknown kconfig symbol 'SPIRAM_MODE_OCT' assigned to 'y' ...
```
Fix in the app: `idf_component_register(... PRIV_REQUIRES esp_psram spi_flash)`.

**Trap 3 — default `CONFIG_ESPTOOLPY_FLASHSIZE="2MB"`**, so an unmodified IDF build writes a 2 MB
image header onto a 16 MB chip.

Working `sdkconfig.defaults` for this exact board (verified: `CONFIG_SPIRAM=y`,
`CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED=80`, `CONFIG_ESPTOOLPY_FLASHMODE="dio"`,
`CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`, image header `DIO / 16MB`, `reconfigure` warning-free):

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y   # already default: IDF logs to UART0 *and* USB-JTAG
```
```bash
rm -f sdkconfig && idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
```
Note `tools/espidf.constraints.v5.5.txt` is in place (Python version constraints resolved OK) and
`tools/idf-env.json` registers the install as `idf-v5.5` while the directory is `idf` — harmless for
CLI use, may confuse the VS Code ESP-IDF extension.

---

## 12. Remaining blockers

**B1 — ✅ CLOSED (this session).** Re-login done: `id -nG` now contains `dialout`, and every
`esptool` / `espefuse` / `arduino-cli upload` / `uart_tail.py` call in §17 ran without `sg`.
`getent group dialout` → `dialout:x:20:jang`.

**B2 — on-chip JTAG: USB node permissions `[VERIFIED]`.** Still open: `/etc/udev/rules.d/` is
empty and `/dev/bus/usb/001/031` (the `303a:1001` device) is still `crw-rw-r-- root:root`, so
`dialout` does not cover it. OpenOCD reaches the adapter but cannot open it:
```
$ openocd -f board/esp32s3-builtin.cfg -c "adapter speed 2000" -c "init; targets; shutdown"
Info : esp_usb_jtag: capabilities descriptor set to 0x2000
Error: libusb_open() failed with LIBUSB_ERROR_ACCESS
Error: esp_usb_jtag: could not find or open device!
```
`/dev/bus/usb/001/048` = `crw-rw-r-- root:root` — group is `root`, so `sg dialout` does **not** help;
needs the `SUBSYSTEM=="usb"` rule below (or `sudo openocd`). Also: use `board/esp32s3-builtin.cfg`
(→ `interface/esp_usb_jtag.cfg`), **not** `interface/usb-jtag.cfg` (that one is the FTDI
`usb_blaster` "Van Ooijen" adapter and dies with a `usb_blaster` syntax error).

Still to run by hand (`sudo -n` needs a password):
```bash
sudo tee /etc/udev/rules.d/99-esp32s3.rules >/dev/null <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666", SYMLINK+="esp32s3-console"
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", MODE="0666", SYMLINK+="esp32s3-console"
SUBSYSTEM=="usb",  ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```
The tty lines are convenience only (they add a stable `esp32s3-console` symlink, which also
sidesteps B3); the `SUBSYSTEM=="usb"` line is what actually unblocks B2/JTAG.

**B3 — `/dev/ttyACM0` vanishes during mux flips** — see §9.3; always script against
`/dev/serial/by-path/…`.

**B4 — `arduino-cli monitor` is useless non-interactively `[VERIFIED]`:** it exits rc=0 with 0 bytes
of output even under `script -qec` and with `--raw`. `--describe` works. For scripted capture use
`repro/uart_tail.py <port> <secs>` (no reset) or `repro/uart_probe.py <port> <baud> <secs>`
(positionals, not `--help`; **pulses DTR/RTS so it reboots the board**).

**B5 — IDF GDB not on PATH** (§8 table). `tools/idf-env.json` recorded `features: ["core"]` only, so
`export.sh` does not publish the GDB dirs even though they are installed. Fix with the PATH append or
`./idf/install.sh esp32s3 --enable-gdb --enable-openocd` shown in §8.

---

## 13. Skill defects D1–D7: unchanged `[VERIFIED]` (re-run this session)

> Superseded later in the same session: §17 re-runs all of these as assertions and they now pass.
> Kept verbatim as the "before" record.

```
$ repro/run.sh
D1 GPIO33-37 on OPI-PSRAM S3   expected invalid   actual valid=True errors=[] warnings=[]
D2 GPIO22-25 non-existent      expected invalid   actual valid=True errors=[] warnings=[]
D3 real module string          expected JSON      actual !! non-JSON output (callers cannot parse)
D5 GPIO19/20 = native USB      expected warn      actual valid=True errors=[] warnings=[]
guardrails (still good)        valid=False FLASH_PIN:26 ADC2_WIFI:18  warn 45/46:STRAPPING_PIN
D4 codegen                     ledcAttach(NEOPIXEL_PIN, 1000, 8)  +  Serial1.begin(9600)  // pins dropped
```
D4 is now demonstrably wrong *for this board*: its baseline spec puts the NeoPixel on **GPIO 48**,
which is exactly `PIN_RGB_LED` — the generated `ledcAttach()` cannot drive a WS2812. Correct output is
`rgbLedWrite(RGB_BUILTIN, r, g, b)` (or RMT/NeoPixelBus). Add to the fix list: `variants/esp32s3`
already models `PIN_RGB_LED=48`; the skill should read that constant rather than treat 48 as free PWM.

---

## 14. Housekeeping & rev 1 doc drift

1. **This session deleted `~/.arduino15/staging` (1.8 GB).** Cause: an early `arduino-cli cache clean`
   (rev 1's own command list). Contents were leftover download archives; `packages` (6.8 GB) is intact
   and was re-validated by compiling + flashing after the clean. No action needed.
2. `~/.arduino15` is a **symlink** → `/media/jang/home/AppImage/arduino-ide.AppImage.home/.arduino15`.
   `du -sh ~/.arduino15` reports 4 KB (does not follow symlinks) — that is why rev 1's "8.1 GB" looked
   wrong. Consequence: `arduino-cli` only finds the core/toolchains because of that symlink; if the
   AppImage home is removed the 6.8 GB install becomes unreachable. Consider `arduino-cli config set
   directories.data` to a real in-repo path, or make the data dir a real directory.
3. Rev 1 §4.1's `arduino-cli config init --config-file ~/.arduinoCLI/arduino-cli.yaml` was never run
   and is **not needed** — the default config path resolves to `~/.arduino15` (where the cores are).
4. `~/.bashrc` → `env_active()` exports `IDF_TOOLS_PATH=/media/jang/home/Deve/zen-esp32/tools` but not
   `IDF_PATH`/`PATH`; `activate.sh` does both, so use `activate.sh` (`env_active`'s line is redundant
   but harmless — it does prevent an accidental `~/.espressif` re-download).
5. Rev 1 §4.2's `miniterm.py` is installed as `pyserial-miniterm`.
6. Rev 1 §4 P1's pinned `v5.3.2` was superseded by the v5.5.6 install; keep v5.5 (Arduino core 3.3.12
   is built against the 5.5 line, so Arduino and IDF configs stay aligned).
7. Rev 1 §4.10 (project location) is still unanswered — nothing was scaffolded; only `repro/firmware/`
   was added, as evidence for §10.3.
8. Rev 1 §1's `ARDUINO_USB_CDC_ON_BOOT=0` advice is inverted for the current wiring (§9.1).

---

## 15. Capability matrix (supersedes rev 1 §6)

| Capability | Status | How |
|---|---|---|
| Chip/flash/PSRAM/efuse/MAC probe | ✅ | `sg dialout -c 'esptool --chip esp32s3 --port $PORT flash-id'` (921600, stub OK) |
| Headless Arduino build | ✅ | `arduino-cli compile` (4 s incremental), 3 different configs verified |
| Headless flash | ✅ | `arduino-cli upload`, 1183 kbit/s, hash-verified ×3 |
| Program + observe firmware | ✅ | RGB colour cycle + heap/PSRAM log, user-confirmed visually |
| Monitor / scripted capture | ✅ | `repro/uart_tail.py` (no reset) / `repro/uart_probe.py` (resets); `arduino-cli monitor` no (B4) |
| Erase / raw bin write | ✅ | `esptool write-flash`/`erase-flash` (stub flasher verified) |
| ESP-IDF build for `esp32s3` | ✅ | `activate.sh` + `sdkconfig.defaults` (§11) |
| ESP-IDF flash/monitor | ✅ not exercised | `idf.py flash monitor` uses the same verified port/config path `[HYPOTHESIS]` |
| On-chip JTAG / GDB | ⚠️ blocked | needs USB node permission fix (B2) + GDB on PATH (B5) |
| Skill pinmap validation | ✅ with D1–D5 caveats | still false-negative on OPI PSRAM + GPIO22-25 + GPIO19/20 |
| Skill codegen | ❌ | D4 confirmed wrong for GPIO48 WS2812 |
| TinyUSB / MSC / DFU / native-USB-only sketches | ❌ untested | `USBMode=default` (TinyUSB) path never built |
| Wi-Fi / BLE on this board | ❌ untested | next milestone |

---

## 16. Next sequence

*(status after this session: 1 done, 5 done — see §17; 2, 3, 4, 6 still open)*

1. **B1 + B2**: ✅ B1 done by re-login (no more `sg` anywhere in this session); ❌ B2 still needs the
   udev rule from §12. The scripts in `repro/` no longer carry `sg` wrappers.
2. **Answer rev 1 §4.10** (in-repo vs `~/Arduino`) so `repro/firmware/*` can move into the project as
   `firmware/psram_smoke` and `firmware/rgb_blink`, plus a committed `arduino-cli` profile
   (`--build-property`-free: the FQBN string in §10.1 is already exact).
3. **Wi-Fi + BLE bring-up** as the next hardware milestone (nothing exercised yet; also the real test of
   the "quad-only flash + OPI PSRAM" coexistence under RF load).
4. **JTAG**: once B2/B5 are fixed, `. ./activate.sh && idf.py openocd` + GDB attach on `esp32s3.cpu0`,
   and confirm whether HWCDC (`CDCOnBoot=cdc`) leaves the JTAG interface claimable while the app runs.
5. **Patch skill D1→D5** with a regression fixture per fix (rev 1 §5 list is still accurate); D4's
   expected output should change to `rgbLedWrite` now that GPIO48 is known to be `PIN_RGB_LED`.
   ✅ done — §17 (D1-D7 plus one new defect, D8).
6. Optional: pin a second profile for the CH9102-only case (`CDCOnBoot=default`) so both cable
   positions have a known-good FQBN.

---

## 17. Rev 1 §7.4 executed: skill defects D1-D7 patched `[VERIFIED]`

Session on the restarted PC: `dialout` active, board on `/dev/ttyACM0` as `303a:1001`, no `sg` needed.

### 17.1 Hardware inspection (the input to the patch)

```bash
PORT=$(ls -1 /dev/serial/by-id/*Espressif* | head -1)
esptool  --chip esp32s3 --port $PORT chip-id
esptool  --chip esp32s3 --port $PORT flash-id
espefuse --chip esp32s3 --port $PORT summary
```

| Fact | Value | Source |
|---|---|---|
| Chip | ESP32-S3 QFN56 **rev v0.2**, dual LX7 + **LP core**, 240 MHz | `chip-id` |
| Crystal | **40 MHz** | `chip-id` |
| PSRAM | **8 MB, octal, `AP_3v3`, 85 °C** | `chip-id` + eFuse `PSRAM_CAP/VENDOR/TEMP` |
| Flash | **16 MB**, vendor `1c:7018` (XMC) | `flash-id` |
| eFuse `FLASH_TYPE` | **4 data lines (quad)** | `espefuse summary` |
| eFuse `VDD_SPI_*` | `FORCE=1`, `TIEH=VDD3P3_RTC_IO` → **3.3 V** | `espefuse summary` |
| eFuse `PIN_POWER_SELECTION` | `VDD_SPI`, described by Espressif as "default power supply for **GPIO33-GPIO37**" | `espefuse summary` |
| eFuse `DIS_USB_JTAG` | False → on-chip JTAG pad path is alive (B2 is purely a permissions issue) | `espefuse summary` |
| MAC | 68:ee:8f:5a:c4:44 | `chip-id` |

⇒ **module class = `N16R8`** (16 MB quad flash + 8 MB octal PSRAM). There is no eFuse holding the
module part number; the memory geometry above is what identifies it.

Cross-checked against the installed SDKs (not against memory):
`soc/esp32s3/include/soc/spi_pins.h` (MSPI pads GPIO26-32 + octal `D4..DQS` = **GPIO33-37**),
`register/soc/io_mux_reg.h` (**no pad for GPIO22-25**; `MTCK/MTDO/MTDI/MTMS` = 39/40/41/42,
`XTAL_32K_P/N` = 15/16, `U0TXD/U0RXD` = 43/44, `SPICLK_P/N` = 47/48),
`soc/adc_channel.h` (ADC1=1-10, ADC2=11-20), `soc/touch_sensor_channel.h` (1-14),
`soc/rtc_io_channel.h` (**RTC GPIOs = 0-21 only**), `soc/usb_pins.h` (`DM=19, DP=20` — same on S2),
`soc/soc_caps.h` (**`SOC_LEDC_CHANNEL_NUM 8`**, `SOC_RMT_TX_CANDIDATES_PER_GROUP 4`, 3 UARTs),
Arduino core 3.3.12 `variants/esp32s3/pins_arduino.h` (`PIN_RGB_LED 48`, `SDA 8/SCL 9`,
`MOSI 11/MISO 13/SCK 12`), `esp32-hal-rgb-led.h`, `HardwareSerial.{h,cpp}` (`Serial0` exists,
`begin(baud, config, rx, tx)`), `esp32-hal-ledc.c` (`LEDC_CHANNELS` is doubled only on the
original ESP32).

### 17.2 What was wrong and what it is now

| # | Defect | Fix |
|---|---|---|
| **D1** | OPI PSRAM reservation missing → GPIO33-37 sold as free | `scripts/platforms/modules.py` decodes the module ordering code; `_PSRAM_OCTAL_PINS` + module-aware `_effective_psram_pins()` → **error** `PSRAM_PIN` naming SPIIO4-7/SPIDQS. `N*R8`/`N*R16` → reserved; `N*R2` (quad) → not reserved; no-PSRAM module → free. Also emit `CONFIG_SPIRAM_MODE_OCT` / `PSRAM=opi` hints in `generate_config`. |
| **D2** | S3 pin DB was a stub; GPIO22-25 "free"; 41/49 pins had no functions | `_NONEXISTENT_PINS` (S2/S3 = 22-25) → those numbers are **absent from the DB**, `get_pin()` raises "does not exist", validate emits `NON_EXISTENT_PIN`. 45 pads for S3, 43 for S2. `_get_alternate_functions()` now merges ADC/touch/RTC/USB/JTAG/MSPI/GPSPI-octal/strap/pad-name → **every S3 pad has fixed functions**. |
| **D3** | `"module": "WROOM-18N Octal PSRAM"` aborted with no JSON | New module vocabulary: legacy `WROOM/WROVER`, real codes (`N16R8`, `ESP32-S3-WROOM-1-N16R8`, `MINI-1-N4`, `SOLO-1`, …), plus a new **`"psram": {"mode": "octal|quad|none", "size_mb": n}`** override that wins over the string. Unknown strings → `MODULE_UNKNOWN` warning, validation continues, JSON always emitted (exit 0/1, never 2 for a bad module). |
| **D4** | WS2812 → `ledcAttach()`; UART pins dropped; I2C addr `0x00` | WS2812-family pins are categorised separately (`_is_ws2812`, APA102/SPI excluded) → Arduino `rgbLedWrite(RGB_BUILTIN, …)` for GPIO48 (core's pseudo-pin) / `rgbLedWrite(pin, …)` elsewhere, ESP-IDF `rmt_new_tx_channel` + `rmt_new_bytes_encoder` with real WS2812 timings and `esp_check.h`. UART: per-port grouping, right object (`Serial0/1/2`), declared pads, console-aware baud, warning for missing TX/RX, note that UART2 has no default pads. I2C addresses from `common-devices.md` (SSD1306 → 0x3C …), placeholder is now labelled. |
| **D5** | GPIO19/20 (native USB) produced no warning | `_USB_PINS` → `USB_PIN` warning on S2 **and** S3, message names the collateral damage (UART1 RTS/CTS, ADC2_CH8/9); pin DB marks `USB` + `USB_NATIVE`. |
| **D6** | 50 tests blind to all of the above | 50 → **96 tests** (`tests/test_module_and_pinmap_defects.py`, `tests/test_code_fidelity.py`) + 7 new fixtures; all 96 pass, the 50 pre-existing ones unchanged. |
| **D7** | `specs.md` omissions | `references/esp32-s3/specs.md` rewritten with sourced facts (GPIO holes, octal pins, LP core, VDD_SPI/3.3 V interaction, 40 vs 26 MHz, USB pads, LEDC=8, RMT=4, RTC=0-21, GPIO48/WS2812 note). New `references/esp32-s3/modules.md`: ordering-code table, reserved-pin table per module class, the §17.1 commands with this board's real output as the worked example, and the `zen-esp32` bring-up profile (FQBN + IDF sdkconfig + the two USB-mux traps). `esp32-pins.md` and `esp32-specifics.md` got the same facts; `SKILL.md` gained the guardrails, the schema/`psram` field, the error contract and the error-code list. |
| **D8 (new)** | `_FLASH_PINS_ESP32C6` = {24,25,26,**27**,28,**29**} | IDF `spi_pins.h` for C6: CS0=24, MISO=25, WP=26, HD=28, CLK=29, MOSI=**30**. Old table had one false positive (GPIO27, a free pin) *and* one false negative (GPIO30, real flash pad). Fixed + regression fixture. Also fixed: LEDC channel budget for S2/S3 was 16, is **8** (Arduino core `LEDC_CHANNELS` doubles only where `SOC_LEDC_SUPPORT_HS_MODE`). |

### 17.3 Verification

```
$ (cd esp32-skills && python3 -m pytest -q tests/)          → 96 passed
$ (cd repro && ./run.sh)                                    → ALL CASES PASS, exit 0
```

`repro/run.sh` is no longer a print-out: it asserts every case (new `repro/check.py` helper, which
also *fails on non-JSON output*, i.e. it keeps D3's original symptom under test). Inputs updated:
`d1_*` now declares the real module code; added `d1b_*` (psram override) and `d8_c6_flash_pins_gpio30.json`.

**Hardware-in-the-loop proof of D4** (the codegen claim is now executed, not argued):

```bash
PORT=$(ls -1 /dev/serial/by-id/*Espressif* | head -1)
python3 - <<'PY'   # generate Arduino code straight from the skill
import sys, json; sys.path.insert(0, "esp32-skills/scripts")
from platforms import get_platform
d = json.load(open("repro/inputs/baseline_s3_devkit.json"))
open("/tmp/gen_check/gen_check.ino", "w").write(
    get_platform("esp32", "esp32s3", d["module"]).generate_code(d, "arduino"))
PY
arduino-cli compile --fqbn "$FQ" --build-path /tmp/gen_check/build /tmp/gen_check
# → Sketch uses 339608 bytes (10%), Global variables 24376 bytes (7%)   [compiles for the real target]
arduino-cli upload --input-dir /tmp/gen_check/build --fqbn "$FQ" --port $PORT   # Hash of data verified
python3 repro/uart_probe.py $PORT 115200 4
# → mode:DIO, clock div:1 … / GPIO initialized
```
Board then re-flashed with `repro/firmware/rgb_blink` and confirmed cycling RED/GREEN/BLUE/WHITE
(§9.1 evidence state restored). The generated code is DIO-headed, which independently confirms the
`FlashMode=dio,PSRAM=opi` library variant still holds for skill-generated builds.

### 17.4 Skill files touched

```
scripts/platforms/modules.py                    NEW  — ordering-code decoder (ModuleSpec)
scripts/platforms/__init__.py                   rewritten factory: never aborts on a module string
scripts/platforms/base.py                       ModuleSpec plumbing + 4 new ConflictType codes
scripts/platforms/esp32.py                      data tables, validate(), Arduino + ESP-IDF codegen
scripts/validate_pinmap.py, scripts/generate_config.py   always-JSON error envelopes, psram field
references/esp32-s3/specs.md                    D7 rewrite
references/esp32-s3/modules.md                  NEW  — module codes + identification recipe
references/platforms/esp32-pins.md              holes, octal pins, USB pads, LEDC table, C6 flash pins
references/platforms/esp32-specifics.md         octal PSRAM + input-only per variant
skills/esp32/SKILL.md, README.md                guardrails, schema, error contract, code list
tests/{test_module_and_pinmap_defects,test_code_fidelity}.py + 7 fixtures        D6
repro/{run.sh,check.py,README.md,inputs/*}      assertions instead of print-outs
```

`plugin.json` and the `SKILL.md` frontmatter bumped 1.0.0 → 1.1.0 (behaviour change: new
input field, new error codes, different codegen).

