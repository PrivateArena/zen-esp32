#!/usr/bin/env bash

# Get the absolute path of the portable directory
export ESP_PORTABLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Set ESP-IDF paths
export IDF_PATH="$ESP_PORTABLE_DIR/idf"
export IDF_TOOLS_PATH="$ESP_PORTABLE_DIR/tools"

# Source the ESP-IDF export script, which automatically configures Python venv and tools paths
if [ -f "$IDF_PATH/export.sh" ]; then
    . "$IDF_PATH/export.sh"
else
    echo "Error: ESP-IDF export.sh not found!"
    return 1
fi

echo "ESP32 Portable Environment Activated!"
echo "IDF_PATH: $IDF_PATH"
echo "IDF_TOOLS_PATH: $IDF_TOOLS_PATH"
