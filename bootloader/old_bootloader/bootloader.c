#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/boot.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdint.h>
#include <util/delay.h>

#include "pff.h"

/*
 * ATmega32A layout with BOOTSZ1:0 = 00:
 *   application: 0x0000..0x6FFF (28 KiB)
 *   bootloader:  0x7000..0x7FFF (4 KiB)
 *
 * Addresses in this file are byte addresses.  avr-libc's boot_page_*
 * macros also take byte addresses.
 */
#define APPLICATION_END 0x7000U
#define EEPROM_NAME_ADDRESS 0U
#define EEPROM_STEM_SIZE 9U
#define FILE_NAME_SIZE 13U
#define SD_RECOVERY_BYTES 20U

#define OLED_RESET PB1
#define OLED_CS PB3
#define SD_CS PB4
#define SPI_MOSI PB5
#define SPI_MISO PB6
#define SPI_SCK PB7

/* New status LED: PA7 (physical pin 33), active high. */
#define STATUS_LED PA7

#if SPM_PAGESIZE != 128
#error This bootloader expects the ATmega32A 128-byte flash page size.
#endif

enum boot_result {
    BOOT_OK = 0,
    BOOT_SD_ERROR = 1,
    BOOT_NO_FILE = 2,
    BOOT_BAD_IMAGE = 3,
    BOOT_READ_ERROR = 4,
    BOOT_VERIFY_ERROR = 5
};

static FATFS file_system;
static uint8_t page_buffer[SPM_PAGESIZE];
static char fallback_name[] = "MENU.BIN";

/* Disable a watchdog-reset watchdog before normal C startup runs. */
void disable_watchdog_early(void)
    __attribute__((naked, section(".init3"), used));

void disable_watchdog_early(void)
{
    MCUSR = 0;
    wdt_disable();
}

/* diskio.c calls this function. */
uint8_t spi_transfer(uint8_t value)
{
    SPDR = value;
    while ((SPSR & (1U << SPIF)) == 0U) {
    }
    return SPDR;
}

static void spi_init_slow(void)
{
    /* Keep both SPI devices deselected. PB4/SS must be an output in master mode. */
    PORTB |= (1U << OLED_RESET) | (1U << OLED_CS) | (1U << SD_CS);
    DDRB |= (1U << OLED_RESET) | (1U << OLED_CS) | (1U << SD_CS) |
            (1U << SPI_MOSI) | (1U << SPI_SCK);
    DDRB &= (uint8_t)~(1U << SPI_MISO);

    /* Mode 0, master, fCPU/32 = 250 kHz for SD-card initialization. */
    SPCR = (1U << SPE) | (1U << MSTR) | (1U << SPR1);
    SPSR = (1U << SPI2X);
}

static void spi_set_fast(void)
{
    /* Mode 0, master, fCPU/4 = 2 MHz after initialization. */
    SPCR = (1U << SPE) | (1U << MSTR);
    SPSR = 0;
}

static void prepare_sd_mount_attempt(void)
{
    uint8_t count;

    /* Recover a continuously powered card left mid-command by an MCU reset. */
    PORTB |= (1U << OLED_CS) | (1U << SD_CS);
    spi_init_slow();
    _delay_ms(2);

    for (count = 0U; count < SD_RECOVERY_BYTES; count++) {
        spi_transfer(0xFFU);
    }
}

static void status_led_init(void)
{
    PORTA &= (uint8_t)~(1U << STATUS_LED);
    DDRA |= (1U << STATUS_LED);
}

static void delay_100ms(uint8_t count)
{
    while (count-- != 0U) {
        _delay_ms(100);
    }
}

/* Repeats an error code forever: N short flashes, pause, then repeat. */
static void fatal_blink(uint8_t code) __attribute__((noreturn));

static void fatal_blink(uint8_t code)
{
    uint8_t count;

    SPCR = 0;
    PORTB |= (1U << OLED_CS) | (1U << SD_CS);

    while (1) {
        for (count = 0; count < code; count++) {
            PORTA |= (1U << STATUS_LED);
            delay_100ms(1);
            PORTA &= (uint8_t)~(1U << STATUS_LED);
            delay_100ms(2);
        }
        delay_100ms(8);
    }
}

static uint8_t load_eeprom_filename(char *name)
{
    uint8_t index;
    uint8_t terminated = 0;

    /* EEPROM stores only a FAT 8.3 stem of at most eight characters. */
    for (index = 0; index < EEPROM_STEM_SIZE; index++) {
        uint8_t character = eeprom_read_byte(
            (const uint8_t *)(uintptr_t)(EEPROM_NAME_ADDRESS + index));

        if (character == 0U) {
            name[index] = '\0';
            terminated = 1;
            break;
        }

        if (character == 0xFFU || character <= 32U || character > 126U ||
            character == '.' || character == '/' || character == '\\') {
            return 0;
        }

        if (character >= 'a' && character <= 'z') {
            character = (uint8_t)(character - 'a' + 'A');
        }

        name[index] = (char)character;
    }

    if (terminated == 0U || index == 0U) {
        return 0;
    }

    /* Reconstruct the SD-card short name in RAM. */
    name[index++] = '.';
    name[index++] = 'B';
    name[index++] = 'I';
    name[index++] = 'N';
    name[index] = '\0';
    return 1;
}

/*
 * Clear only the 9-byte filename-stem mailbox. Other EEPROM bytes may later be
 * used for settings or saves and must not be destroyed by the bootloader.
 */
static void clear_eeprom_filename(void)
{
    uint8_t index;

    for (index = 0; index < EEPROM_STEM_SIZE; index++) {
        eeprom_update_byte(
            (uint8_t *)(uintptr_t)(EEPROM_NAME_ADDRESS + index), 0xFFU);
    }
    eeprom_busy_wait();
}

static uint8_t mount_card(void)
{
    prepare_sd_mount_attempt();

    if (pf_mount(&file_system) == FR_OK) {
        spi_set_fast();
        return 1;
    }

    PORTB |= (1U << OLED_CS) | (1U << SD_CS);
    return 0;
}

static uint8_t flash_page_matches(uint16_t page_address)
{
    uint8_t index;

    for (index = 0; index < SPM_PAGESIZE; index++) {
        if (pgm_read_byte_near(page_address + index) != page_buffer[index]) {
            return 0;
        }
    }
    return 1;
}

static void write_flash_page(uint16_t page_address)
{
    uint8_t saved_sreg = SREG;
    uint8_t index;

    cli();
    eeprom_busy_wait();
    boot_page_erase(page_address);
    boot_spm_busy_wait();

    for (index = 0; index < SPM_PAGESIZE; index += 2U) {
        uint16_t word = page_buffer[index];
        word |= (uint16_t)page_buffer[index + 1U] << 8;
        boot_page_fill(page_address + index, word);
    }

    boot_page_write(page_address);
    boot_spm_busy_wait();
    boot_rww_enable();
    SREG = saved_sreg;
}

static uint8_t program_open_file(uint16_t file_size)
{
    uint16_t page_address;

    for (page_address = 0; page_address < APPLICATION_END;
         page_address += SPM_PAGESIZE) {
        uint8_t index;
        UINT bytes_read;
        UINT requested = 0;

        for (index = 0; index < SPM_PAGESIZE; index++) {
            page_buffer[index] = 0xFFU;
        }

        if (page_address < file_size) {
            requested = (UINT)(file_size - page_address);
            if (requested > SPM_PAGESIZE) {
                requested = SPM_PAGESIZE;
            }

            if (pf_read(page_buffer, requested, &bytes_read) != FR_OK ||
                bytes_read != requested) {
                return BOOT_READ_ERROR;
            }
        }

        /* Avoid flash wear when this page already has the requested contents. */
        if (flash_page_matches(page_address) == 0U) {
            write_flash_page(page_address);
            if (flash_page_matches(page_address) == 0U) {
                return BOOT_VERIFY_ERROR;
            }
        }
    }

    return BOOT_OK;
}

static uint8_t program_file(const char *filename)
{
    FRESULT result = pf_open(filename);

    if (result == FR_NO_FILE) {
        return BOOT_NO_FILE;
    }
    if (result != FR_OK) {
        return BOOT_READ_ERROR;
    }
    if (file_system.fsize == 0U || file_system.fsize > APPLICATION_END) {
        return BOOT_BAD_IMAGE;
    }

    return program_open_file((uint16_t)file_system.fsize);
}

static uint8_t is_fallback_name(const char *name)
{
    uint8_t index;

    for (index = 0; fallback_name[index] != '\0'; index++) {
        if (name[index] != fallback_name[index]) {
            return 0;
        }
    }
    return name[index] == '\0';
}

static void start_application(void) __attribute__((noreturn));

static void start_application(void)
{
    cli();

    /* Leave shared devices deselected and turn off the status LED. */
    PORTB |= (1U << OLED_CS) | (1U << SD_CS);
    SPCR = 0;
    SPSR = 0;
    PORTA &= (uint8_t)~(1U << STATUS_LED);
    DDRA &= (uint8_t)~(1U << STATUS_LED);

    /* Address zero is the normal application's reset vector. */
    __asm__ __volatile__("jmp 0x0000");
    __builtin_unreachable();
}

int main(void)
{
    char selected_name[FILE_NAME_SIZE];
    uint8_t has_selection;
    uint8_t result;

    cli();
    MCUSR = 0;
    wdt_disable();
    status_led_init();

    if (mount_card() == 0U) {
        fatal_blink(BOOT_SD_ERROR);
    }

    has_selection = load_eeprom_filename(selected_name);
    if (has_selection != 0U) {
        result = program_file(selected_name);
        if (result == BOOT_OK) {
            clear_eeprom_filename();
            start_application();
        }

        /* Do not retry MENU.BIN under the same name. */
        if (is_fallback_name(selected_name) != 0U) {
            fatal_blink(result);
        }

        /* A partial read may have invalidated PetitFS state. Reinitialize it. */
        if (mount_card() == 0U) {
            fatal_blink(BOOT_SD_ERROR);
        }
    }

    result = program_file(fallback_name);
    if (result != BOOT_OK) {
        fatal_blink(result);
    }

    clear_eeprom_filename();
    start_application();
}
