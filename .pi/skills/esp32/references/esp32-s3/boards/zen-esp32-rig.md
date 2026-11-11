# Board profile: `zen-esp32` bring-up rig (verified end-to-end)

Silicon and module facts live in `../specs.md` and `../modules.md`; this file is only
about what this particular board does with them.

| Property | Value | Evidence |
|---|---|---|
| Module | **ESP32-S3-WROOM-1 N16R8** — 16 MB quad flash + 8 MB octal PSRAM ⇒ GPIO26-32 flash, GPIO33-37 PSRAM | `espefuse summary`, `flash-id` |
| Chip | ESP32-S3 QFN56 v0.2, LP core, 40 MHz crystal | `esptool chip-id` |
| Memory | 16 MB flash (XMC 0x1c/0x7018) + 8 MB OPI PSRAM, 3.3 V | `flash-id`, `espefuse summary` |
| Console | single USB-C carrying **either** CH9102 (GPIO43/44) **or** native USB, alternating | hotplug log |
| **Console node (the key)** | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5D01162178-if00` — CH9102 SN **`5D01162178`** | `ls -l /dev/serial/by-id/`, MAC cross-checked, 2026-11-09 |
| Base MAC | `68:ee:8f:5a:c4:44` — identity of *this module*, read once; **not** a port handle (an app-owned CDC stack replaces the MAC-derived USB iSerial) | `chip-id`, 2026-11-09 |
| USB identity while running | `303a:1001` USB-Serial-JTAG when the native-USB side owns the link; **steady `1a86:55d3` + UART0 ROM banner when cabled to the bridge side** — the node did not flip across 6 resets | `lsusb`, kernel log, 2026-11-09 |
| Auto-reset | works over the same connector once the app is not holding USB | `esptool --before default-reset` |
| On-board LED | WS2812 on **GPIO48** = `PIN_RGB_LED`; cycle RED→GREEN→BLUE→WHITE→OFF at 150 ms confirmed | `rgb_blink` + user |
| Connectors | none beyond the header — no camera, no card slot | visual |
| Working FQBN | `esp32:esp32:esp32s3:FlashMode=dio,FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadMode=default,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600` | build+flash+monitor in one session |
| Working IDF config | `CONFIG_ESPTOOLPY_FLASHMODE_DIO`, `FLASHSIZE_16MB`, `SPIRAM=y`, `SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M` | `idf.py build` clean |

Two S3-specific traps that cost a whole debugging session:

1. **`CDCOnBoot=cdc` keeps the port identity stable.** With `CDCOnBoot=default` the
   connector switches between the CH9102 (`1a86:55d3`) and the ESP32-S3 (`303a:1001`) as
   the app state changes, so `/dev/tty*` appears to change role under the user. With `cdc`
   it is the same device across flashing and running.
2. **Pin the port by path, not by letter.** `/dev/serial/by-path/…` (or by-id) — the
   USB-Serial-JTAG node disappears for a moment at every mode flip, and `/dev/ttyACM0` is
   re-used by the other bridge.

## Re-measure log

| Date | Observation |
|---|---|
| 2026-11-09 | Cabled socket (hub port `1-10`, CH9102 SN `5D01162178`) presented **only** the bridge: no `303a:*` device appeared before or after `esptool --before default-reset` / `--after hard-reset`. UART0 gave the ROM banner, then silence ⇒ the flashed app prints nothing there. `esptool read-flash 0x10000` strings: core 3.3.12 / IDF v5.5.5, `PartitionScheme=app3M_fat9M_16MB`, FQBN as in the table above. **`[UNCERTAIN]`** one USB-C or two — if a native-USB socket exists it was not cabled this session. |

## Pin placement notes

* GPIO33-37 are octal PSRAM here — see `../modules.md` §2. Nothing on this board routes
  them anywhere, so "the validator says reserved" and "there is no pin" agree.
* GPIO19/20 are the chip's own USB. `CDCOnBoot=cdc` and USB-Serial-JTAG debugging both
  want them; treat them as reserved unless you have a second path to the console.
* Silence on the console is **not** evidence of a dead node on this board: the flashed app
  prints nothing. Run `python3 repro/hardreset.py $PORT` and look for the ROM banner.
* The RGB LED is on GPIO48, which is also the pad the Arduino core documents as
  marginal for external WS2812 strips — a strip on the header belongs on GPIO38
  (`../specs.md` §4).
