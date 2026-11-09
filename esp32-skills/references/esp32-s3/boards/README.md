# ESP32-S3 board profiles — which S3 is in front of you

`specs.md` and `modules.md` describe the **silicon and the module**. They cannot tell
you what a *board* does with the pads: which pads a vendor routes to a connector, which
are shared between a socket and the header, where the LED/console/USB sit. Two boards on
the same module can disagree about all of that, and the disagreement is what costs an
afternoon.

**Identify the module class first (it decides the reserved pins), then the board (it
decides the rest).** A board name never tells you the PSRAM bus width — read it.

## 1. Profiles

| Profile | Module | Console | Distinguishing hardware |
|---|---|---|---|
| [`zen-esp32-rig.md`](zen-esp32-rig.md) | ESP32-S3-WROOM-1 **N16R8** | one USB-C that alternates between CH9102 and native USB | WS2812 on GPIO48, no connector-level extras |
| [`yuanxinwei-esp32s3-cam-sd.md`](yuanxinwei-esp32s3-cam-sd.md) | ESP32-S3-WROOM-1 **N16R8** | **two** USB-C: one CH9102, one native USB | 24-pin DVP camera FPC, microSD slot, BOOT+EN buttons, 2 unpopulated 20-pin headers |

Both are `N16R8`: **16 MB quad flash + 8 MB octal PSRAM**, so on *both* boards GPIO26-32
are flash and GPIO33-37 are PSRAM. Same reservations, completely different boards — which
is the reason the two files exist separately. And the reservation is *not* a guarantee of
empty header pins: the `yuanxinwei` board breaks out **GPIO35/36/37 = SPIIO5-7** on its header
and silkscreens them `✱ PSRAM`, so on that board a reserved pad is a connector pin you can
short out — see its profile §3.

## 2. Telling them apart without a datasheet

```bash
# from the plugin repo: one shot, everything below is captured to repro/probe-*.txt
repro/board_probe.sh                 # or: repro/board_probe.sh /dev/ttyUSB0
```

| Observable | Command | Reads as |
|---|---|---|
| **Which port is which board** | `ls -l /dev/serial/by-id/` | the **CH9102 bridge serial**: `5C82085540` → this folder's `yuanxinwei-esp32s3-cam-sd.md`, `5D01162178` → `zen-esp32-rig.md`. Burned in the bridge, so it survives any reflash and costs no board reset to read — the only handle that is safe to script. Never `ttyACMx` (letters move), never the MAC (module identity, needs a bootloader reset to read, and app-owned CDC replaces the MAC-derived USB iSerial) |
|---|---|---|
| USB identity, app running | `lsusb` | `303a:1001` = USB-Serial-JTAG (nothing is claiming USB). `303a:4001` + `iSerial 123456` = an app-owned TinyUSB CDC stack — the ROM is then **not** reachable over that port |
| USB identity, bridge side | `lsusb` | `1a86:55d3` = CH9102 on UART0 (GPIO43/44). Flashing works through it with `--before default-reset` |
| Memory class | `esptool chip-id`, `espefuse summary` | `Embedded PSRAM 8MB (AP_3v3)` + `FLASH_TYPE = 4 data lines` ⇒ `N*R8` ⇒ GPIO33-37 reserved |
| Console liveness | `repro/uart_tail.py <port> 3` | ROM log present ⇒ UART0 wired; silence ⇒ wrong socket or app-owned USB |
| Board extras | eyes / silk | camera FPC, SD slot, LED, buttons |

If `esptool` says `No serial data received` on a port that is clearly the chip's own USB,
the running firmware owns USB (TinyUSB): use the board's UART side, or hold BOOT and tap
RST. See `yuanxinwei-esp32s3-cam-sd.md` §"two sockets, two identities".

## 3. Measuring a board that has no published pin map

`repro/firmware/board_sonde/` is the tool. It is a sketch, not a library: it answers
"what is wired where" from the chip side, which is the only source that cannot be a
typo in a product page. Stages, each printable on the UART console:

| Stage | Measures | What it is good for |
|---|---|---|
| S0 | chip, flash, PSRAM at runtime | confirms the memory class the app actually sees |
| S1 | every user pad read floating / with internal pull-up / with internal pull-down | locates board-level pull resistors: I²C buses, strap resistors, an SD slot's nets when a card is in |
| S2 | bit-bang I²C over **every ordered pad pair**, 0x08-0x77, then again with a pixel clock rotating over every pad | finds a camera on SCCB without knowing the pins; also finds any other device sharing the bus |
| S3 | sensor ID registers over the discovered pair | separates OV2640 (8-bit reg, `0x0A`→`0x90`) from OV5640 (16-bit reg, `0x300A/0x300B`→`0x56/0x40`) |
| S4 | µSD in 1-bit SPI: `(CLK,CS,DI)` enumerated, CMD0 reply read on every remaining pad at once | finds the slot without a schematic, then proves it by reading block 0 |

Two knobs and why they exist:

* `PROBE_I2C` — the SCCB passes need a camera **fitted**; without one they are 4 minutes
  of guaranteed silence, so they are compiled out by default.
* `PROBE_OPI_PADS` — survey and drive GPIO33-37. **Only** in a build made with
  `PSRAM=disabled`: the stage refuses to do it while `psramFound()` is true. This is the
  only way to answer "did this board route the octal-PSRAM pads to a connector", which is
  otherwise invisible from software and is the most expensive mistake a cheap S3 board can
  make.

Rules the sonde exists to respect, all of them learned the hard way:

1. Never `pinMode()` GPIO26-32 (or 33-37 with PSRAM live). Re-parenting an MSPI pad to
   the GPIO matrix detaches the peripheral → the next cache miss hangs → `TG1WDT_SYS_RST`.
   A "harmless read" of a flash pad is enough to crash the probe.
2. A µSD card's own 50 kΩ pull-ups mark CMD/DAT1-3 but **not CLK**, so "enumerate the
   pulled-up pads" cannot find a slot. Enumerate the clock, read the reply everywhere.
3. A camera does not answer SCCB without XCLK; and XCLK is **not** an LEDC job on this
   chip (see `specs.md` §3).
4. Releasing the pads before you exit is not optional: a probe left driving IO0 breaks
   the next `esptool` auto-reset, and the next flash attempt fails in a way that looks
   like a bad cable.

## 4. Adding a profile

Fill the same table the existing profiles use, one row per property, and put the command
or measurement in the **Evidence** column — a board profile without evidence is a rumour.
Mark anything you have not measured as `not measured`, and say which stage of the sonde
would settle it.
