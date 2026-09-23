MCU := atmega32
F_CPU := 8000000UL
BOOT_START := 0x7000
BOOT_BYTES := 4096

TARGET := build/atmega32_sd_bootloader
SOURCES := bootloader.c pff.c diskio.c

CC := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
SIZE := avr-size

CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -std=gnu99 -Os -flto \
	-Wall -Wextra -Wshadow -Wundef -ffunction-sections -fdata-sections \
	-mcall-prologues
LDFLAGS := -mmcu=$(MCU) -flto \
	-Wl,--section-start=.text=$(BOOT_START),--gc-sections,--relax \
	-Wl,-Map,$(TARGET).map

.PHONY: all clean size-check

all: $(TARGET).hex size-check

build:
	mkdir -p build

$(TARGET).elf: $(SOURCES) pff.h pffconf.h diskio.h | build
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom -R .fuse -R .lock -R .signature $< $@

size-check: $(TARGET).elf
	@used=`$(SIZE) -B $(TARGET).elf | awk 'NR == 2 { print $$1 + $$2 }'`; \
	start=`$(OBJDUMP) -h $(TARGET).elf | awk '$$2 == ".text" { print $$4 }'`; \
	if [ -z "$$used" ] || [ -z "$$start" ]; then \
		echo "Could not inspect the linked bootloader." >&2; exit 1; \
	fi; \
	if [ "$$start" != "00007000" ]; then \
		echo "ERROR: .text starts at $$start, expected 00007000." >&2; exit 1; \
	fi; \
	if [ "$$used" -gt $(BOOT_BYTES) ]; then \
		echo "ERROR: bootloader uses $$used bytes; limit is $(BOOT_BYTES)." >&2; exit 1; \
	fi; \
	echo "Bootloader flash use: $$used / $(BOOT_BYTES) bytes"
	$(SIZE) -C --mcu=$(MCU) $(TARGET).elf

clean:
	rm -rf build
