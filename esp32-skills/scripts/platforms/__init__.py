"""
Platform factory: variant + module selection for the ESP32 family.

Variants
    esp32, esp32s2, esp32s3, esp32c3, esp32c6

Modules
    Legacy classes:
        WROOM  - flash only                (no PSRAM pins reserved)
        WROVER - flash + PSRAM             (GPIO16-17 on the original ESP32)
    Real ordering codes (decoded in platforms/modules.py):
        ESP32-S3-WROOM-1-N16R8, N8R2, N16R8, N8, MINI-1-N4, PICO-V3-02, SOLO-1
    The decoded code decides whether the module has quad or octal PSRAM and
    therefore which GPIOs are unusable.

An explicit memory description always wins:
    {"variant": "esp32s3", "psram": {"mode": "octal", "size_mb": 8}}
"""

from typing import Any, Dict, Optional

from .base import (
    ConflictType,
    GenerationResult,
    PeripheralGroup,
    Pin,
    Platform,
    ValidationResult,
)
from .esp32 import Esp32Platform
from .modules import ModuleSpec, parse_module


# Single authoritative list of supported variants — imported by scripts too.
SUPPORTED_VARIANTS = ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]

__all__ = [
    "Pin",
    "PeripheralGroup",
    "ConflictType",
    "ValidationResult",
    "GenerationResult",
    "Platform",
    "ModuleSpec",
    "SUPPORTED_VARIANTS",
    "get_platform",
    "normalise_variant",
]


# Registry of platform implementations.
_PLATFORM_REGISTRY: dict = {
    ("esp32", "esp32"): Esp32Platform,
    ("esp32", "esp32s2"): Esp32Platform,
    ("esp32", "esp32s3"): Esp32Platform,
    ("esp32", "esp32c3"): Esp32Platform,
    ("esp32", "esp32c6"): Esp32Platform,
}


def normalise_variant(value: Any) -> str:
    """Fold separators so "esp32-s3", "ESP32_S3" and "esp32 s3" all map to esp32s3."""
    folded = str(value or "").lower().strip().replace("-", "").replace("_", "").replace(" ", "")
    aliases = {"esp32s": "esp32s2", "s3": "esp32s3", "s2": "esp32s2"}
    return aliases.get(folded, folded)


def get_platform(
    platform: str,
    variant: Optional[str] = None,
    module: Optional[str] = None,
    psram: Optional[Any] = None,
) -> Platform:
    """
    Factory function to instantiate an ESP32 platform implementation.

    Args:
        platform: Platform family, e.g. "esp32".
        variant: Chip variant; inferred from `platform` when omitted.
        module: Module class or real ordering code (see platforms.modules).
        psram: Optional explicit PSRAM description, {"mode": ..., "size_mb": ...}.

    Raises:
        ValueError: Only for an unsupported *variant* — an unsupported module is
            a warning, not a hard error, so callers always get parseable JSON.
    """
    platform_lower = normalise_variant(platform)

    # If platform is a recognised variant name (e.g., "esp32s3"), use it as
    # the variant only when no explicit variant was provided.
    if variant is None:
        variant = platform_lower
    else:
        variant = normalise_variant(variant)

    if not variant:
        variant = "esp32"

    if variant not in SUPPORTED_VARIANTS:
        raise ValueError(
            f"Unsupported variant '{variant}'. Supported: {', '.join(SUPPORTED_VARIANTS)}"
        )

    spec: ModuleSpec = parse_module(variant, module, psram)

    registry_key = ("esp32", variant)
    if registry_key in _PLATFORM_REGISTRY:
        return _PLATFORM_REGISTRY[registry_key](
            name="esp32",
            variant=variant,
            module=spec.canonical,
            module_spec=spec,
        )

    raise ValueError(f"Implementation for {variant} not found.")
