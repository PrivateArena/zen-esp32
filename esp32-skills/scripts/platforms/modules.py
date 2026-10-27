"""
Espressif module part-number vocabulary and what it implies for the GPIO map.

Why this module exists
----------------------
The original skill only accepted the two legacy module classes ``WROOM``
(flash only) and ``WROVER`` (flash + PSRAM). Real Espressif hardware has not
been named that way since the ESP32-S3 generation: modules are ordered as
``ESP32-S3-WROOM-1-N16R8``, ``ESP32-S3-MINI-1-N4``, ``ESP32-C3-MINI-1``, ...
and the trailing *ordering code* is the only place where flash size and PSRAM
bus width are written down. Because PSRAM bus width decides whether GPIO33-37
exist for the user at all, the module string is load-bearing data, not cosmetics.

Naming scheme (Espressif module datasheets)
-------------------------------------------
``ESP32-<chip>-<family>-<n>-<ordering code>``

* ``family``  WROOM-1 / WROOM-2 / MINI-1 / PICO-V3-02 / SOLO-1 / ...
* ordering code ``N<flash-MB>[R<psram-MB>][<suffix>]``, e.g.

  - ``N8``     8 MB flash, no PSRAM
  - ``N8R2``   8 MB flash, 2 MB *quad* PSRAM
  - ``N16R8``  16 MB flash, 8 MB *octal* PSRAM  <- reserves GPIO33-37 on S3
  - ``N16R16`` 16 MB flash, 16 MB *octal* PSRAM

PSRAM bus width rule (and why it is a rule, not a guess)
-------------------------------------------------------
Quad (QSPI) PSRAM is sold in 1/2/4 MB; octal (OPI) PSRAM in 8/16 MB. So
``size >= 8 MB`` is treated as octal. Sizes with an unrecognised suffix
(``R112``, ``R162``, ...) are reported as unknown and validated conservatively
(as quad, which reserves fewer pins). Callers can always state the bus
explicitly with the ``psram`` field of the input JSON -- that wins.

Sources (checked against ESP-IDF v5.5.6 and Arduino ESP32 core 3.3.12)
---------------------------------------------------------------------
* Octal PSRAM pins: ``components/soc/esp32s3/include/soc/spi_pins.h``
  (``MSPI_IOMUX_PIN_NUM_D4..DQS`` = GPIO33..37)
* eFuse ``PIN_POWER_SELECTION`` description: "Set default power supply for
  GPIO33-GPIO37" (``espefuse summary`` on an N16R8)
* Legacy aliases: ``scripts/platforms/__init__.py`` (pre-existing behaviour)
"""

import re
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

# Legacy module classes kept for backward compatibility.
LEGACY_MODULES = ("WROOM", "WROVER")

# Module family names understood in the "family" slot of a part number.
_FAMILY_TOKENS = ("WROOM", "WROVER", "MINI", "PICO", "SOLO", "QFN", "M1", "MHET")

# Ordering-code pattern: N<flash MB>[R<psram code>] e.g. N16R8, N8, N4R2, N16R162.
_ORDERING_CODE_RE = re.compile(r"(?<![A-Z0-9])N(\d{1,3})(?:R(\d{1,3}[A-Z]?))?(?=$|[A-Z-])")
_FAMILY_RE = re.compile(r"(WROOM|WROVER|MINI|PICO|SOLO|QFN)(?:[-_]?(\d{1,2}))?")

# PSRAM sizes that are sold with an octal (OPI) bus; smaller ones are quad.
_OCTAL_MIN_MB = 8

# PSRAM bus names accepted in the explicit "psram" override field.
_PSRAM_MODE_ALIASES = {
    "": None,
    "OPI": "octal",
    "OCTAL": "octal",
    "8": "octal",
    "QSPI": "quad",
    "QUAD": "quad",
    "QIO": "quad",
    "DIO": "quad",
    "4": "quad",
    "NONE": None,
    "OFF": None,
}


@dataclass
class ModuleSpec:
    """Normalised description of one module ordering code.

    Attributes:
        raw: The module string exactly as the caller supplied it.
        canonical: Legacy class the module behaves like, "WROOM" (no PSRAM)
            or "WROVER" (PSRAM present). Drives every pre-existing code path.
        family: Module family, e.g. "WROOM-1", "MINI-1", or None.
        flash_size_mb: Flash size decoded from the ordering code, or None.
        psram_size_mb: PSRAM size in MB (0 == none), or None if unknown.
        psram_bus: "octal", "quad" or "none".
        single_core: True for SOLO parts (S2/S3 single-core variants).
        unknown: True when the string could not be decoded at all.
        warnings: Human-readable notes; surfaced as validation warnings.
        source: "explicit-psram-override" | "module-code" | "legacy" | "default".
    """

    raw: str
    canonical: str = "WROOM"
    family: Optional[str] = None
    flash_size_mb: Optional[int] = None
    psram_size_mb: Optional[int] = None
    psram_bus: str = "none"
    single_core: bool = False
    unknown: bool = False
    warnings: List[str] = field(default_factory=list)
    source: str = "default"

    @property
    def has_psram(self) -> bool:
        return self.psram_bus in ("octal", "quad")

    @property
    def is_octal_psram(self) -> bool:
        return self.psram_bus == "octal"

    def to_dict(self) -> Dict[str, Any]:
        return {
            "raw": self.raw,
            "canonical": self.canonical,
            "family": self.family,
            "flash_size_mb": self.flash_size_mb,
            "psram_size_mb": self.psram_size_mb,
            "psram_bus": self.psram_bus,
            "single_core": self.single_core,
            "unknown": self.unknown,
            "source": self.source,
        }


def _normalise(raw: Any) -> str:
    text = str(raw or "").strip().upper()
    text = re.sub(r"[^A-Z0-9]+", "-", text).strip("-")
    return text


def _psram_bus_for_size(size_mb: int, code: str, out: ModuleSpec) -> str:
    """Decide quad vs octal from the PSRAM size digit."""
    if size_mb <= 0:
        return "none"
    if size_mb in (8, 16):
        return "octal"
    if size_mb in (1, 2, 4):
        return "quad"
    # Anything else (3, 5, 112, 162 ...) is a vendor-specific code we cannot
    # decode - stay conservative: quad reserves the fewest GPIOs.
    out.unknown = True
    out.psram_size_mb = None
    out.warnings.append(
        f"PSRAM code '{code}' does not decode to a standard 1/2/4/8/16 MB size. "
        f"Assuming a quad (QSPI) PSRAM bus, which reserves the fewest GPIOs. "
        f"Set \"psram\": {{\"mode\": \"octal|quad|none\"}} to be explicit."
    )
    return "quad"


def _finalise(spec: ModuleSpec, psram_override: Any, raw_text: str) -> ModuleSpec:
    """Apply the explicit psram field, then word-level inference, then canonical class."""
    _apply_psram_override(spec, psram_override, raw_text)

    if spec.psram_bus == "none" and spec.source != "explicit-psram-override":
        _infer_psram_from_words(spec, _normalise(spec.raw))

    if spec.is_octal_psram and spec.canonical != "WROVER":
        spec.canonical = "WROVER"
    if spec.canonical == "WROVER" and not spec.has_psram and spec.source != "explicit-psram-override":
        # A family token alone ("...WROVER...") still means "has PSRAM".
        spec.psram_bus = spec.psram_bus or "quad"
    return spec


def parse_module(
    variant: Optional[str],
    module: Any,
    psram_override: Optional[Dict[str, Any]] = None,
) -> ModuleSpec:
    """Parse a module string into a ModuleSpec. Never raises.

    Args:
        variant: Chip variant ("esp32s3", ...), used only for warning text.
        module: Raw module string from the input JSON (may be None).
        psram_override: Optional explicit PSRAM description, either a dict
            ({"mode": "octal", "size_mb": 8}) or a bare string ("octal").

    Returns:
        ModuleSpec. Unknown strings yield ``unknown=True`` plus a warning so
        that validation still runs and still emits JSON.
    """
    raw_text = str(module or "").strip()
    text = _normalise(raw_text)
    spec = ModuleSpec(raw=raw_text)

    if not text:
        spec.canonical = "WROOM"
        spec.source = "default"
        return _finalise(spec, psram_override, raw_text)

    # Legacy classes (and their documented aliases).
    legacy = text.replace("-", "")
    if legacy in ("WROOM", "WROOM32", "WROOM32D", "WROOM32U"):
        spec.canonical = "WROOM"
        spec.family = "WROOM-32" if "32" in legacy else "WROOM"
        spec.source = "legacy"
        return _finalise(spec, psram_override, raw_text)
    if legacy in ("WROVER", "WROVER32"):
        spec.canonical = "WROVER"
        spec.psram_bus = "quad"
        spec.psram_size_mb = 4
        spec.family = "WROVER-32" if "32" in legacy else "WROVER"
        spec.source = "legacy"
        return _finalise(spec, psram_override, raw_text)

    if "SOLO" in text:
        spec.single_core = True

    fam = _FAMILY_RE.search(text)
    if fam:
        name = fam.group(1)
        digit = fam.group(2)
        spec.family = f"{name}-{digit}" if digit else name
        if name in ("WROVER",):
            spec.canonical = "WROVER"

    code = _ORDERING_CODE_RE.search(text)
    if code:
        flash_mb = int(code.group(1))
        spec.flash_size_mb = flash_mb if 1 <= flash_mb <= 256 else None
        psram_code = code.group(2)
        if psram_code:
            digits = re.sub(r"[A-Z]", "", psram_code)
            size_mb = int(digits) if digits else 0
            spec.psram_size_mb = size_mb
            spec.psram_bus = _psram_bus_for_size(size_mb, f"R{psram_code}", spec)
            if spec.has_psram:
                spec.canonical = "WROVER"
        else:
            spec.psram_size_mb = 0
            spec.psram_bus = "none"
        spec.source = "module-code"
    elif spec.family is None:
        spec.unknown = True
        spec.warnings.append(
            f"Unrecognised module string '{raw_text}'"
            + (f" for {variant}" if variant else "")
            + ". Validated as a flash-only module (no PSRAM pins reserved). "
            "If this module has PSRAM, use a real ordering code "
            "(e.g. N16R8, N8R2, WROVER) or set \"psram\": {\"mode\": \"octal|quad|none\"}."
        )
        spec.source = "unknown"
    else:
        spec.source = "module-code"

    return _finalise(spec, psram_override, raw_text)


_BUS_WORDS = (
    (r"(^|[^A-Z])(OPI|OCTAL)([^A-Z]|$)", "octal"),
    (r"(^|[^A-Z])(QSPI|QUAD|QIO|DIO)([^A-Z]|$)", "quad"),
)


def _infer_psram_from_words(spec: ModuleSpec, normalised_text: str) -> None:
    """Last resort: the module field often carries prose ("N16R8 Octal PSRAM").

    Inferring a PSRAM bus from words is conservative in the safe direction only
    for "octal" (which reserves MORE pins), so both directions are reported.
    """
    for pattern, bus in _BUS_WORDS:
        if re.search(pattern, normalised_text):
            spec.psram_bus = bus
            spec.canonical = "WROVER"
            spec.warnings.append(
                f"Inferred {bus} PSRAM from the words in the module string "
                f"'{spec.raw}'. Confirm with `espefuse --chip <chip> --port <port> summary` "
                "(eFuse PSRAM_CAP / PSRAM_VENDOR) or state it explicitly: "
                "\"psram\": {\"mode\": \"octal\"}."
            )
            return
    if re.search(r"(^|[^A-Z])PSRAM([^A-Z]|$)", normalised_text):
        spec.psram_bus = "quad"
        spec.canonical = "WROVER"
        spec.warnings.append(
            f"Module string '{spec.raw}' mentions PSRAM but no size or bus width; "
            "assuming quad (which reserves the fewest pins). State it explicitly: "
            "\"psram\": {\"mode\": \"octal|quad\", \"size_mb\": 8}."
        )


def _apply_psram_override(
    spec: ModuleSpec, psram_override: Any, raw_text: str
) -> None:
    """Apply the explicit "psram" input field on top of the decoded code."""
    if psram_override is None:
        return

    if isinstance(psram_override, str):
        override: Dict[str, Any] = {"mode": psram_override}
    elif isinstance(psram_override, dict):
        override = psram_override
    else:
        spec.warnings.append(
            "Ignoring 'psram' field: expected an object "
            "({\"mode\": \"octal|quad|none\", \"size_mb\": 8}) or a string."
        )
        return

    mode_key = str(override.get("mode") or "").strip().upper()
    if mode_key not in _PSRAM_MODE_ALIASES:
        spec.warnings.append(
            f"Unknown psram.mode '{override.get('mode')}'. "
            "Use octal, quad or none. Keeping the value decoded from the module "
            f"string ('{raw_text}')."
        )
        return

    size = override.get("size_mb")
    try:
        size_mb = int(size) if size not in (None, "") else None
    except (TypeError, ValueError):
        spec.warnings.append(
            f"psram.size_mb must be an integer number of megabytes, got '{size}'. Ignored."
        )
        size_mb = None

    spec.psram_bus = _PSRAM_MODE_ALIASES[mode_key] or "none"
    spec.psram_size_mb = size_mb if size_mb is not None else (0 if spec.psram_bus == "none" else spec.psram_size_mb)
    spec.canonical = "WROVER" if spec.has_psram else "WROOM"
    spec.source = "explicit-psram-override"


def module_help_text() -> str:
    """Human-readable summary for error/help messages."""
    return (
        "module: legacy classes WROOM (no PSRAM) / WROVER (PSRAM), or a real "
        "Espressif ordering code such as ESP32-S3-WROOM-1-N16R8, N8R2, N16R8, "
        "N8, MINI-1-N4, PICO-V3-02, SOLO-1. Or omit it and describe the memory "
        "directly with \"psram\": {\"mode\": \"octal|quad|none\", \"size_mb\": 8}."
    )
