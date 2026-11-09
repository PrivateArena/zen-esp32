# Board profile: YuanXinWei (元信微科技) "ESP32-S3 单独开发板" — camera FPC + microSD

A generic AliExpress/Taobao ESP32-S3 board: WROOM-1-class module, a 24-pin camera FPC in
the middle of the PCB, a microSD slot at the far edge, two USB-C sockets, EN + BOOT
buttons, and two unpopulated 20-pin headers.

Silicon/module facts: `../specs.md`, `../modules.md`. This file is what only *this*
board does.

**Status: the connector pin maps are NOT measured.** Everything else below was. The
camera half needs a flex module fitted; the microSD half currently cannot be measured
because the slot presents nothing to the SoC — see §4 before trusting any pin table you
find for a "similar" board.

## 1. Identity

| Property | Value | Evidence |
|---|---|---|
| Chip | ESP32-S3 QFN56 **v0.2**, dual core + LP core, **40 MHz** crystal, features 0x12 | `esptool chip-id` |
| Flash | **16 MB, quad** (`FLASH_TYPE = 4 data lines`), JEDEC mfr `0x68` device `0x4018` | `flash-id` |
| PSRAM | **8 MB octal (OPI)**, `PSRAM_VENDOR=AP_3v3`, `PSRAM_TEMP=85C` | `espefuse summary` |
| Flash voltage | 3.3 V forced by eFuse (`VDD_SPI_FORCE=1`, `VDD_SPI_TIEH=VDD3P3_RTC_IO`) | `espefuse summary` |
| Module class | ⇒ **`N16R8`**: GPIO26-32 flash, **GPIO33-37 = PSRAM, not user pins** | derived from the two rows above |
| MAC | `e0:72:a1:d4:e4:80` | `chip-id` |
| Console | **CH9102** (`1a86:55d3`, "USB Single Serial", SN `5C82085540`) on **UART0, GPIO43/44** | hotplug log, ROM banner seen on that node |
| Auto-reset | **works** — `esptool --before default-reset` enters the bootloader with no button press | observed |
| Buttons | `EN/RST` and `BOOT`/`IO0` present, but not needed for normal flashing | visual + auto-reset above |
| Buttons' LEDs | silkscreen `ON`, `TX`, `RX`, `IO2`, `48` beside the USB sockets | photo |
| Factory firmware | ESP-IDF **`v4.4-dev-2594-ga20df743f1-dirty`**, source `main/led_strip_tusb_console.c`, single 1 MB `factory` partition, `nvs` + `phy_init` | `esptool read-flash` + `strings` of the image |
| Factory USB identity | `303a:4001` "Espressif Device", `iSerial 123456`, 2-interface CDC — an app-owned **TinyUSB** stack, so the ROM was **not** reachable over that socket | `lsusb -v`, `esptool` failure |
| Factory image backed up | `repro/backup-new-s3-app.bin`, md5 `32faac5e14b1037b7385903742da2b35` (0x000000-0x110000) | `esptool read-flash` |

## 2. Two USB-C sockets, two identities — read this first

The single most confusing thing about this board: the two sockets are **not**
interchangeable, and which one you are on decides whether you can flash at all.

| Socket | Carries | Shows up as | Use it for |
|---|---|---|---|
| UART side | CH9102 → GPIO43/44 | `1a86:55d3` | flashing, console, ROM logs — **use this one** |
| USB side | native USB → GPIO19/20 | `303a:1001` with USB-Serial-JTAG firmware, `303a:4001` with the shipped TinyUSB demo | CDC console only, and *only if the running app hands the port back* |

The shipped demo holds USB itself, so on the USB side `esptool` answers
`No serial data received` — the ROM's USB-Serial-JTAG reset handshake does not exist on a
CDC device. Move the cable to the UART side, or hold BOOT and tap RST.

**Address the port by `/dev/serial/by-id/…`, never by letter.** Verified on this board:
when the USB link dropped, the bridge came back as **`/dev/ttyACM1`** with the *same*
by-id name — a script holding `/dev/ttyACM0` silently talked to nothing.

## 3. What the pads are actually doing (measured)

From `repro/firmware/board_sonde/` (majority-of-9 sampling; a single read of a floating
pad on this board is not a measurement — see §5):

| Pad(s) | Reading | Reading |
|---|---|---|
| 0 | weak pull-up | boot strap resistor, as expected |
| 4, 5 | strong-high (beats the 45 kΩ internal pull-down ⇒ something ≤ ~10 kΩ) | the only pair with real bus-style pull-ups on the board: **prime SCCB candidates for the camera FPC**, direction not established |
| 33, 35, 36, 37 | clean, no pull | the octal-PSRAM pads carry no board net (see §4) |
| 34 | **marginal** — drifts high and low with charge, no correlation with the card | do not build anything on this pad's "state" |
| 38, 39, 40 | strong-high | JTAG pads with the S3's default pull configuration, not board resistors |
| 20 | high only while the native-USB socket is attached | the chip's own USB D+ pull-up |
| everything else (1-3, 6-21, 38-48) | floating / driven low | nothing attached |

* **No I²C device answers on any pad pair** — 700+ pairs swept over 0x08-0x77 with and
  without a measured ~15 MHz pixel clock. That is the expected result for an **empty
  camera FPC**, and it also proves there is no IMU, expander or touch controller hiding on
  a shared bus.
* **GPIO33-37 carry no connector net.** Measured in a `PSRAM=disabled` build, which is the
  only legitimate way to look at them: with OPI PSRAM live, re-parenting those pads is the
  crash described in `../specs.md` §4. This board therefore does *not* make the classic
  "octal PSRAM pad routed to a header/connector" mistake.

### Header pin labels (photo, not verified pad-by-pad)

Top edge: `5V 14 13 12 11 10 9 46 3 8 18 17 16 15 7 6 5 4 EN 3V3`
Bottom edge: `GND 19 20 21 47 48 45 0 … 39 40 41 42 1 2 RX TX`

The labels behind the FPC connector were not legible in the photo. **Check them on the
real board**: if they read `33…37`, then the header claims pads that this module uses for
octal PSRAM — which is not a bug in the board alone, it is a bug in any design that
touches them.

## 4. microSD slot — measured negative, do not guess

With a card inserted, the slot could not be found from software:

* exhaustive **39,270-tuple** sweep of `(CLK, CS, DI)` over all 35 usable pads (GPIO33-37
  included, PSRAM disabled), CMD0 reply read on every remaining pad simultaneously:
  **0 replies**.
* live pad monitor across three card insert/remove cycles: **no pad changed state and
  held**.

So the slot is either unpowered, not making contact, or not wired to this SoC at all.
None of these are distinguishable from the chip side. Before writing any SD code for this
board, settle it physically: confirm the card type and orientation against the silkscreen,
try a second card, and check the slot's VDD pin for 3.3 V. **Do not copy a pin map from a
"similar" ESP32-S3 camera board** — the camera FPC on those designs and this one are not
the same PCB.

Once the slot answers, stage S4 of the sonde proves the net map by reading block 0 and the
partition table, not by a single ACK.

## 5. Build / flash recipes that work on this board

```bash
# normal image (octal PSRAM usable)
arduino-cli compile --upload --port /dev/serial/by-id/usb-1a86_USB_Single_Serial_5C82085540-if00 \
  --fqbn 'esp32:esp32:esp32s3:FlashMode=dio,FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadMode=default,PartitionScheme=default,UploadSpeed=921600' \
  repro/firmware/board_sonde

# OPI-pad probing build: PSRAM must be OFF or the probe is lying (or crashing)
... :PSRAM=disabled,...
```

`psramFound()` in stage S0 is the check that the build you are running matches the build
you think you flashed.

Notes specific to writing probes for this board:

1. `pinMode()` on GPIO26-32 → XIP hang → `rst:0x8 (TG1WDT_SYS_RST)`. Reproduced here twice.
2. A bit-bang XCLK generator must be pinned to the core the Arduino `loopTask` does *not*
   own, and the task watchdog must tolerate the starved IDLE task, or the probe aborts
   mid-scan (`task_wdt: IDLE0 … Aborting`).
3. Release every pad you touched before returning: a probe still holding IO0 makes the
   next `esptool` auto-reset fail with `No serial data received`, which looks like a cable
   fault.
4. Read pads with a majority of samples. Two "discoveries" on this board (GPIO34 as an SD
   net, GPIO3 as a pulled-up net) evaporated under repeated sampling.

## 6. To be measured

| Question | Which stage settles it |
|---|---|
| Camera SCCB SDA/SCL + which sensor | S2/S3 with a flex module fitted (`PROBE_I2C 1`) |
| XCLK / PCLK / HREF / VSYNC / D0-D7 | S3 for the sensor type, then drive a test pattern and edge-count the DVP pads |
| Which pads the microSD slot uses | S4 once the slot is confirmed powered and contacted |
| What the four illegible header labels say | eyes, then a continuity check |
| Whether GPIO4/5 really are the SCCB pair | S2 pass A with a camera fitted: the pull-ups are already on the right pads |
