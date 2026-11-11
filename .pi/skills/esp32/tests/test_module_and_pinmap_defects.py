"""
Regression tests for the ESP32-S3 module/pin-database defects (D1, D2, D3, D5, D8).

Each test corresponds to a defect reported in the environment review
(repro/run.sh reproduces the same cases from the CLI side):

D1  Octal (OPI) PSRAM on ESP32-S3 reserves GPIO33-37 — was a silent false negative.
D2  GPIO22-25 have no pad on ESP32-S2/S3 — the numbers used to validate as free.
D3  Real module ordering codes (N16R8, WROOM-1, …) aborted with no JSON at all.
D5  GPIO19/20 are the native USB pads on S2/S3 — no warning was produced.
D8  ESP32-C6 flash pins are GPIO24-26+28-30; the table claimed 27 was flash and
    missed 30 — one false positive and one false negative at the same time.
"""
import sys
from pathlib import Path

import pytest

SCRIPTS_DIR = Path(__file__).parent.parent / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from platforms import get_platform          # noqa: E402
from platforms.modules import parse_module  # noqa: E402


# ===================================================================
# D1 — octal PSRAM reservation
# ===================================================================

class TestOctalPsramPins:
    """GPIO33-37 belong to the PSRAM on N*R8 / N*R16 modules."""

    def test_octal_module_reserves_gpio33_37(self, load_fixture, helpers):
        assignment = load_fixture("d1_esp32s3_octal_psram.json")
        platform = get_platform("esp32", "esp32s3", assignment["module"])
        result = platform.validate(assignment)
        helpers.assert_invalid(result)
        bad = sorted(e["gpio"] for e in result.errors if e["code"] == "PSRAM_PIN")
        assert bad == [33, 34, 35, 36, 37]

    def test_error_message_names_the_signal(self, load_fixture):
        assignment = load_fixture("d1_esp32s3_octal_psram.json")
        platform = get_platform("esp32", "esp32s3", assignment["module"])
        result = platform.validate(assignment)
        messages = " ".join(e["message"] for e in result.errors)
        assert "SPIIO4" in messages and "SPIDQS" in messages

    def test_explicit_psram_field_also_reserves(self, load_fixture, helpers):
        assignment = load_fixture("d1b_esp32s3_psram_override.json")
        platform = get_platform("esp32", "esp32s3", assignment["module"],
                                assignment.get("psram"))
        result = platform.validate(assignment)
        helpers.assert_invalid(result)
        assert [e["code"] for e in result.errors] == ["PSRAM_PIN"]

    def test_quad_module_leaves_gpio33_37_free(self):
        """N8R2 (quad PSRAM) shares only the flash bus - GPIO33-37 are user pins."""
        platform = get_platform("esp32", "esp32s3", "ESP32-S3-WROOM-1-N8R2")
        for gpio in range(33, 38):
            assert platform.get_pin(gpio).usability == "free"

    def test_no_psram_module_leaves_gpio33_37_free(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        for gpio in range(33, 38):
            assert platform.get_pin(gpio).usability == "free"

    def test_classic_esp32_wrover_still_reserves_16_17(self):
        platform = get_platform("esp32", "esp32", "WROVER")
        for gpio in (16, 17):
            assert platform.get_pin(gpio).usability == "reserved"


# ===================================================================
# D2 — GPIO numbers that do not exist
# ===================================================================

class TestNonExistentPins:
    """GPIO22-25 are not pins on ESP32-S2/S3."""

    @pytest.mark.parametrize("variant", ["esp32s3", "esp32s2"])
    def test_gpio_absent_from_database(self, variant):
        platform = get_platform("esp32", variant, "WROOM")
        for gpio in (22, 23, 24, 25):
            assert gpio not in [p.gpio_num for p in platform.get_all_pins()]
        with pytest.raises(ValueError, match="does not exist"):
            platform.get_pin(22)

    def test_pin_count_is_pad_count_not_max_number(self):
        assert len(get_platform("esp32", "esp32s3", "WROOM").get_all_pins()) == 45
        assert len(get_platform("esp32", "esp32s2", "WROOM").get_all_pins()) == 43

    def test_validate_reports_nonexistent(self, load_fixture, helpers):
        assignment = load_fixture("d2_esp32s3_nonexistent_pins.json")
        result = get_platform("esp32", "esp32s3", "WROOM").validate(assignment)
        helpers.assert_invalid(result)
        assert sorted(e["gpio"] for e in result.errors) == [22, 23, 24, 25]
        assert {e["code"] for e in result.errors} == {"NON_EXISTENT_PIN"}


# ===================================================================
# D3 — module vocabulary
# ===================================================================

class TestModuleParsing:
    """Module strings must decode, or warn — never abort."""

    @pytest.mark.parametrize("code,flash,psram_mb,bus", [
        ("ESP32-S3-WROOM-1-N16R8", 16, 8, "octal"),
        ("N16R8", 16, 8, "octal"),
        ("ESP32-S3-WROOM-1-N8R2", 8, 2, "quad"),
        ("N8", 8, 0, "none"),
        ("ESP32-S3-MINI-1-N4", 4, 0, "none"),
    ])
    def test_ordering_codes(self, code, flash, psram_mb, bus):
        spec = parse_module("esp32s3", code)
        assert spec.flash_size_mb == flash
        assert (spec.psram_size_mb or 0) == psram_mb
        assert spec.psram_bus == bus
        assert not spec.unknown

    def test_legacy_aliases_still_work(self):
        assert parse_module("esp32", "wroom-32").canonical == "WROOM"
        assert parse_module("esp32", "WROVER").canonical == "WROVER"

    def test_unknown_module_warns_instead_of_raising(self):
        spec = parse_module("esp32s3", "Some Vendor Board v1.2")
        assert spec.unknown
        assert spec.warnings
        assert spec.psram_bus == "none"

    def test_get_platform_never_raises_for_module(self):
        platform = get_platform("esp32", "esp32s3", "Board rev C with 8MB flash")
        assert platform.module in ("WROOM", "WROVER")

    def test_psram_override_wins(self):
        spec = parse_module("esp32s3", "WROOM", {"mode": "octal", "size_mb": 8})
        assert spec.psram_bus == "octal"
        assert spec.canonical == "WROVER"

    def test_psram_override_can_disable(self):
        spec = parse_module("esp32", "WROVER", {"mode": "none"})
        assert spec.psram_bus == "none"
        assert spec.canonical == "WROOM"

    def test_prose_module_still_validates(self, load_fixture, helpers):
        """D3: prose in the module field must produce findings, not an abort."""
        assignment = load_fixture("d3_esp32s3_module_prose.json")
        platform = get_platform("esp32", "esp32s3", assignment["module"])
        result = platform.validate(assignment)
        codes = [w["code"] for w in result.warnings]
        assert "MODULE_UNKNOWN" in codes
        # …and the octal wording really does reserve GPIO33-37 (here: 37).
        assert "PSRAM_PIN" in [e["code"] for e in result.errors]


# ===================================================================
# D5 — native USB pads
# ===================================================================

class TestNativeUsbPins:
    """GPIO19/20 are USB D-/D+ on ESP32-S2 and ESP32-S3."""

    @pytest.mark.parametrize("variant", ["esp32s3", "esp32s2"])
    def test_usb_warning(self, variant, load_fixture, helpers):
        assignment = load_fixture("d5_esp32s3_usb_pins.json")
        result = get_platform("esp32", variant, "WROOM").validate(assignment)
        helpers.assert_valid(result)  # warning, not error
        hit = sorted(w["gpio"] for w in result.warnings if w["code"] == "USB_PIN")
        assert hit == [19, 20]

    def test_usb_pads_marked_in_pin_database(self):
        pin = get_platform("esp32", "esp32s3", "WROOM").get_pin(19)
        assert "USB" in pin.alternate_functions
        assert "USB_NATIVE" in pin.conflicts


# ===================================================================
# D8 — ESP32-C6 flash pin table
# ===================================================================

class TestEsp32C6FlashPins:

    def test_gpio30_is_flash_and_gpio27_is_free(self, load_fixture, helpers):
        assignment = load_fixture("d8_esp32c6_flash_pins.json")
        platform = get_platform("esp32", "esp32c6", "WROOM")
        result = platform.validate(assignment)
        helpers.assert_invalid(result)
        flash_hits = sorted(e["gpio"] for e in result.errors if e["code"] == "FLASH_PIN")
        assert flash_hits == [30], "MSPI pads on C6 are 24,25,26,28,29,30"
        assert not [e for e in result.errors if e["gpio"] == 27]
        assert platform.get_pin(27).usability == "free"


# ===================================================================
# Pin database content (D2 "8 of 49 pins have alternate functions")
# ===================================================================

class TestPinDatabaseCoverage:

    def test_every_s3_pad_has_a_fixed_function_note(self):
        platform = get_platform("esp32", "esp32s3", "N16R8")
        missing = [p.gpio_num for p in platform.get_all_pins()
                   if not p.alternate_functions]
        # GPIO21 and GPIO47/48 have no ADC/touch/USB/MSPI role on the S3.
        assert missing == [], f"unexpected gaps: {missing}"
        # (every S3 pad has at least an ADC/touch/RTC/MSPI/strap role documented)

    def test_adc_map_matches_idf(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        assert platform.get_pin(1).adc_channel == "ADC1_CH0"
        assert platform.get_pin(10).adc_channel == "ADC1_CH9"
        assert platform.get_pin(11).adc_channel == "ADC2_CH0"
        assert platform.get_pin(20).adc_channel == "ADC2_CH9"
        assert platform.get_pin(38).adc_channel is None  # GPIO26-48: no ADC

    def test_rtc_domain_is_gpio0_21_only(self):
        platform = get_platform("esp32", "esp32s3", "WROOM")
        assert "RTC" in platform.get_pin(21).alternate_functions
        assert "RTC" not in platform.get_pin(26).alternate_functions
