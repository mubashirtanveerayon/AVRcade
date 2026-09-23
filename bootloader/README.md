# ATmega32A SD-card bootloader

This package is matched to the attached handheld wiring and menu program:

- ATmega32A at 8 MHz from the internal RC oscillator
- SD `CS=PB4`, `MOSI=PB5`, `MISO=PB6`, `SCK=PB7`
- SSD1309 `CS=PB3` and `RESET=PB1` are held inactive while booting
- EEPROM bytes `0..12` contain the menu's null-terminated 8.3 filename
- status LED is active-high on `PA7`, physical pin 33

## What happens after reset

1. The bootloader mounts the FAT32 SD card.
2. It reads EEPROM bytes `0..12` and accepts a null-terminated `.BIN` 8.3 name.
3. If that file opens and is a valid size, it programs it into `0x0000..0x6FFF`.
4. If EEPROM is empty/invalid or the selected image cannot be used, it tries
   `MENU.BIN` in the SD-card root.
5. Each application page is compared before writing, so resetting with the same
   image does not needlessly consume flash erase/write cycles.
6. Every written page is read back and verified. Unused application pages are
   returned to `0xFF`.
7. Only after the final image succeeds, EEPROM bytes `0..12` are erased to
   `0xFF`. No other EEPROM locations are changed.
8. Execution jumps through the application's reset vector at address zero.
9. A fatal failure produces a repeating LED code and never jumps into a known
   incomplete application.

The bootloader itself occupies `0x7000..0x7FFF`. The Makefile links it at
`0x7000` and refuses to finish if its program image exceeds 4096 bytes.

## SD card and application-image requirements

- Format the card as FAT32. This size-optimized PetitFS configuration supports
  FAT32 only.
- Put files in the root directory and use short 8.3 names such as `PONG.BIN`.
- Always provide `MENU.BIN` before installing the bootloader.
- Files must be raw AVR binaries, not Intel HEX files.
- Each binary must be non-empty and no larger than 28 KiB (28672 bytes).
- Applications must use the normal link address zero. Do not relocate a game to
  the boot section.

The package contains the modified menu and its own full PetitFS configuration
under `menu/`. Build it with:

```bash
make -C menu clean all
```

The resulting file is `menu/build/menu.bin`. Copy it to the card as `MENU.BIN`,
replacing the old menu binary. This version saves the selected filename, shows
`STARTING...`, and uses the watchdog to reset the MCU into the bootloader.

The generic builder remains available for games, for example:

```bash
chmod +x make_application_bin.sh
./make_application_bin.sh -o pong.bin pong.c pff.c diskio.c
```

PetitFS returns root-directory short names in uppercase, and the bootloader
also uppercases the EEPROM selection before opening it.

## Status LED wiring and codes

Wire:

```text
PA7 / physical pin 33 -> 330 Ohm resistor -> LED anode
LED cathode (short leg / flat side) -> GND
```

Repeating groups mean:

| Flashes | Meaning |
| ---: | --- |
| 1 | SD initialization or FAT32 mount failed |
| 2 | Requested image unavailable and `MENU.BIN` missing |
| 3 | Final image is empty or larger than 28 KiB |
| 4 | FAT or file read failed |
| 5 | Flash read-back verification failed |

Fix the SD card/file problem and reset. Even if programming was interrupted,
the bootloader remains in its separate boot section and retries on the next
reset.

## Build and install with USBasp

Install the Linux AVR tools if needed:

```bash
sudo apt install gcc-avr avr-libc binutils-avr avrdude make
```

Then run:

```bash
chmod +x install_bootloader.sh restore_usbasp_mode.sh
make -C menu clean all
# Copy menu/build/menu.bin to the SD-card root as MENU.BIN here.
./install_bootloader.sh
```

`install_bootloader.sh` performs these safety checks/actions in order:

- compiles, links, locates and size-checks the bootloader;
- verifies the connected part and requires your current `LFUSE=0xE4`;
- accepts only the known starting/boot high-fuse values `0x91` or `0x90`;
- saves EEPROM and fuse backups under `build/`;
- erases flash and programs/verifies the bootloader while reset still points at
  the ordinary application area;
- only after successful verification, writes `HFUSE=0x90` to program
  `BOOTRST`.

Your fuse transition is:

| Setting | Before | Bootloader mode |
| --- | ---: | ---: |
| Low fuse | `0xE4` | `0xE4` (unchanged) |
| High fuse | `0x91` | `0x90` |
| Boot size | 4 KiB | 4 KiB |
| Preserve EEPROM on chip erase | yes | yes |
| Reset destination | application | bootloader |

The USBasp message `cannot set sck period; please check for usbasp firmware
update` is a firmware-capability warning. In your case communication is working
because the signature and fuses are successfully read. The scripts do not rely
on changing the USBasp SCK period.

After installation, do not run the old normal `flash.sh` for routine game
changes: its ISP chip erase can erase this bootloader. Build `.bin` files and
copy them to SD instead. USBasp remains the recovery method and can always
reinstall or remove the bootloader.

## Roll back to ordinary USBasp uploads

Run:

```bash
./restore_usbasp_mode.sh
```

This backs up EEPROM and flash, erases all flash, and restores your exact
pre-bootloader baseline: `HFUSE=0x91`, `LFUSE=0xE4`. The chip is then blank and
ready for the normal USBasp upload script. EEPROM remains preserved.

This is intentionally your working baseline, not the literal factory-new fuse
pair (`0x99/0xE1` on an ATmega32A). Literal factory fuses would switch the MCU
back to 1 MHz and stop preserving EEPROM across later chip erases, which does
not match your current 8 MHz project.

## One-shot game selection and return to menu

The modified menu saves a game filename and immediately performs a watchdog
reset. The bootloader flashes and verifies that game, clears only EEPROM bytes
`0..12`, and starts it. The next reset therefore sees no selection and loads
`MENU.BIN` automatically. Games do not need to modify EEPROM to return to the
menu; they only need to reset, or the player can press the hardware reset
button.

If power is lost before the selected image finishes and verifies, the filename
is deliberately left in EEPROM. The next reset retries the same image instead
of treating a partially programmed application as successful.

## Package contents

- `bootloader.c` - boot decision, SPM page programming, verification and LED
- `pff.c`, `pff.h`, `pffconf.h` - supplied PetitFS R0.03a, trimmed for boot use
- `diskio.c`, `diskio.h` - supplied PB4/hardware-SPI SD driver
- `Makefile` - boot-section link and hard 4 KiB size check
- `install_bootloader.sh` - build, backup, program, verify and fuse setup
- `restore_usbasp_mode.sh` - safe return to the user's original fuse mode
- `make_application_bin.sh` - raw application binary builder with 28 KiB check
- `menu/` - modified menu source, full PetitFS files, Makefile and built binary
- `complete_handheld_wiring.txt` - complete wiring including the PA7 status LED
