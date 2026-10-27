# ESP32-S3 Module Codes — what the ordering number means for your GPIO map

The module part number is the *only* written-down source for "does this board
have octal PSRAM", and octal PSRAM decides whether five GPIOs exist for the user.
Never infer it from the development-board name.

## 1. Naming scheme

```
ESP32-S3 - WROOM-1 - N16R8
   chip      family    ordering code
```

| Token | Meaning |
|---|---|
| `WROOM-1` / `WROOM-2` | PCB-antenna module (`-2` = different antenna/layout revision) |
| `MINI-1` | small module, IPEX/U.FL antenna options |
| `SOLO-1` | single-core (one CPU fused off) |
| `N<nn>` | **nn = flash in MBytes** (N8 = 8 MB, N16 = 16 MB) |
| `R<n>` | **n = PSRAM in MBytes**: R2 = 2 MB quad, R8 = 8 MB octal, R16 = 16 MB octal |
| no `R` | no PSRAM |

Rule of thumb, and the rule the validator implements: **R2/R4 → quad (QSPI),
R8/R16 → octal (OPI)**. Sizes that do not decode (e.g. an `R112`-style suffix)
are reported and treated as quad, which is the bus that reserves the *fewest* pins.

## 2. What each option reserves on ESP32-S3

| Module class | Flash pads (never usable) | PSRAM pads (never usable) | User GPIOs |
|---|---|---|---|
| any S3 | 26, 27, 28, 29, 30, 31, 32 | — | 45 − 7 = 38 minus strapping/input-only |
| `N*R2` / `N*R4` (quad PSRAM) | 26-32 | shares the flash bus | same as above |
| `N*R8` / `N*R16` (**octal PSRAM**) | 26-32 | **33, 34, 35, 36, 37** | 33 |

GPIO33-37 are `SPIIO4/5/6/7` + `SPIDQS` (`IDF: components/soc/esp32s3/include/soc/spi_pins.h`,
`MSPI_IOMUX_PIN_NUM_D4…DQS`). The eFuse that powers them is described by Espressif's
own tool as "Set default power supply for GPIO33-GPIO37" (`espefuse: PIN_POWER_SELECTION`).

Two board-design consequences that people hit repeatedly:

* A 480×480 **RGB (parallel) panel** wants 16 data lines; on an `N*R8` module five
  of the usual candidates are gone — plan the bus on GPIO38-48 + the low group.
* **Octal PSRAM + Wi-Fi** is the worst case for power integrity: both are on
  VDD_SPI. Keep PSRAM and RF traces short, decouple at the module, and if the board
  is unstable only under RF load, look at the supply, not the code.

## 3. Identify the module from software (no datasheet, no photo)

```bash
PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_68:EE:8F:5A:C4:44-if00
esptool --chip esp32s3 --port $PORT chip-id      # chip, revision, PSRAM class, crystal, MAC
esptool --chip esp32s3 --port $PORT flash-id    # flash size + vendor id
espefuse --chip esp32s3 --port $PORT summary    # PSRAM_CAP / PSRAM_VENDOR / FLASH_TYPE / VDD_SPI
```

Real output of the three commands on the board this reference was written for:

```
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                68:ee:8f:5a:c4:44

Manufacturer: 1c     Device: 7018     Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set in eFuse: 3.3V          (VDD_SPI_FORCE=1, VDD_SPI_TIEH=VDD3P3_RTC_IO)

PSRAM_CAP    = 8M        PSRAM_VENDOR = AP_3v3      PSRAM_TEMP = 85C
PIN_POWER_SELECTION = VDD_SPI     DIS_USB_JTAG = False
```

Reading: 16 MB quad flash + 8 MB octal PSRAM at 3.3 V ⇒ **an `N16R8` class
module** ⇒ GPIO33-37 are PSRAM, GPIO26-32 are flash, USB is the chip's own.

There is no eFuse that stores the module part number; the memory geometry above
is what identifies the class. If you need the exact part, read the shield can.

## 4. Feeding this to the skill

Either the real ordering code, or the explicit memory description — both are
understood and produce identical reserved-pin sets:

```json
{"variant": "esp32s3", "module": "ESP32-S3-WROOM-1-N16R8", "pins": [ … ]}
{"variant": "esp32s3", "module": "WROOM", "psram": {"mode": "octal", "size_mb": 8}, "pins": [ … ]}
```

A prose module string ("WROOM-18N Octal PSRAM", a vendor name, a board rev) does
not abort validation any more: it produces a `MODULE_UNKNOWN` warning, the JSON
result is still emitted, and the reserved-pin decision is reported so a human can
confirm it against `espefuse`.

## 5. Board profile: `zen-esp32` bring-up rig (verified end-to-end)

| Property | Value | Evidence |
|---|---|---|
| Chip | ESP32-S3 QFN56 v0.2, LP core, 40 MHz crystal | `esptool chip-id` |
| Memory | 16 MB flash (XMC 0x1c/0x7018) + 8 MB OPI PSRAM, 3.3 V | `flash-id`, `espefuse summary` |
| Console | single USB-C carrying **either** CH9102 (GPIO43/44) **or** native USB, alternating | hotplug log |
| USB identity while running | `303a:1001` USB-Serial-JTAG (stable across reflashing) | `lsusb` |
| On-board LED | WS2812 on **GPIO48** = `PIN_RGB_LED`; cycle RED→GREEN→BLUE→WHITE→OFF at 150 ms confirmed | `rgb_blink` + user |
| Working FQBN | `esp32:esp32:esp32s3:FlashMode=dio,FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadMode=default,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600` | build+flash+monitor in one session |
| Working IDF config | `CONFIG_ESPTOOLPY_FLASHMODE_DIO`, `FLASHSIZE_16MB`, `SPIRAM=y`, `SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M` | `idf.py build` clean |

Two S3-specific traps that cost a whole debugging session:

1. **`CDCOnBoot=cdc` keeps the port identity stable.** With `CDCOnBoot=default`
   the connector switches between the CH9102 (`1a86:55d3`) and the ESP32-S3
   (`303a:1001`) as the app state changes, so `/dev/tty*` appears to change role
   under the user. With `cdc` it is the same device across flashing and running.
2. **Pin the port by path, not by letter.** `/dev/serial/by-path/…` (or by-id) —
   the USB-Serial-JTAG node disappears for a moment at every mode flip, and
   `/dev/ttyACM0` is re-used by the other bridge.
