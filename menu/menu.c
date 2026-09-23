#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>

#include "pff.h"
#include "u8g2.h"

/*
 * ATmega32A SD game-selection menu
 *
 * OLED:    DC=PB0, RESET=PB1, CS=PB3, MOSI=PB5, SCK=PB7
 * SD:      CS=PB4, MOSI=PB5, MISO=PB6, SCK=PB7
 * Button A: PD0 to GND (launch selected game)
 * Joystick VRx: PA0/ADC0 (left/right selection)
 *
 * EEPROM bytes 0..8 contain only the null-terminated 8-character FAT
 * filename stem. The bootloader appends ".BIN" before opening the file.
 */

#define OLED_DC       PB0
#define OLED_RESET    PB1
#define OLED_CS       PB3
#define SD_CS         PB4
#define SPI_MOSI      PB5
#define SPI_MISO      PB6
#define SPI_SCK       PB7

#define BUTTON_A      PD0
#define BUTTON_SAVE   BUTTON_A

#define JOYSTICK_X_CHANNEL       0U
#define JOYSTICK_LEFT_THRESHOLD  340U
#define JOYSTICK_RIGHT_THRESHOLD 684U

#define EEPROM_NAME_ADDRESS 0U
#define EEPROM_STEM_SIZE    9U
#define FILE_NAME_SIZE      13U
#define MAX_GAME_FILES      24U
#define MAX_DIRECTORY_ENTRIES 1024U
#define SD_RECOVERY_BYTES 20U

#define SELECT_FIRST     0
#define SELECT_PREVIOUS -1
#define SELECT_NEXT      1

static FATFS file_system;
static DIR root_directory;
static FILINFO file_info;
static u8g2_t u8g2;

static char selected_file[FILE_NAME_SIZE] = "NO BIN FILES";
static char game_files[MAX_GAME_FILES][FILE_NAME_SIZE];
static uint16_t selected_index = 0U;
static uint16_t game_count = 0U;
static uint8_t sd_ready = 0U;
static uint8_t file_selected = 0U;

/* Allow this application to start safely after a watchdog reset too. */
void disable_watchdog_early(void)
    __attribute__((naked, section(".init3"), used));

void disable_watchdog_early(void)
{
    MCUSR = 0;
    wdt_disable();
}

/* diskio.c also uses this function for the shared SPI bus. */
uint8_t spi_transfer(uint8_t value)
{
    SPDR = value;
    while ((SPSR & (1U << SPIF)) == 0U) {
    }
    return SPDR;
}

static void spi_init(void)
{
    /* PB4/SS must remain an output or hardware SPI can enter slave mode. */
    DDRB |= (1U << OLED_DC) | (1U << OLED_RESET) | (1U << OLED_CS) |
            (1U << SD_CS) | (1U << SPI_MOSI) | (1U << SPI_SCK);
    DDRB &= (uint8_t)~(1U << SPI_MISO);

    PORTB |= (1U << OLED_CS) | (1U << SD_CS) | (1U << OLED_RESET);
    PORTB &= (uint8_t)~(1U << OLED_DC);

    /* Master, mode 0, fCPU/4 = 2 MHz. */
    SPCR = (1U << SPE) | (1U << MSTR);
    SPSR = 0U;
}

static void spi_set_slow(void)
{
    /* Mode 0, master, fCPU/32 = 250 kHz for SD-card initialization. */
    SPCR = (1U << SPE) | (1U << MSTR) | (1U << SPR1);
    SPSR = (1U << SPI2X);
}

static void spi_set_fast(void)
{
    /* Mode 0, master, fCPU/4 = 2 MHz for the OLED and normal SD reads. */
    SPCR = (1U << SPE) | (1U << MSTR);
    SPSR = 0U;
}

static void prepare_sd_mount_attempt(void)
{
    uint8_t count;

    /*
     * Deselect both devices before touching the shared bus. Slow recovery
     * clocks release a card left mid-command by a reset, then PetitFS sends
     * CMD0 and performs a fresh initialization at a legal SD init speed.
     */
    PORTB |= (1U << OLED_CS) | (1U << SD_CS);
    spi_set_slow();
    _delay_ms(2);

    for (count = 0U; count < SD_RECOVERY_BYTES; count++) {
        spi_transfer(0xFFU);
    }
}

/* u8g2 byte callback for the OLED on the same hardware SPI bus as the SD. */
static uint8_t oled_byte_callback(u8x8_t *u8x8, uint8_t message,
                                  uint8_t argument, void *data_pointer)
{
    uint8_t *data = (uint8_t *)data_pointer;
    (void)u8x8;

    switch (message) {
    case U8X8_MSG_BYTE_INIT:
        spi_init();
        break;

    case U8X8_MSG_BYTE_SET_DC:
        if (argument != 0U) {
            PORTB |= (1U << OLED_DC);
        } else {
            PORTB &= (uint8_t)~(1U << OLED_DC);
        }
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        /* Never allow both shared-SPI devices to be selected together. */
        PORTB |= (1U << SD_CS);
        PORTB &= (uint8_t)~(1U << OLED_CS);
        break;

    case U8X8_MSG_BYTE_SEND:
        while (argument-- != 0U) {
            spi_transfer(*data++);
        }
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
        PORTB |= (1U << OLED_CS);
        break;

    default:
        return 0U;
    }

    return 1U;
}

static uint8_t oled_gpio_delay_callback(u8x8_t *u8x8, uint8_t message,
                                        uint8_t argument, void *data_pointer)
{
    (void)u8x8;
    (void)data_pointer;

    switch (message) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        spi_init();
        break;

    case U8X8_MSG_DELAY_NANO:
        /* Function-call overhead already exceeds this display delay. */
        break;

    case U8X8_MSG_DELAY_100NANO:
        while (argument-- != 0U) {
            _delay_us(1);
        }
        break;

    case U8X8_MSG_DELAY_10MICRO:
        while (argument-- != 0U) {
            _delay_us(10);
        }
        break;

    case U8X8_MSG_DELAY_MILLI:
        while (argument-- != 0U) {
            _delay_ms(1);
        }
        break;

    case U8X8_MSG_GPIO_RESET:
        if (argument != 0U) {
            PORTB |= (1U << OLED_RESET);
        } else {
            PORTB &= (uint8_t)~(1U << OLED_RESET);
        }
        break;

    case U8X8_MSG_GPIO_CS:
        if (argument != 0U) {
            PORTB |= (1U << OLED_CS);
        } else {
            PORTB &= (uint8_t)~(1U << OLED_CS);
        }
        break;

    case U8X8_MSG_GPIO_DC:
        if (argument != 0U) {
            PORTB |= (1U << OLED_DC);
        } else {
            PORTB &= (uint8_t)~(1U << OLED_DC);
        }
        break;

    default:
        break;
    }

    return 1U;
}

static void oled_init(void)
{
    /* A one-page buffer uses only 128 bytes of the ATmega32A's SRAM. */
    u8g2_Setup_ssd1309_128x64_noname0_1(
        &u8g2, U8G2_R0, oled_byte_callback, oled_gpio_delay_callback);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0U);
    u8g2_SetContrast(&u8g2, 0xCFU);
    u8g2_SetFontMode(&u8g2, 1U);
}

static void draw_centered(const char *text, uint8_t baseline)
{
    u8g2_uint_t width = u8g2_GetStrWidth(&u8g2, text);
    u8g2_uint_t x = 0U;

    if (width < 128U) {
        x = (u8g2_uint_t)((128U - width) / 2U);
    }
    u8g2_DrawStr(&u8g2, x, baseline, text);
}

static void draw_splash(uint8_t frame, const char *visible_title)
{
    uint8_t frame_width = (uint8_t)(20U + (uint8_t)(frame * 7U));
    uint8_t frame_x;
    uint8_t progress = (uint8_t)((uint16_t)frame * 106U / 14U);
    uint8_t title_x;
    u8g2_uint_t title_width;

    if (frame_width > 118U) {
        frame_width = 118U;
    }
    frame_x = (uint8_t)((128U - frame_width) / 2U);

    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetDrawColor(&u8g2, 1U);

        /* Moving pixel sparks give the boot logo an arcade feel. */
        u8g2_DrawPixel(&u8g2, (uint8_t)((frame * 7U) & 127U), 5U);
        u8g2_DrawPixel(&u8g2, (uint8_t)((93U + frame * 3U) & 127U), 57U);
        u8g2_DrawPixel(&u8g2, (uint8_t)((40U + frame * 5U) & 127U), 9U);
        u8g2_DrawPixel(&u8g2, (uint8_t)((121U - frame * 4U) & 127U), 52U);

        u8g2_DrawRFrame(&u8g2, frame_x, 12U, frame_width, 36U, 5U);

        u8g2_SetFont(&u8g2, u8g2_font_helvB14_tr);
        title_width = u8g2_GetStrWidth(&u8g2, "AVRcade");
        title_x = (uint8_t)((128U - title_width) / 2U);
        u8g2_DrawStr(&u8g2, title_x, 36U, visible_title);

        u8g2_DrawFrame(&u8g2, 10U, 61U, 108U, 3U);
        if (progress != 0U) {
            u8g2_DrawBox(&u8g2, 11U, 62U, progress, 1U);
        }
    } while (u8g2_NextPage(&u8g2) != 0U);
}

static void show_startup_animation(void)
{
    static const char console_name[] = "AVRcade";
    char visible_title[sizeof(console_name)];
    uint8_t frame;
    uint8_t visible_count;

    memset(visible_title, 0, sizeof(visible_title));

    for (frame = 0U; frame <= 14U; frame++) {
        visible_count = (uint8_t)(frame / 2U + 1U);
        if (visible_count > (uint8_t)(sizeof(console_name) - 1U)) {
            visible_count = (uint8_t)(sizeof(console_name) - 1U);
        }

        memcpy(visible_title, console_name, visible_count);
        visible_title[visible_count] = '\0';
        draw_splash(frame, visible_title);
        _delay_ms(45);
    }

    _delay_ms(250);
}

static void draw_header(void)
{
    char position[12];
    char *cursor = position;
    uint16_t value;
    char reverse[5];
    uint8_t digits;

    u8g2_DrawBox(&u8g2, 0U, 0U, 128U, 14U);
    u8g2_SetDrawColor(&u8g2, 0U);
    u8g2_SetFont(&u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(&u8g2, 4U, 11U, "AVRcade");

    if (file_selected != 0U) {
        value = selected_index;
        digits = 0U;
        do {
            reverse[digits++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value != 0U && digits < sizeof(reverse));
        while (digits != 0U) {
            *cursor++ = reverse[--digits];
        }
        *cursor++ = '/';

        value = game_count;
        digits = 0U;
        do {
            reverse[digits++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value != 0U && digits < sizeof(reverse));
        while (digits != 0U) {
            *cursor++ = reverse[--digits];
        }
        *cursor = '\0';

        u8g2_DrawStr(&u8g2,
                     (u8g2_uint_t)(124U - u8g2_GetStrWidth(&u8g2, position)),
                     11U, position);
    }

    u8g2_SetDrawColor(&u8g2, 1U);
}

static uint8_t make_filename_stem(const char *filename, char *stem)
{
    uint8_t index;

    for (index = 0U; index < EEPROM_STEM_SIZE - 1U; index++) {
        char character = filename[index];

        if (character == '.') {
            if (index == 0U) {
                return 0U;
            }
            stem[index] = '\0';
            return 1U;
        }
        if (character == '\0') {
            return 0U;
        }
        stem[index] = character;
    }

    if (filename[EEPROM_STEM_SIZE - 1U] == '.') {
        stem[EEPROM_STEM_SIZE - 1U] = '\0';
        return 1U;
    }

    return 0U;
}

static void draw_menu_screen(void)
{
    char display_name[EEPROM_STEM_SIZE] = {0};

    if (file_selected != 0U) {
        make_filename_stem(selected_file, display_name);
    }

    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetDrawColor(&u8g2, 1U);
        draw_header();

        if (sd_ready != 0U && file_selected != 0U) {
            u8g2_DrawRFrame(&u8g2, 4U, 18U, 120U, 29U, 4U);

            /* Left and right navigation chevrons. */
            u8g2_DrawLine(&u8g2, 12U, 29U, 7U, 33U);
            u8g2_DrawLine(&u8g2, 7U, 33U, 12U, 37U);
            u8g2_DrawLine(&u8g2, 116U, 29U, 121U, 33U);
            u8g2_DrawLine(&u8g2, 121U, 33U, 116U, 37U);

            u8g2_SetFont(&u8g2, u8g2_font_helvB10_tr);
            draw_centered(display_name, 37U);

            u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
            u8g2_DrawStr(&u8g2, 3U, 59U, "JOY  < >");
            u8g2_DrawStr(&u8g2, 76U, 59U, "A SELECT");
        } else {
            u8g2_DrawRFrame(&u8g2, 6U, 20U, 116U, 27U, 4U);
            u8g2_SetFont(&u8g2, u8g2_font_6x12_tr);
            draw_centered(selected_file, 37U);
            u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
            if (sd_ready != 0U) {
                draw_centered("ADD .BIN FILES TO SD", 59U);
            } else {
                draw_centered("CHECK SD CARD", 59U);
            }
        }
    } while (u8g2_NextPage(&u8g2) != 0U);
}

static void draw_launch_screen(uint8_t progress)
{
    char display_name[EEPROM_STEM_SIZE] = {0};
    uint8_t width = (uint8_t)((uint16_t)progress * 104U / 7U);

    make_filename_stem(selected_file, display_name);

    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetDrawColor(&u8g2, 1U);
        draw_header();
        u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
        draw_centered("LOADING", 26U);
        u8g2_SetFont(&u8g2, u8g2_font_helvB10_tr);
        draw_centered(display_name, 43U);
        u8g2_DrawRFrame(&u8g2, 10U, 52U, 108U, 8U, 3U);
        if (width != 0U) {
            u8g2_DrawBox(&u8g2, 12U, 54U, width, 4U);
        }
    } while (u8g2_NextPage(&u8g2) != 0U);
}

static void show_launch_animation(void)
{
    uint8_t progress;

    for (progress = 0U; progress <= 7U; progress++) {
        draw_launch_screen(progress);
        _delay_ms(35);
    }
}

static void button_init(void)
{
    DDRD &= (uint8_t)~(1U << BUTTON_SAVE);
    PORTD |= (1U << BUTTON_SAVE);
}

static uint8_t save_button_pressed(void)
{
    return (PIND & (1U << BUTTON_SAVE)) == 0U;
}

static void adc_init(void)
{
    /* AVCC reference; ADC clock = 8 MHz / 64 = 125 kHz. */
    ADMUX = (1U << REFS0) | JOYSTICK_X_CHANNEL;
    ADCSRA = (1U << ADEN) | (1U << ADPS2) | (1U << ADPS1);
    _delay_ms(2);
}

static uint16_t read_joystick_x(void)
{
    ADMUX = (uint8_t)((ADMUX & 0xE0U) | JOYSTICK_X_CHANNEL);
    ADCSRA |= (1U << ADSC);
    while ((ADCSRA & (1U << ADSC)) != 0U) {
    }
    return ADC;
}

static int8_t read_joystick_direction(void)
{
    uint16_t x = read_joystick_x();

    if (x < JOYSTICK_LEFT_THRESHOLD) {
        return SELECT_PREVIOUS;
    }
    if (x > JOYSTICK_RIGHT_THRESHOLD) {
        return SELECT_NEXT;
    }
    return SELECT_FIRST;
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
        return 0U;
    }

    return name[length - 4U] == '.' &&
           upper_ascii(name[length - 3U]) == 'B' &&
           upper_ascii(name[length - 2U]) == 'I' &&
           upper_ascii(name[length - 1U]) == 'N';
}

static void copy_name(char *destination, const char *source)
{
    uint8_t index;

    for (index = 0U; index < FILE_NAME_SIZE - 1U && source[index] != '\0';
         index++) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

/* Read the directory once, before OLED activity, and retain the menu in SRAM. */
static uint8_t cache_game_files(void)
{
    uint16_t examined = 0U;
    uint8_t reached_end = 0U;
    FRESULT result;

    game_count = 0U;
    result = pf_opendir(&root_directory, "");
    if (result != FR_OK) {
        copy_name(selected_file, "ROOT ERROR");
        sd_ready = 0U;
        selected_index = 0U;
        return 0U;
    }

    while (examined < MAX_DIRECTORY_ENTRIES) {
        result = pf_readdir(&root_directory, &file_info);
        if (result != FR_OK) {
            copy_name(selected_file, "READ ERROR");
            sd_ready = 0U;
            game_count = 0U;
            selected_index = 0U;
            return 0U;
        }

        if (file_info.fname[0] == '\0') {
            reached_end = 1U;
            break;
        }
        examined++;

        if ((file_info.fattrib & (AM_DIR | AM_HID | AM_SYS | AM_VOL)) != 0U ||
            is_bin_filename(file_info.fname) == 0U) {
            continue;
        }

        if (game_count < MAX_GAME_FILES) {
            copy_name(game_files[game_count], file_info.fname);
            game_count++;
        }
    }

    if (reached_end == 0U) {
        copy_name(selected_file, "TOO MANY ENT");
        game_count = 0U;
        selected_index = 0U;
        return 0U;
    }

    if (game_count == 0U) {
        copy_name(selected_file, "NO BIN FILES");
        selected_index = 0U;
        return 0U;
    }

    selected_index = 1U;
    copy_name(selected_file, game_files[0]);
    return 1U;
}

/* No SD-card access occurs while navigating the cached list. */
static uint8_t select_cached_game(int8_t direction)
{
    if (game_count == 0U) {
        return 0U;
    }

    if (direction == SELECT_PREVIOUS) {
        if (selected_index <= 1U) {
            selected_index = game_count;
        } else {
            selected_index--;
        }
    } else if (direction == SELECT_NEXT) {
        if (selected_index >= game_count) {
            selected_index = 1U;
        } else {
            selected_index++;
        }
    }

    copy_name(selected_file, game_files[selected_index - 1U]);
    return 1U;
}

static uint8_t initialize_sd(void)
{
    FRESULT result;

    prepare_sd_mount_attempt();
    result = pf_mount(&file_system);

    PORTB |= (1U << OLED_CS) | (1U << SD_CS);
    spi_set_fast();

    if (result == FR_OK) {
        sd_ready = 1U;
        return cache_game_files();
    }

    /* Keep the exact PetitFS failure class visible for hardware diagnosis. */
    if (result == FR_NOT_READY) {
        copy_name(selected_file, "SD INIT E2");
    } else if (result == FR_DISK_ERR) {
        copy_name(selected_file, "SD READ E1");
    } else if (result == FR_NO_FILESYSTEM) {
        copy_name(selected_file, "FAT ERROR E6");
    } else {
        copy_name(selected_file, "SD MOUNT ERR");
    }
    sd_ready = 0U;
    return 0U;
}

static uint8_t save_selected_filename_stem(void)
{
    char stored_stem[EEPROM_STEM_SIZE] = {0};
    uint8_t index;

    if (make_filename_stem(selected_file, stored_stem) == 0U) {
        return 0U;
    }

    for (index = 0U; stored_stem[index] != '\0'; index++) {
        stored_stem[index] = upper_ascii(stored_stem[index]);
    }

    eeprom_update_block(stored_stem,
                        (void *)(uintptr_t)EEPROM_NAME_ADDRESS,
                        EEPROM_STEM_SIZE);
    eeprom_busy_wait();
    return 1U;
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

int main(void)
{
    uint8_t previous_save;
    uint8_t save_pressed;
    uint8_t joystick_armed = 0U;
    int8_t direction;

    spi_init();
    button_init();
    adc_init();

    /*
     * Mount and scan the card before u8g2 resets or clocks the OLED. This
     * preserves the same proven SD-first startup order as the bootloader.
     */
    file_selected = initialize_sd();

    oled_init();

    show_startup_animation();
    draw_menu_screen();

    previous_save = save_button_pressed();

    while (1) {
        direction = read_joystick_direction();
        if (direction == SELECT_FIRST) {
            joystick_armed = 1U;
        } else if (joystick_armed != 0U && sd_ready != 0U) {
            file_selected = select_cached_game(direction);
            draw_menu_screen();
            joystick_armed = 0U;
        }

        save_pressed = save_button_pressed();
        if (save_pressed != 0U && previous_save == 0U &&
            sd_ready != 0U && file_selected != 0U) {
            if (save_selected_filename_stem() != 0U) {
                show_launch_animation();
                u8g2_SetPowerSave(&u8g2, 1U);
                PORTB |= (1U << OLED_CS) | (1U << SD_CS);
                _delay_ms(10);
                restart_mcu();
            }
        }

        previous_save = save_pressed;
        _delay_ms(25);
    }
}
