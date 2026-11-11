# Board profile: YuanXinWei (元信微科技) "ESP32-S3 单独开发板" — camera FPC + microSD

Generic AliExpress/Taobao ESP32-S3 board: WROOM-class module, 24-pin camera FPC, microSD slot,
two USB-C sockets, EN + BOOT buttons, two 20-pin headers (unpopulated, pin strips in the box).
PCB code `127294G_Y127_231016` (back, designed 2023-10-16). Vendor photos of both faces:
`yuanxinwei-esp32s3-cam-sd-images/`.

Silicon and module facts live in `../specs.md` and `../modules.md`; this file is only what this
board does with them.

## 1. Identity

| Property | Value | Evidence |
|---|---|---|
| Module | `ESP32-S3-WROOM-1` class, **N16R8** — 16 MB quad flash + 8 MB octal PSRAM ⇒ GPIO26-32 flash, GPIO33-37 PSRAM | shield marking not legible; `espefuse summary` + `flash-id` |
| Chip | ESP32-S3 QFN56 **v0.2**, `PKG_VERSION=0`, dual core + LP core, **40 MHz** crystal, features `0x12` | `chip-id`, `espefuse summary` |
| Flash | 16 MB quad, mfr `0x68` device `0x4018` | `flash-id` |
| PSRAM | 8 MB OPI, `PSRAM_VENDOR=AP_3v3`, `PSRAM_TEMP=85C` | `espefuse summary` |
| Flash/IO supply | 3.3 V, forced by eFuse (`VDD_SPI_FORCE=1`, `VDD_SPI_TIEH=VDD3P3_RTC_IO`, `PIN_POWER_SELECTION=VDD_SPI`) | `espefuse summary` |
| Base MAC | `e0:72:a1:d4:e4:80` — identity of *this module*, read once to cross-check the bridge SN; **not** a port handle | `chip-id`, 2026-11-09 |
| Console | **CH9102** (`1a86:55d3`) on UART0 GPIO43/44, bridge SN **`5C82085540`** → `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5C82085540-if00` | hotplug log, ROM banner, 2026-11-09 |
| Auto-reset | works over the UART socket, no button press needed | `esptool --before default-reset` |
| Buttons | `EN/RST` and `BOOT/IO0`, silkscreened | photo |
| Indicator LEDs | silkscreen `ON`, `TX`, `RX`, `IO2`, `48` beside the USB sockets — the last two name the pads they sit on, not yet driven | photo |
| PCB code | `127294G_Y127_231016` | back photo |

## 2. Two USB-C sockets, labelled on the back

| Socket (silk) | Carries | Host sees | Use for |
|---|---|---|---|
| `USB-UART` | CH9102 → GPIO43/44 | `1a86:55d3` | flashing, console, ROM logs — **the working socket** |
| `USB-OTG` | native USB → GPIO19/20 | `303a:1001` with USB-Serial-JTAG, `303a:4001` with the shipped TinyUSB demo | CDC only; an app that owns USB makes the ROM unreachable there |

The shipped demo showed `303a:4001`, `iSerial 123456`, 2-interface CDC: a hardcoded iSerial, so a
USB-side node name identifies nothing. Factory image (ESP-IDF `v4.4-dev-2594-ga20df743f1-dirty`,
`main/led_strip_tusb_console.c`) is saved as `repro/backup-new-s3-app.bin`; flash it back with
`esptool --port $PORT write-flash 0x0 repro/backup-new-s3-app.bin`.

## 3. Header pin map — read from the vendor photo, left→right

```
top     5V 14 13 12 11 10  9 46  3  8 18 17 16 15  7  6  5  4 EN 3V3
bottom GND 19 20 21 47 48 45  0 35 36 37 38 39 40 41 42  2  1 RX TX
                                    ✱  ✱  ✱  ~  ~  ~
```

Back-side legend: **`→ Camera` · `~ SD Card` · `✱ PSRAM`**.

| Marked pads | Mark | Hardware meaning |
|---|---|---|
| GPIO35, 36, 37 | `✱` PSRAM | **the vendor breaks out three of the five octal-PSRAM pads** (SPIIO5-7) on the header |
| GPIO38, 39, 40 | `~` SD | three of the four microSD nets (measured board pull-ups on exactly these three — the fourth, a clock, is the one that never has a pull-up) |
| none | `→` | camera nets exist only on the 24-pin FPC; no header pad carries the camera mark |

**GPIO35/36/37 are the hazard of this board.** On an `N16R8` module those pads *are* the PSRAM
bus: any design that uses those header pins must be built with PSRAM off, and on this module
`pinMode()` on them while PSRAM is live detaches MSPI → `TG1WDT_SYS_RST`. Note that GPIO33 and 34
are **not** broken out, so "the header gives me 35-37" is a vendor choice, not a spare-pins gift.

A pad survey cannot see this: an unpopulated header is just a trace to a pad, so the S1 survey
correctly reported "no board net" on 35/37 and the conclusion "no connector net" drawn from it was
wrong. Only the silkscreen or a continuity check shows it.

Other header facts: GPIO19/20 = the native-USB socket, GPIO0/45 = boot straps, GPIO43/44 = the
console (not on the header), GPIO46 is input-only, GPIO38-42 are the JTAG group, GPIO48 is the
Arduino `PIN_RGB_LED`.

## 4. Camera (24-pin FPC)

* SCCB candidates **GPIO4/GPIO5**: the only pad pair with real bus-style pull-ups on this board
  (strong-high in the pad survey). Direction not established.
* No I²C device answered any pad pair, with and without a pixel clock — the expected result for an
  **empty** FPC, and it rules out an IMU/expander elsewhere on a bus.
* XCLK/PCLK/HREF/VSYNC/D0-D7: unmeasured, needs a camera module fitted (`PROBE_I2C 1`, stages S2/S3
  in `repro/firmware/board_sonde/`).

## 5. microSD slot

* Three nets known from silk: **GPIO38, 39, 40**, each measured strong-high ⇒ board-level pull-ups
  are present. The fourth net (probably CLK) is unmarked and unknown.
* With a card verified good on a PC reader: push-pull SPI CMD0 over every role permutation of
  `{38, 39, 40, X}`, X over all user pads incl. GPIO33-37 in a `PSRAM=disabled` build —
  **0 replies** (`repro/firmware/sd_net_find`, 2026-11-09). Same verdict as the earlier open-drain
  sweep, reached a different way.
* So the slot is unpowered, not making contact, or not wired to this SoC — not distinguishable
  from software. Settle it physically, in order: meter VDD (µSD pin 4) → VSS (pin 6) for 3.3 V;
  look for unpopulated series/0 Ω parts between slot and module; re-seat against the silkscreen and
  try a second card. µSD pinout, contacts facing you: `1 DAT2 · 2 DAT3/CS · 3 CMD · 4 VDD · 5 CLK ·
  6 VSS · 7 DAT0 · 8 DAT1`.
* Do not copy a pin map from a "similar" ESP32-S3 camera board: the FPC footprint here is not the
  one those designs use.

## 6. Still unmeasured

| Question | What settles it |
|---|---|
| 4th SD net + does the slot answer | meter the slot VDD, then `repro/firmware/sd_net_find` |
| Camera pin map + sensor type | camera fitted, board_sonde S2/S3 |
| LED pads (does `IO2` mean GPIO2, is `48` a WS2812?) | `repro/firmware/led_find` — watch the phases |
| Whether the two rows really are 20 pads each with those labels on the physical board | eyes on the board, one pass |
