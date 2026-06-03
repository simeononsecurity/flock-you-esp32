#!/bin/bash
# Flash Atom VoiceS3R with Flock-You firmware (TESTING_MODE=1)
# ─────────────────────────────────────────────────────────────
# Before running: put device in DOWNLOAD MODE
#   Hold the reset button ~2 sec until the internal green LED lights, then release.

set -e  # Exit on error

# ── Virtual Environment Auto-Load ────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

activate_venv() {
    local venv_path="$1"
    if [ -f "$venv_path/bin/activate" ]; then
        echo "🐍 Activating venv: $venv_path"
        # shellcheck disable=SC1091
        source "$venv_path/bin/activate"
        return 0
    fi
    return 1
}

if [ -z "$VIRTUAL_ENV" ]; then
    VENV_FOUND=false
    for candidate in \
        "$SCRIPT_DIR/.venv" \
        "$SCRIPT_DIR/venv" \
        "$SCRIPT_DIR/env" \
        "$SCRIPT_DIR/api/.venv" \
        "$SCRIPT_DIR/api/venv" \
        "$HOME/.platformio/penv"
    do
        if activate_venv "$candidate"; then
            VENV_FOUND=true
            break
        fi
    done

    if [ "$VENV_FOUND" = false ]; then
        echo "⚠️  No venv found — using system Python/pio (if available)"
    fi
else
    echo "🐍 Using active venv: $VIRTUAL_ENV"
fi
# ─────────────────────────────────────────────────────────────────────────────

echo "🔍 Looking for Atom VoiceS3R (USB-CDC)..."

# ESP32-S3 native USB shows up as /dev/tty.usbmodem* on macOS
PORTS=$(ls /dev/tty.usbmodem* 2>/dev/null || true)

if [ -z "$PORTS" ]; then
    echo ""
    echo "❌ ERROR: No Atom VoiceS3R found!"
    echo ""
    echo "   → Enter DOWNLOAD MODE first:"
    echo "     Hold the reset button ~2 sec until the internal green LED lights,"
    echo "     then release. The port will appear as /dev/tty.usbmodem*"
    echo ""
    exit 1
fi

PORT_COUNT=$(echo "$PORTS" | wc -l | tr -d ' ')

if [ "$PORT_COUNT" -gt 1 ]; then
    echo "⚠️  Multiple USB-CDC devices found:"
    echo "$PORTS"
    echo ""
    echo "Please select which one to flash:"
    select PORT in $PORTS; do
        if [ -n "$PORT" ]; then
            break
        fi
    done
else
    PORT="$PORTS"
fi

echo "✅ Found Atom VoiceS3R at: $PORT"
echo ""

# Release any process holding the CDC port (e.g. leftover serial monitors)
PORT_HOLDER=$(lsof -t "$PORT" 2>/dev/null || true)
if [ -n "$PORT_HOLDER" ]; then
    echo "🔓 Releasing port held by PID $PORT_HOLDER..."
    kill "$PORT_HOLDER" 2>/dev/null || true
    sleep 1
fi

# Change to project directory
cd "$(dirname "$0")"

echo "🔨 Compiling firmware (TESTING_MODE=1)..."
pio run --environment m5atom-voices3r

echo ""
echo "⬆️  Uploading to $PORT..."
# Use brew esptool directly — handles ESP32-S3 native USB CDC reset reliably
esptool --chip esp32s3 \
    --port "$PORT" \
    --before usb-reset \
    --after hard-reset \
    write-flash \
    0x0    .pio/build/m5atom-voices3r/bootloader.bin \
    0x8000 .pio/build/m5atom-voices3r/partitions.bin \
    0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
    0x10000 .pio/build/m5atom-voices3r/firmware.bin

echo ""
echo "✅ Flash complete!"
echo "   The device will reboot automatically."
echo "   Any WiFi packet it hears will trigger a detection line on the serial monitor."
echo ""

# Ask if user wants to monitor
read -p "📡 Start serial monitor? (y/n) " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Starting monitor (Ctrl+C to exit)..."
    echo "────────────────────────────────────"
    pio device monitor --port "$PORT" --baud 115200 --environment m5atom-voices3r
fi

echo ""
echo "✅ Done!"
