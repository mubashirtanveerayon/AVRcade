MCU := atmega32
F_CPU := 8000000UL
APPLICATION_BYTES := 28672

TARGET := build/menu
U8G2_DIR ?= ../u8g2/csrc
U8G2_SOURCES := \
	$(U8G2_DIR)/u8g2_box.c \
	$(U8G2_DIR)/u8g2_buffer.c \
	$(U8G2_DIR)/u8g2_circle.c \
	$(U8G2_DIR)/u8g2_d_memory.c \
	$(U8G2_DIR)/u8g2_d_setup.c \
	$(U8G2_DIR)/u8g2_font.c \
	$(U8G2_DIR)/u8g2_fonts.c \
	$(U8G2_DIR)/u8g2_hvline.c \
	$(U8G2_DIR)/u8g2_intersection.c \
	$(U8G2_DIR)/u8g2_line.c \
	$(U8G2_DIR)/u8g2_ll_hvline.c \
	$(U8G2_DIR)/u8g2_setup.c \
	$(U8G2_DIR)/u8x8_8x8.c \
	$(U8G2_DIR)/u8x8_byte.c \
	$(U8G2_DIR)/u8x8_cad.c \
	$(U8G2_DIR)/u8x8_display.c \
	$(U8G2_DIR)/u8x8_d_ssd1309.c \
	$(U8G2_DIR)/u8x8_gpio.c \
	$(U8G2_DIR)/u8x8_setup.c \
	$(U8G2_DIR)/u8x8_string.c
SOURCES := menu.c pff.c diskio.c $(U8G2_SOURCES)

CC := avr-gcc
OBJCOPY := avr-objcopy
SIZE := avr-size

CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -std=gnu99 -Os -flto \
	-Wall -Wextra -ffunction-sections -fdata-sections -I$(U8G2_DIR)
LDFLAGS := -mmcu=$(MCU) -flto -Wl,--gc-sections,--relax \
	-Wl,-Map,$(TARGET).map

.PHONY: all clean size-check

all: $(TARGET).bin size-check

build:
	mkdir -p build

$(TARGET).elf: $(SOURCES) pff.h pffconf.h diskio.h \
	$(U8G2_DIR)/u8g2.h $(U8G2_DIR)/u8x8.h | build
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary -R .eeprom -R .fuse -R .lock -R .signature $< $@

size-check: $(TARGET).elf $(TARGET).bin
	@used=`$(SIZE) -B $(TARGET).elf | awk 'NR == 2 { print $$1 + $$2 }'`; \
	bin_size=`wc -c < $(TARGET).bin`; \
	if [ -z "$$used" ]; then \
		echo "Could not inspect the linked menu application." >&2; exit 1; \
	fi; \
	if [ "$$used" -gt $(APPLICATION_BYTES) ] || [ "$$bin_size" -gt $(APPLICATION_BYTES) ]; then \
		echo "ERROR: menu exceeds the $(APPLICATION_BYTES)-byte application area." >&2; exit 1; \
	fi; \
	echo "Menu binary: $$bin_size bytes; flash use: $$used / $(APPLICATION_BYTES) bytes"
	$(SIZE) -C --mcu=$(MCU) $(TARGET).elf

clean:
	rm -rf build
