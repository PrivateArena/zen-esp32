# AGENTS.md — zen-esp32 ESP32-S3 bench

Two physical ESP32-S3 boards hang off this host. Every command here was executed on it; if you
change one, re-run it and fix this file.

| Path | What it is |
|---|---|
| `esp32-skills/` | the deliverable — `skills/esp32/SKILL.md` + `references/` |
| `esp32-skills/references/esp32-s3/boards/` | **one profile per physical board. Read it before touching a board.** |
| `esp32-skills/scripts/` | `validate_pinmap.py` (JSON pinmap → errors), `generate_config.py --framework arduino\|espidf` (validated JSON → code) |
| `repro/` | board kit (`board_probe.sh`, `uart_tail.py`, `hardreset.py`, `firmware/board_sonde`) + `run.sh` skill-regression kit |
| `ENV-REVIEW*.md` | dated session logs — evidence and history, not the spec |
| `idf/`, `tools/` | portable ESP-IDF v5.5.6 (gitignored) |

## Ports: the CH9102 bridge serial is the only key

```bash
ls -l /dev/serial/by-id/
# usb-1a86_USB_Single_Serial_5C82085540-if00 -> ttyACM0   cam+SD board  (yuanxinwei-esp32s3-cam-sd.md)
# usb-1a86_USB_Single_Serial_5D01162178-if00 -> ttyACM1   zen rig       (zen-esp32-rig.md)
PORT=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5C82085540-if00
```

The serial is burned in the **bridge**, not the module: it survives any reflash and firmware, and
reading it costs nothing. `ttyACMx` letters swap between boards and sessions. **MACs are not keys** —
they identify the module, reading one means resetting the board into the bootloader, and an
app-owned CDC stack replaces the MAC-derived USB iSerial. They live as dated identity rows in the
profiles, for cross-checking a SN against a module once.

## Commands (verified 2026-11-09)

```bash
esptool  --chip auto    --port $PORT --before default-reset --after hard-reset chip-id     # identity
espefuse --chip esp32s3 --port $PORT --before default-reset --after hard-reset summary    # flash/PSRAM class
arduino-cli compile --fqbn "$FQ" --build-path build <sketch>                              # 4 s incremental
arduino-cli compile --upload --port $PORT --fqbn "$FQ" <sketch>                           # ok = "Hash of data verified"
python3 repro/uart_tail.py $PORT 5      # console capture, NO reset (pure termios)
python3 repro/uart_probe.py  $PORT 115200 5   # same, but it DOES reset the target
python3 repro/hardreset.py $PORT        # pulse EN only -> restart app. Both EN+IO0 = download mode
repro/board_probe.sh $PORT              # one-shot profile evidence -> repro/probe-*.txt; leaves the
                                        # board in the bootloader, so hardreset.py afterwards
repro/run.sh                            # skill kit, no board -> "ALL CASES PASS"
python3 -m pytest esp32-skills/tests -q # 96 tests
. ./activate.sh && idf.py --version     # ESP-IDF v5.5.6; idf.py is not on PATH until this
```

FQBN that builds, flashes and boots on **both** boards:

```
esp32:esp32:esp32s3:FlashMode=dio,FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadMode=default,PartitionScheme=default,UploadSpeed=921600
```

`PartitionScheme=app3M_fat9M_16MB` when the app needs storage; `PSRAM=disabled` only for a probe
that must touch GPIO33-37. Check `psramFound()` in your own log: it is what proves the image you
flashed is the image you built.

## Rules that each already cost a session

1. `pinMode()` on GPIO26-32 (flash) or GPIO33-37 (octal PSRAM — both boards are `N16R8`) detaches
   MSPI → next cache miss → `rst:0x8 (TG1WDT_SYS_RST)`. A "harmless read" is enough.
2. Silence is not a dead port: the flashed app may print nothing, or own USB. `hardreset.py` and
   look for `ESP-ROM:esp32s3-`. `303a:4001` in `lsusb` = an app owns the CDC stack → the ROM is not
   reachable on that socket; use the CH9102 side or hold BOOT + tap EN.
3. A probe talking through the CH9102 socket must print on **`Serial0` (GPIO43/44)** — with
   `CDCOnBoot=cdc` the plain `Serial` goes out of the native-USB socket and is invisible there.
4. Release every pad a probe touched (one left on IO0 makes the next auto-reset fail like a bad
   cable) and read pads as a majority of samples — two "discoveries" here evaporated on re-read.
5. WS2812 is RMT, not PWM: `rgbLedWrite()` (`neopixelWrite()` is deprecated in core 3.3.12);
   on-board strip = `PIN_RGB_LED` GPIO48, pseudo-pin `RGB_BUILTIN` (97).
6. ESP-IDF: `-D CONFIG_*` on the `idf.py` command line is silently ignored — use
   `sdkconfig.defaults` and `rm sdkconfig`.
7. A generated `repro/probe-*.txt` is evidence, not source: keep the one backing a profile,
   delete the rest.

## Loop

Identify the port → open that board's profile → measure what it marks *not measured* with
`repro/board_probe.sh` / `firmware/board_sonde` → write the result back **into the profile**, with
the command that produced it, and say `not measured` rather than guessing from a "similar" board.
Software cannot prove a rail is unpowered: when a connector presents nothing to the SoC, report the
physical check that settles it (cam+SD board profile §4.1 — microSD VDD pin 4 vs VSS pin 6) instead
of re-running a sweep that can only return zero.
