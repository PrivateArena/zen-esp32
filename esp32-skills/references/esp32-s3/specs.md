# ESP32-S3 Specifications

Facts below are marked with where they can be re-checked. `IDF:` = ESP-IDF
`components/soc/esp32s3/…` header, `core:` = Arduino ESP32 core
`variants/esp32s3/pins_arduino.h`, `fuse:` = `espefuse summary`, `trm:` = ESP32-S3
Technical Reference Manual.

## 1. Core Architecture
- **CPU:** Dual-core Xtensa® LX7, 240 MHz (`trm`).
- **LP core:** one low-power RISC-V core (38.5 MHz, GPIO/RTC/UTC access) plus the
  Ultra-Low-Coprocessor (RTC fast memory 8 KB). `IDF: SOC_LP_CORE` / `soc_caps.h`.
  The LP core is what makes GPIO0-21 (the "HP+LP" pads) useful in deep sleep.
- **Wireless:** 802.11 b/g/n (Wi-Fi 4), BLE 5.0 (Mesh support).
- **Acceleration:** AI Vector instructions for accelerated ML and complex GUI rendering.
- **Application:** High-performance IoT, AI on the edge, and multimedia GUIs.

## 2. Memory & Storage
- **SRAM:** 512 KB internal (+ 16 KB LP SRAM + 8 KB eFuse + 40 KB ROM).
- **PSRAM:** off-chip, on the MSPI bus. Two electrically different flavours:

| PSRAM | Bus | Module codes | GPIOs claimed beyond the flash bus |
|---|---|---|---|
| 1/2/4 MB | quad (QSPI) | `N*R2`, `N*R4` | none (shares GPIO26-32) |
| 8/16 MB | **octal (OPI)** | `N*R8`, `N*R16` | **GPIO33, 34, 35, 36, 37** |

- **Octal PSRAM pins:** `IDF: spi_pins.h` → `MSPI_IOMUX_PIN_NUM_D4..D7,DQS =
  GPIO33..37` (TRM names SPIIO4-7 + SPIDQS). `fuse: PIN_POWER_SELECTION` is
  literally documented as "Set default power supply for GPIO33-GPIO37".
  On an N*R8 / N*R16 module these five pins are **not user pins at all** — using
  them (RGB LCD data, SPI display, buttons…) corrupts PSRAM access or browns the
  PSRAM out; it is a silent failure, not a boot failure.
- **Flash vs PSRAM voltage:** both are fed from VDD_SPI. On a 3.3 V flash module
  `fuse: VDD_SPI_FORCE=1, VDD_SPI_TIEH=VDD3P3_RTC_IO` → "Flash voltage (VDD_SPI)
  set to 3.3V by efuse". Octal PSRAM parts are marked `AP_3v3` for the same
  reason. Do **not** try to "improve" battery life by switching VDD_SPI to 1.8 V
  on such a module: PSRAM is not 1.8 V tolerant.
- **Flash mode:** quad-flash modules (eFuse `FLASH_TYPE = 4 data lines`) work with
  `QIO` or `DIO`; `DIO` is the conservative choice and is what the Arduino
  `FlashMode=dio` + `PSRAM=opi` library variant (`esp32s3-libs/<ver>/dio_opi`) is built for.
  Always verify the produced image: `esptool --port <port> image-info build/app.bin`
  → `Flash mode: DIO`, `Flash size: 16MB`.

## 3. Peripheral Mapping
- **GPIO Count:** **45 pads — GPIO0-21 and GPIO26-48. GPIO22, 23, 24, 25 DO NOT
  EXIST** (`IDF: io_mux_reg.h` has no pad between `GPIO21_U (0x58)` and
  `SPICS1_U (0x6c)`). `SOC_GPIO_PIN_COUNT` is 49 and is a *max number + 1*, not a
  count of pads — never size an array by it and iterate.
- **ADC:** 20 channels. ADC1 = GPIO1-10 (CH0-9), ADC2 = GPIO11-20 (CH0-9)
  (`IDF: adc_channel.h`). Only GPIO0-21 can be ADC pins: **GPIO26-48 have no ADC**.
- **DAC:** **None.** (Pads GPIO17/18 are historically named `DAC_1`/`DAC_2`; they
  are ordinary GPIO + UART1 TX/RX on this chip.)
- **Touch:** 14 GPIO touch channels, GPIO1-14 (`IDF: touch_sensor_channel.h`);
  `SOC_TOUCH_SENSOR_NUM = 15` counts the external channel 0.
- **RTC / deep-sleep-capable GPIOs:** GPIO0-21 only (`IDF: rtc_io_channel.h`).
  GPIO26-48 cannot be EXT0/EXT1 wake sources.
- **USB:** native USB OTG **D- = GPIO19, D+ = GPIO20** (`IDF: usb_pins.h`), shared
  with USB-Serial-JTAG. Those two pads are *also* UART1 RTS/CTS and ADC2_CH8/9 —
  see §4 before touching them. USB-Serial-JTAG can be flashed, monitored and
  debugged without any external bridge, and its JTAG interface is claimable while
  the app runs (`fuse: DIS_USB_JTAG=0` unless burnt).
- **JTAG (classic 4-wire):** MTCK=GPIO39, MTDO=GPIO40, MTDI=GPIO41, MTMS=GPIO42.
- **Hardware PWM (LEDC):** **8 channels** (`IDF: SOC_LEDC_CHANNEL_NUM`, and the
  Arduino core `LEDC_CHANNELS` only doubles it on the original ESP32), 4 timers.
  Not 16 — the original ESP32 number is not inherited.
- **RMT:** 1 group, **4 TX + 4 RX channels** → at most 4 independent WS2812-family
  data lines.
- **LCD/Camera:** supports RGB, 8080, and I80 interfaces. Note that a 16-bit RGB
  panel needs ~10 data lines; on an octal-PSRAM module GPIO33-37 are unavailable,
  so RGB panels on N*R8 modules use GPIO38-48 + the low group.
- **UARTs:** 3. UART0 default pads TX=GPIO43, RX=GPIO44. UART1 defaults
  TX=17, RX=18, RTS=19, CTS=20. **UART2 has no default pads** — both pins must be
  assigned (`IDF: uart_pins.h`).
- **SPI:** SPI0/SPI1 are the MSPI flash/PSRAM bus (not available to the user);
  GPSPI = SPI2 only (plus SPI3 on some builds). IOMUX defaults: MOSI=11, SCLK=12,
  MISO=13, CS=10 (`core:` MOSI/MISO/SCK/SS).
- **I2C:** 2 masters; `core:` variant defaults SDA=GPIO8, SCL=GPIO9.

## 4. Hardware Safety & Constraints
- **Non-existent pins:** GPIO22-25 (error, not warning — there is nothing to
  configure).
- **Flash pins:** GPIO26-32 (`IDF: MSPI_*`), never usable as GPIO.
- **Octal PSRAM:** GPIO33-37 on `N*R8`/`N*R16` modules — see §2.
- **ADC2/WiFi conflict:** ADC2 readings are invalid when Wi-Fi is active
  (GPIO11-20). GPIO19/20 are additionally the USB pads.
- **Strapping pins:** 0 (Boot), 3 (JTAG select), 45 (VDD_SPI voltage),
  46 (ROM print). GPIO0/45/46 have external resistors on almost every dev board;
  GPIO3 selects pad-JTAG vs USB-JTAG at reset.
- **Input-only:** GPIO46 only (it is also a strapping pin).
- **Native USB:** using GPIO19/20 as GPIO kills flashing-over-USB, the CDC console
  and USB-Serial-JTAG debugging in the same stroke, and only after the app starts
  — the bootloader still enumerates. Treat them as reserved on any board whose
  connector is the ESP32-S3's own USB.
- **Board-level gotcha (Arduino core comment):** on some boards the drive voltage
  on **GPIO48** (WS2812 data) is marginal; the core suggests moving the strip to a
  3V3-referenced pin such as 38 — `core: variants/esp32s3/pins_arduino.h`.
- **On-board WS2812:** `core: PIN_RGB_LED = 48`, and `LED_BUILTIN`/`RGB_BUILTIN`
  are the pseudo-pin `SOC_GPIO_PIN_COUNT + 48 = 97`. `rgbLedWrite(RGB_BUILTIN, r, g, b)`
  is the supported API (`neopixelWrite()` is deprecated in core 3.3.x); LEDC/PWM
  cannot generate WS2812 timing at all.

## 5. Crystal / clock variants
- Modules ship with a **40 MHz** main crystal (this chip: `esptool chip-id` →
  `Crystal frequency: 40 MHz`); some boards use 26 MHz. The eFuse/ROM choose the
  divisor from the detected value, but ESP-IDF needs `CONFIG_XTAL_FREQ` to match —
  a 40 MHz part configured for 26 MHz ends up with wrong baud rates and flash
  timings. Read the chip, do not guess: `esptool --port <port> chip-id`.
- The 32 kHz low-power crystal, when fitted, sits on GPIO15 (XTAL_32K_P) and
  GPIO16 (XTAL_32K_N) — using either as a normal GPIO forbids `RTC_CLK_SRC_EXT_XTAL`.

## 6. See also
- `modules.md` (next to this file) — how to decode `N16R8`-style module codes and
  a worked identification of a real board.
