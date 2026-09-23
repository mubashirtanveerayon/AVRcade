#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PROGRAMMER="usbasp"
PART="m32"
EXPECTED_LFUSE="0xe4"
NORMAL_HFUSE="0x91"
BOOT_HFUSE="0x90"
HEX_FILE="build/atmega32_sd_bootloader.hex"

for tool in make avr-gcc avr-objcopy avr-objdump avr-size avrdude; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required command: $tool" >&2
        exit 1
    fi
done

read_fuse()
{
    local memory="$1"
    avrdude -c "$PROGRAMMER" -p "$PART" -U "$memory":r:-:h 2>/dev/null |
        tr -d '\r' | awk '/^0x[[:xdigit:]]+$/ { value=tolower($0) } END { print value }'
}

echo "This will:"
echo "  1. build and size-check the 4 KiB bootloader"
echo "  2. back up EEPROM and current fuse readings under ./build"
echo "  3. chip-erase flash (EEPROM is preserved by EESAVE)"
echo "  4. program and verify the bootloader"
echo "  5. change only HFUSE 0x91 -> 0x90 to enable boot reset"
echo
echo "Before continuing, put a valid MENU.BIN in the FAT32 SD-card root."

if [[ "${1:-}" != "--yes" ]]; then
    read -r -p "Type INSTALL to continue: " answer
    if [[ "$answer" != "INSTALL" ]]; then
        echo "Cancelled."
        exit 0
    fi
fi

make clean all

echo "Checking the ATmega32A connection..."
avrdude -c "$PROGRAMMER" -p "$PART" -n

current_hfuse="$(read_fuse hfuse)"
current_lfuse="$(read_fuse lfuse)"

if [[ "$current_lfuse" != "$EXPECTED_LFUSE" ]]; then
    echo "Refusing to continue: LFUSE is $current_lfuse, expected $EXPECTED_LFUSE." >&2
    echo "The bootloader is compiled for the internal 8 MHz configuration." >&2
    exit 1
fi

if [[ "$current_hfuse" != "$NORMAL_HFUSE" && "$current_hfuse" != "$BOOT_HFUSE" ]]; then
    echo "Refusing to continue: unexpected HFUSE $current_hfuse." >&2
    exit 1
fi

mkdir -p build
printf 'HFUSE=%s\nLFUSE=%s\n' "$current_hfuse" "$current_lfuse" \
    > build/fuses-before-install.txt
avrdude -c "$PROGRAMMER" -p "$PART" \
    -U eeprom:r:build/eeprom-before-install.hex:i

echo "Programming and verifying the bootloader before changing BOOTRST..."
avrdude -c "$PROGRAMMER" -p "$PART" -e \
    -U flash:w:"$HEX_FILE":i

echo "Enabling reset into the 4 KiB boot section..."
avrdude -c "$PROGRAMMER" -p "$PART" -U hfuse:w:"$BOOT_HFUSE":m

verified_hfuse="$(read_fuse hfuse)"
if [[ "$verified_hfuse" != "$BOOT_HFUSE" ]]; then
    echo "HFUSE verification failed: read $verified_hfuse, expected $BOOT_HFUSE." >&2
    exit 1
fi

echo "Installed successfully. HFUSE=$verified_hfuse, LFUSE=$current_lfuse."
echo "Disconnect USBasp (or release RESET) and reset the board to boot from SD."
echo "The old-USBasp 'cannot set sck period' message is harmless when reads/writes succeed."
