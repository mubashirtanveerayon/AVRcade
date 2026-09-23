#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

#include "pff.h"

/*
 * ATmega32A SD game-selection menu
 *
 * OLED: DC=PB0, RESET=PB1, CS=PB3, MOSI=PB5, SCK=PB7
 * SD:   CS=PB4, MOSI=PB5, MISO=PB6, SCK=PB7
 * B1:   PD0 to GND (cycle)
 * B2:   PD1 to GND (save)
 *
 * EEPROM bytes 0..12 contain one null-terminated 8.3 filename.
 */

#define OLED_DC       PB0
#define OLED_RESET    PB1
#define OLED_CS       PB3
#define SD_CS         PB4
#define SPI_MOSI      PB5
#define SPI_MISO      PB6
#define SPI_SCK       PB7

#define BUTTON_CYCLE  PD0
#define BUTTON_SAVE   PD1
#define BUTTON_MASK   ((1U << BUTTON_CYCLE) | (1U << BUTTON_SAVE))

#define FILE_NAME_SIZE 13U
#define MAX_DIRECTORY_ENTRIES 1024U

static FATFS file_system;
static DIR root_directory;
static FILINFO file_info;
static char selected_file[FILE_NAME_SIZE] = "NO BIN FILES";
static uint8_t sd_ready = 0;
static uint8_t file_selected = 0;

/* Allow this application to start safely after a watchdog reset too. */
void disable_watchdog_early(void)
    __attribute__((naked, section(".init3"), used));

void disable_watchdog_early(void)
{
    MCUSR = 0;
    wdt_disable();
}

static const uint8_t font5x7[42][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x08,0x04,0x08,0x10,0x08}  /* ~ */
};

/* diskio.c also uses this function for the shared SPI bus. */
uint8_t spi_transfer(uint8_t value)
{
    SPDR = value;
    while (!(SPSR & (1U << SPIF))) {
    }
    return SPDR;
}

static void spi_init(void)
{
    /* PB4 must remain an output or hardware SPI can enter slave mode. */
    DDRB |= (1U << OLED_DC) | (1U << OLED_RESET) | (1U << OLED_CS) |
            (1U << SD_CS) | (1U << SPI_MOSI) | (1U << SPI_SCK);
    DDRB &= ~(1U << SPI_MISO);

    PORTB |= (1U << OLED_CS) | (1U << SD_CS) | (1U << OLED_RESET);
    PORTB &= ~(1U << OLED_DC);

    /* Master, mode 0, fCPU/4 = 2 MHz. */
    SPCR = (1U << SPE) | (1U << MSTR);
    SPSR = 0;
}

static void oled_command(uint8_t command)
{
    PORTB &= ~(1U << OLED_DC);
    PORTB &= ~(1U << OLED_CS);
    spi_transfer(command);
    PORTB |= (1U << OLED_CS);
}

static void oled_data_begin(void)
{
    PORTB |= (1U << OLED_DC);
    PORTB &= ~(1U << OLED_CS);
}

static void oled_data_end(void)
{
    PORTB |= (1U << OLED_CS);
}

static void oled_set_position(uint8_t page, uint8_t column)
{
    oled_command((uint8_t)(0xB0U | (page & 7U)));
    oled_command((uint8_t)(0x00U | (column & 0x0FU)));
    oled_command((uint8_t)(0x10U | ((column >> 4) & 0x0FU)));
}

static void oled_clear_page(uint8_t page)
{
    uint8_t column;

    oled_set_position(page, 0);
    oled_data_begin();
    for (column = 0; column < 128; column++) {
        spi_transfer(0x00);
    }
    oled_data_end();
}

static uint8_t glyph_index(char character)
{
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }

    if (character >= '0' && character <= '9') {
        return (uint8_t)(1 + character - '0');
    }
    if (character >= 'A' && character <= 'Z') {
        return (uint8_t)(11 + character - 'A');
    }
    if (character == ':') {
        return 37;
    }
    if (character == '-') {
        return 38;
    }
    if (character == '.') {
        return 39;
    }
    if (character == '_') {
        return 40;
    }
    if (character == '~') {
        return 41;
    }
    return 0;
}

static void oled_putc(char character)
{
    uint8_t index = glyph_index(character);
    uint8_t column;

    oled_data_begin();
    for (column = 0; column < 5; column++) {
        spi_transfer(pgm_read_byte(&font5x7[index][column]));
    }
    spi_transfer(0x00);
    oled_data_end();
}

static void oled_init(void)
{
    uint8_t page;

    PORTB &= ~(1U << OLED_RESET);
    _delay_ms(20);
    PORTB |= (1U << OLED_RESET);
    _delay_ms(20);

    /* Typical initialization for a 128x64 SPI SSD1309 module. */
    oled_command(0xFD); oled_command(0x12); /* unlock commands */
    oled_command(0xAE);                    /* display off */
    oled_command(0xD5); oled_command(0xA0); /* clock */
    oled_command(0xA8); oled_command(0x3F); /* multiplex 1/64 */
    oled_command(0xD3); oled_command(0x00); /* display offset */
    oled_command(0x40);                    /* start line 0 */
    oled_command(0xA1);                    /* segment remap */
    oled_command(0xC8);                    /* reversed COM scan */
    oled_command(0xDA); oled_command(0x12); /* COM pins */
    oled_command(0x81); oled_command(0xCF); /* brighter contrast */
    oled_command(0xD9); oled_command(0x25); /* precharge */
    oled_command(0xDB); oled_command(0x34); /* VCOMH */
    oled_command(0xA4);                    /* use display RAM */
    oled_command(0xA6);                    /* normal, not inverted */
    oled_command(0xAF);                    /* display on */

    for (page = 0; page < 8; page++) {
        oled_clear_page(page);
    }
}

static void clear_screen(void)
{
    uint8_t page;

    for (page = 0; page < 8; page++) {
        oled_clear_page(page);
    }
}

static void show_line(uint8_t page, const char *text)
{
    uint8_t length = (uint8_t)strlen(text);

    oled_clear_page(page);
    oled_set_position(page, 0);

    if (length > 21U) {
        length = 21U;
    }

    while (length-- != 0) {
        oled_putc(*text++);
    }
}

static void buttons_init(void)
{
    DDRD &= (uint8_t)~BUTTON_MASK;
    PORTD |= BUTTON_MASK;
}

static uint8_t read_buttons(void)
{
    return (uint8_t)((~PIND) & BUTTON_MASK);
}

static uint8_t filename_is_valid(const char *name)
{
    uint8_t index;

    for (index = 0; index < FILE_NAME_SIZE; index++) {
        uint8_t character = (uint8_t)name[index];

        if (character == 0U) {
            return index != 0U;
        }

        if (character < 32U || character > 126U) {
            return 0;
        }
    }

    return 0;
}

static uint8_t load_saved_filename(char *name)
{
    eeprom_read_block(name, (const void *)0, FILE_NAME_SIZE);

    if (!filename_is_valid(name)) {
        name[0] = '\0';
        return 0;
    }

    return 1;
}

static void save_selected_filename(void)
{
    char stored_name[FILE_NAME_SIZE] = {0};

    strncpy(stored_name, selected_file, FILE_NAME_SIZE - 1U);
    eeprom_update_block(stored_name, (void *)0, FILE_NAME_SIZE);
}

static void restart_mcu(void) __attribute__((noreturn));

static void restart_mcu(void)
{
    /* The watchdog provides a real MCU reset and therefore enters BOOTRST. */
    cli();
    wdt_enable(WDTO_15MS);
    while (1) {
    }
}

static char upper_ascii(char character)
{
    if (character >= 'a' && character <= 'z') {
        return (char)(character - 'a' + 'A');
    }

    return character;
}

static uint8_t is_bin_filename(const char *name)
{
    uint8_t length = (uint8_t)strlen(name);

    if (length < 5U || length >= FILE_NAME_SIZE) {
        return 0;
    }

    if (name[length - 4U] != '.' ||
        upper_ascii(name[length - 3U]) != 'B' ||
        upper_ascii(name[length - 2U]) != 'I' ||
        upper_ascii(name[length - 1U]) != 'N') {
        return 0;
    }

    /* The selector itself is the bootloader's fallback, not a game choice. */
    // if (strcmp(name, "MENU.BIN") == 0) {
    //     return 0;
    // }

    return 1;
}

static uint8_t get_next_bin_file(void)
{
    uint16_t examined = 0;
    uint8_t wrapped = 0;
    FRESULT result;

    while (examined < MAX_DIRECTORY_ENTRIES) {
        result = pf_readdir(&root_directory, &file_info);

        if (result != FR_OK) {
            strcpy(selected_file, "READ ERROR");
            sd_ready = 0;
            return 0;
        }

        if (file_info.fname[0] == '\0') {
            if (wrapped) {
                strcpy(selected_file, "NO BIN FILES");
                return 0;
            }

            pf_readdir(&root_directory, 0);
            wrapped = 1;
            continue;
        }

        examined++;

        if ((file_info.fattrib & (AM_DIR | AM_HID | AM_SYS | AM_VOL)) == 0 &&
            is_bin_filename(file_info.fname)) {
            strncpy(selected_file, file_info.fname, FILE_NAME_SIZE - 1U);
            selected_file[FILE_NAME_SIZE - 1U] = '\0';
            return 1;
        }
    }

    strcpy(selected_file, "TOO MANY BIN");
    return 0;
}

static uint8_t initialize_sd(void)
{
    FRESULT result;

    result = pf_mount(&file_system);
    if (result != FR_OK) {
        strcpy(selected_file, "MOUNT FAILED");
        sd_ready = 0;
        return 0;
    }

    result = pf_opendir(&root_directory, "");
    if (result != FR_OK) {
        strcpy(selected_file, "ROOT ERROR");
        sd_ready = 0;
        return 0;
    }

    sd_ready = 1;
    return get_next_bin_file();
}

static void show_menu(const char *status)
{
    show_line(0, "GAME SELECTOR");
    show_line(1, "SD ROOT BIN FILES");
    show_line(2, "SELECTED:");
    show_line(3, selected_file);
    show_line(4, "");
    show_line(5, "B1:CYCLE");
    show_line(6, "B2:SAVE");
    show_line(7, status);
}

static void show_boot_selection(void)
{
    char saved_file[FILE_NAME_SIZE];
    uint8_t count;

    clear_screen();
    show_line(0, "GAME SELECTOR");
    show_line(2, "EEPROM SELECTION:");

    if (load_saved_filename(saved_file)) {
        show_line(3, saved_file);
    } else {
        show_line(3, "NO SAVED FILE");
    }

    for (count = 0; count < 15U; count++) {
        _delay_ms(100);
    }
}

int main(void)
{
    uint8_t buttons;
    uint8_t previous_buttons;
    uint8_t new_presses;

    spi_init();
    buttons_init();
    oled_init();

    show_boot_selection();

    file_selected = initialize_sd();
    show_menu(sd_ready ? (file_selected ? "READY" : "NO GAMES FOUND")
                       : "SD ERROR");

    previous_buttons = read_buttons();

    while (1) {
        buttons = read_buttons();
        new_presses = (uint8_t)(buttons & (uint8_t)~previous_buttons);

        if ((new_presses & (1U << BUTTON_CYCLE)) != 0U && sd_ready) {
            file_selected = get_next_bin_file();
            show_menu(file_selected ? "READY" : "NO GAMES FOUND");
        }

        if ((new_presses & (1U << BUTTON_SAVE)) != 0U &&
            sd_ready && file_selected) {
            save_selected_filename();
            show_menu("STARTING...");
            _delay_ms(250);
            restart_mcu();
        }

        previous_buttons = buttons;
        _delay_ms(40);
    }
}
