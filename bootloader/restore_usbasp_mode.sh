#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PROGRAMMER="usbasp"
PART="m32"
EXPECTED_LFUSE="0xe4"
BOOT_HFUSE="0x90"
NORMAL_HFUSE="0x91"

for tool in avrdude; do
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

echo "This rollback erases all flash, removes the bootloader, and restores your"
echo "pre-bootloader fuse values: HFUSE=0x91 and LFUSE=0xE4."
echo "EEPROM is preserved because EESAVE is programmed in HFUSE 0x90/0x91."
echo "Afterward the MCU is blank and ready for your normal USBasp flash script."

if [[ "${1:-}" != "--yes" ]]; then
    read -r -p "Type ROLLBACK to continue: " answer
    if [[ "$answer" != "ROLLBACK" ]]; then
        echo "Cancelled."
        exit 0
    fi
fi

avrdude -c "$PROGRAMMER" -p "$PART" -n
current_hfuse="$(read_fuse hfuse)"
current_lfuse="$(read_fuse lfuse)"

if [[ "$current_lfuse" != "$EXPECTED_LFUSE" ]]; then
    echo "Refusing to continue: LFUSE is $current_lfuse, expected $EXPECTED_LFUSE." >&2
    exit 1
fi

if [[ "$current_hfuse" != "$BOOT_HFUSE" && "$current_hfuse" != "$NORMAL_HFUSE" ]]; then
    echo "Refusing to continue: unexpected HFUSE $current_hfuse." >&2
    exit 1
fi

mkdir -p build
avrdude -c "$PROGRAMMER" -p "$PART" \
    -U eeprom:r:build/eeprom-before-rollback.hex:i \
    -U flash:r:build/flash-before-rollback.hex:i

avrdude -c "$PROGRAMMER" -p "$PART" -e \
    -U hfuse:w:"$NORMAL_HFUSE":m \
    -U lfuse:w:"$EXPECTED_LFUSE":m

verified_hfuse="$(read_fuse hfuse)"
verified_lfuse="$(read_fuse lfuse)"
if [[ "$verified_hfuse" != "$NORMAL_HFUSE" || "$verified_lfuse" != "$EXPECTED_LFUSE" ]]; then
    echo "Fuse verification failed: HFUSE=$verified_hfuse LFUSE=$verified_lfuse." >&2
    exit 1
fi

echo "Rollback complete. HFUSE=$verified_hfuse, LFUSE=$verified_lfuse."
echo "You can now upload an ordinary application with USBasp."
