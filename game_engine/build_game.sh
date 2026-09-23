#!/usr/bin/env bash
set -euo pipefail

if (( $# < 2 )); then
    echo "Usage: $0 GAME_NAME game_file.c [more_game_files.c ...]" >&2
    echo "Example: $0 DEMO example_game.c" >&2
    exit 1
fi

ENGINE_DIRECTORY="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_NAME="${1^^}"
shift



if [[ "${GAME_NAME}" == "MENU" ]]; then
    echo "Error: MENU.BIN is reserved for the selector application." >&2
    exit 1
fi

if [[ -n "${U8G2_ROOT:-}" ]]; then
    U8G2_DIRECTORY="${U8G2_ROOT}"
elif [[ -f "${ENGINE_DIRECTORY}/u8g2/csrc/u8g2.h" ]]; then
    U8G2_DIRECTORY="${ENGINE_DIRECTORY}/u8g2"
else
    U8G2_DIRECTORY="${ENGINE_DIRECTORY}/../u8g2"
fi

U8G2_CSRC="${U8G2_DIRECTORY}/csrc"
if [[ ! -f "${U8G2_CSRC}/u8g2.h" ]]; then
    echo "Error: U8g2 csrc directory not found at ${U8G2_CSRC}" >&2
    echo "Set U8G2_ROOT=/path/to/u8g2 and run again." >&2
    exit 1
fi

for command_name in avr-gcc avr-objcopy avr-size; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Error: ${command_name} is not installed." >&2
        exit 1
    fi
done

GAME_SOURCES=()
for source_file in "$@"; do
    if [[ ! -f "${source_file}" || "${source_file}" != *.c ]]; then
        echo "Error: expected an existing .c file: ${source_file}" >&2
        exit 1
    fi
    GAME_SOURCES+=("${source_file}")
done

GAME_BUILD_DIRECTORY="$(mktemp -d)"
trap 'rm -rf -- "${GAME_BUILD_DIRECTORY}"' EXIT

CFLAGS=(
    -mmcu=atmega32
    -DF_CPU=8000000UL
    -I"${ENGINE_DIRECTORY}"
    -I"${U8G2_CSRC}"
    -std=gnu11
    -Os
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)

echo "Building ${GAME_NAME}.BIN..."

avr-gcc "${CFLAGS[@]}" \
    "${ENGINE_DIRECTORY}/engine.c" \
    "${ENGINE_DIRECTORY}/engine_platform.c" \
    "${GAME_SOURCES[@]}" \
    "${U8G2_CSRC}"/*.c \
    -Wl,--gc-sections \
    -o "${GAME_BUILD_DIRECTORY}/${GAME_NAME}.elf"

avr-objcopy -O binary -R .eeprom \
    "${GAME_BUILD_DIRECTORY}/${GAME_NAME}.elf" \
    "${GAME_NAME}.BIN"

avr-size -C --mcu=atmega32 "${GAME_BUILD_DIRECTORY}/${GAME_NAME}.elf"

GAME_SIZE="$(stat -c %s "${GAME_NAME}.BIN")"
MAX_APPLICATION_BYTES="${MAX_APPLICATION_BYTES:-28672}"

if (( GAME_SIZE > MAX_APPLICATION_BYTES )); then
    rm -f -- "${GAME_NAME}.BIN"
    echo "Error: game is ${GAME_SIZE} bytes; limit is ${MAX_APPLICATION_BYTES}." >&2
    exit 1
fi

echo "Created ${GAME_NAME}.BIN (${GAME_SIZE} bytes)"
