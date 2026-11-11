"""
Code-generation fidelity tests (defect D4), plus the RMT/LEDC resource rules.

The rule these tests protect: generated code must compile *and* drive the real
device. A WS2812 driven by LEDC compiles and does nothing at all, which is the
worst kind of generated code.

Facts asserted here are from the installed toolchain:
* Arduino core 3.3.12, `esp32-hal-rgb-led.h` -> `rgbLedWrite(pin, r, g, b)`;
  `neopixelWrite()` is deprecated.
* `variants/esp32s3/pins_arduino.h` -> `PIN_RGB_LED 48`,
  `RGB_BUILTIN = SOC_GPIO_PIN_COUNT + 48` (a pseudo-pin, not GPIO 48).
* `HardwareSerial::begin(unsigned long, uint32_t, int8_t rx, int8_t tx, ...)` and
  `HardwareSerial Serial0(0)` in `HardwareSerial.cpp`.
* ESP-IDF v5.5.6 `driver/rmt_tx.h` / `driver/rmt_encoder.h` (bytes encoder).
"""
import sys
from pathlib import Path

import pytest

SCRIPTS_DIR = Path(__file__).parent.parent / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from platforms import get_platform  # noqa: E402
from helpers import validate_cpp_braces  # noqa: E402


@pytest.fixture
def s3_fixture(load_fixture):
    return load_fixture("d4_esp32s3_rgb_led.json")


@pytest.fixture
def s3_platform():
    return get_platform("esp32", "esp32s3", "N16R8")


class TestWs2812IsNotPwm:
    """D4a: addressable strips go to RMT/rgbLedWrite, never to LEDC."""

    def test_arduino_uses_rgbledwrite(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "arduino")
        assert "rgbLedWrite(RGB_BUILTIN" in code
        assert "rgbLedWrite(NEOPIXELSTRIP_PIN" in code

    def test_arduino_never_uses_ledc_for_the_strip(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "arduino")
        for line in code.splitlines():
            if "ledcAttach" in line:
                assert "FAN" in line, f"strip driven by LEDC: {line}"

    def test_deprecated_api_not_used(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "arduino")
        assert "neopixelWrite" not in code

    def test_espidf_uses_rmt(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "espidf")
        assert '#include "driver/rmt_tx.h"' in code
        assert "rmt_new_tx_channel" in code
        assert "rmt_new_bytes_encoder" in code
        assert ".gpio_num = NEOPIXEL_PIN" in code
        assert "ledc_channel_config" in code  # the fan still uses LEDC
        assert validate_cpp_braces(code)

    def test_validation_warns_about_pwm_described_strip(self, s3_fixture, s3_platform):
        result = s3_platform.validate(s3_fixture)
        hits = sorted(w["gpio"] for w in result.warnings if w["code"] == "WS2812_TIMING")
        assert hits == [14, 48]
        onboard = [w["message"] for w in result.warnings
                   if w["code"] == "WS2812_TIMING" and w["gpio"] == 48][0]
        assert "PIN_RGB_LED" in onboard

    def test_rmt_channel_budget(self):
        """S3 has 4 RMT TX channels; the 5th strip must be reported."""
        platform = get_platform("esp32", "esp32s3", "N16R8")
        assignment = {
            "variant": "esp32s3", "module": "N16R8",
            "pins": [{"gpio": g, "function": f"WS2812_{i}", "protocol_bus": "pwm"}
                     for i, g in enumerate((48, 14, 15, 16, 17))],
        }
        result = platform.validate(assignment)
        budgets = [w for w in result.warnings if "RMT TX" in w["message"]]
        assert len(budgets) == 1
        assert "4 RMT TX channels" in budgets[0]["message"]


class TestLedcChannelBudget:
    """S2/S3 have 8 LEDC channels, not the original ESP32's 16."""

    @pytest.mark.parametrize("variant,expected", [
        ("esp32", 16), ("esp32s2", 8), ("esp32s3", 8), ("esp32c3", 6), ("esp32c6", 6),
    ])
    def test_channel_counts(self, variant, expected):
        platform = get_platform("esp32", variant, "WROOM")
        assert platform._get_ledc_channels() == expected

    def test_over_budget_warns(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        pins = [{"gpio": g, "function": f"PWM{g}", "protocol_bus": "pwm"}
                for g in range(1, 11)]  # GPIO1-10, all output-capable
        result = platform.validate({"variant": "esp32s3", "module": "WROOM", "pins": pins})
        assert any("max 8 LEDC channels" in w["message"] for w in result.warnings)


class TestUartCodegen:
    """D4b: the declared UART port and pads must survive into the code."""

    def test_arduino_keeps_pins_and_port(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "arduino")
        assert "Serial0.begin(115200, SERIAL_8N1, -1, CONSOLE_UART0_TX_PIN);" in code
        assert "Serial1.begin(9600)" not in code

    def test_arduino_two_ports(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        assignment = {
            "variant": "esp32s3", "module": "WROOM",
            "pins": [
                {"gpio": 17, "function": "UART1_TX", "protocol_bus": "uart", "speed_hz": 115200},
                {"gpio": 18, "function": "UART1_RX", "protocol_bus": "uart"},
                {"gpio": 8, "function": "UART2_TX", "protocol_bus": "uart"},
                {"gpio": 4, "function": "UART2_RX", "protocol_bus": "uart"},
            ],
        }
        code = platform.generate_code(assignment, "arduino")
        assert "Serial1.begin(115200, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);" in code
        assert "Serial2.begin(" in code

    def test_espidf_per_port_functions(self, s3_fixture, s3_platform):
        code = s3_platform.generate_code(s3_fixture, "espidf")
        assert "uart0_init" in code
        assert "UART_NUM_0" in code
        assert "UART_NUM_1" not in code


class TestI2cAddresses:
    """D4c: no more 0x00 placeholders for devices the skill documents."""

    def test_ssd1306_default_address(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        assignment = {
            "variant": "esp32s3", "module": "WROOM",
            "pins": [
                {"gpio": 21, "function": "I2C_SDA", "protocol_bus": "i2c", "device": "SSD1306",
                 "pull": "external_up"},
                {"gpio": 47, "function": "I2C_SCL", "protocol_bus": "i2c", "device": "SSD1306",
                 "pull": "external_up"},
            ],
        }
        code = platform.generate_code(assignment, "arduino")
        assert "SSD1306_ADDR = 0x3C" in code
        assert "0x00" not in code
        espidf = platform.generate_code(assignment, "espidf")
        assert "0x3C" in espidf

    def test_undocumented_device_keeps_placeholder_comment(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        assignment = {
            "variant": "esp32s3", "module": "WROOM",
            "pins": [
                {"gpio": 21, "function": "I2C_SDA", "protocol_bus": "i2c", "device": "AcmeX99",
                 "pull": "external_up"},
                {"gpio": 47, "function": "I2C_SCL", "protocol_bus": "i2c", "device": "AcmeX99",
                 "pull": "external_up"},
            ],
        }
        code = platform.generate_code(assignment, "arduino")
        assert "PLACEHOLDER" in code


class TestModuleAwareConfigNotes:
    """generate_config() must surface the memory geometry it decoded."""

    def test_octal_psram_config_lines(self, s3_platform):
        assignment = {
            "variant": "esp32s3", "module": "ESP32-S3-WROOM-1-N16R8",
            "pins": [{"gpio": 4, "function": "BUTTON", "protocol_bus": "gpio",
                      "direction": "input", "pull": "up"}],
        }
        result = s3_platform.generate_config(assignment)
        blob = "\n".join(result.config_lines)
        assert "CONFIG_SPIRAM_MODE_OCT=y" in blob
        assert 'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"' in blob
        assert "PSRAM=opi" in blob
        assert any("GPIO33-37" in n for n in result.wiring_notes)
