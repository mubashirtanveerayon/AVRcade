#include "engine_config.h"
#include "engine.h"
#include "engine_platform.h"

#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>

static u8g2_t display;
static volatile uint32_t millisecond_counter = 0;
static volatile uint8_t engine_is_paused_state = 0;
static volatile uint32_t last_pause_interrupt_time = 0;

ISR(TIMER0_COMP_vect)
{
    millisecond_counter++;
}

ISR(INT1_vect)
{
    uint32_t now = millisecond_counter;
    /* 200 ms software debounce for pause toggle */
    if ((uint32_t)(now - last_pause_interrupt_time) >= 200U) {
        engine_is_paused_state = (engine_is_paused_state == 0U) ? 1U : 0U;
        last_pause_interrupt_time = now;
    }
}

static void write_pin(volatile uint8_t *port, uint8_t pin, uint8_t level)
{
    if (level != 0U) {
        *port |= (1U << pin);
    } else {
        *port &= (uint8_t)~(1U << pin);
    }
}

static uint8_t spi_transfer(uint8_t value)
{
    SPDR = value;
    while ((SPSR & (1U << SPIF)) == 0U) {
    }
    return SPDR;
}

static uint8_t engine_u8x8_byte_hw_spi(u8x8_t *u8x8,
                                       uint8_t message,
                                       uint8_t argument,
                                       void *data_pointer)
{
    uint8_t *data;

    switch (message) {
        case U8X8_MSG_BYTE_INIT:
            ENGINE_SPI_DDR |= (1U << ENGINE_SPI_MOSI_PIN) |
                              (1U << ENGINE_SPI_SCK_PIN);
            ENGINE_SPI_DDR &= (uint8_t)~(1U << ENGINE_SPI_MISO_PIN);

            ENGINE_OLED_DDR |= (1U << ENGINE_OLED_DC_PIN) |
                               (1U << ENGINE_OLED_RESET_PIN) |
                               (1U << ENGINE_OLED_CS_PIN);
            ENGINE_SD_DDR |= (1U << ENGINE_SD_CS_PIN);

            ENGINE_OLED_PORT |= (1U << ENGINE_OLED_CS_PIN) |
                                (1U << ENGINE_OLED_RESET_PIN);
            ENGINE_SD_PORT |= (1U << ENGINE_SD_CS_PIN);

            /* Hardware SPI master, mode 0, MSB first, F_CPU/4 = 2 MHz. */
            SPCR = (1U << SPE) | (1U << MSTR);
            SPSR = 0;
            break;

        case U8X8_MSG_BYTE_SET_DC:
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_DC_PIN, argument);
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            ENGINE_SD_PORT |= (1U << ENGINE_SD_CS_PIN);
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_CS_PIN,
                      u8x8->display_info->chip_enable_level);
            u8x8->gpio_and_delay_cb(
                u8x8,
                U8X8_MSG_DELAY_NANO,
                u8x8->display_info->post_chip_enable_wait_ns,
                0
            );
            break;

        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)data_pointer;
            while (argument-- != 0U) {
                spi_transfer(*data++);
            }
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            u8x8->gpio_and_delay_cb(
                u8x8,
                U8X8_MSG_DELAY_NANO,
                u8x8->display_info->pre_chip_disable_wait_ns,
                0
            );
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_CS_PIN,
                      u8x8->display_info->chip_disable_level);
            break;

        default:
            return 0;
    }

    return 1;
}

static uint8_t engine_u8x8_gpio_and_delay(u8x8_t *u8x8,
                                          uint8_t message,
                                          uint8_t argument,
                                          void *data_pointer)
{
    (void)data_pointer;

    switch (message) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            ENGINE_OLED_DDR |= (1U << ENGINE_OLED_DC_PIN) |
                               (1U << ENGINE_OLED_RESET_PIN) |
                               (1U << ENGINE_OLED_CS_PIN);
            ENGINE_SD_DDR |= (1U << ENGINE_SD_CS_PIN);
            ENGINE_OLED_PORT |= (1U << ENGINE_OLED_CS_PIN) |
                                (1U << ENGINE_OLED_RESET_PIN);
            ENGINE_SD_PORT |= (1U << ENGINE_SD_CS_PIN);
            break;

        case U8X8_MSG_DELAY_NANO:
            if (argument != 0U) __asm__ __volatile__("nop");
            break;

        case U8X8_MSG_DELAY_100NANO:
            while (argument-- != 0U) __asm__ __volatile__("nop");
            break;

        case U8X8_MSG_DELAY_10MICRO:
            while (argument-- != 0U) _delay_us(10);
            break;

        case U8X8_MSG_DELAY_MILLI:
            while (argument-- != 0U) _delay_ms(1);
            break;

        case U8X8_MSG_GPIO_RESET:
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_RESET_PIN, argument);
            break;

        case U8X8_MSG_GPIO_CS:
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_CS_PIN, argument);
            break;

        case U8X8_MSG_GPIO_DC:
            write_pin(&ENGINE_OLED_PORT, ENGINE_OLED_DC_PIN, argument);
            break;

        default:
            u8x8_SetGPIOResult(u8x8, 1);
            break;
    }

    return 1;
}

static void display_init(void)
{
#if ENGINE_SSD1309_VARIANT == 2U
    #if ENGINE_DISPLAY_BUFFER_MODE == 1U
    u8g2_Setup_ssd1309_128x64_noname2_1(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #elif ENGINE_DISPLAY_BUFFER_MODE == 2U
    u8g2_Setup_ssd1309_128x64_noname2_2(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #else
    u8g2_Setup_ssd1309_128x64_noname2_f(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #endif
#else
    #if ENGINE_DISPLAY_BUFFER_MODE == 1U
    u8g2_Setup_ssd1309_128x64_noname0_1(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #elif ENGINE_DISPLAY_BUFFER_MODE == 2U
    u8g2_Setup_ssd1309_128x64_noname0_2(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #else
    u8g2_Setup_ssd1309_128x64_noname0_f(
        &display, ENGINE_DISPLAY_ROTATION,
        engine_u8x8_byte_hw_spi, engine_u8x8_gpio_and_delay);
    #endif
#endif

    u8g2_InitDisplay(&display);
    u8g2_SetPowerSave(&display, 0);
    u8g2_SetContrast(&display, ENGINE_OLED_CONTRAST);
}

static void timer_init(void)
{
    /* Timer0 CTC: 8 MHz / 64 / (124 + 1) = 1000 interrupts/second. */
    TCCR0 = (1U << WGM01) | (1U << CS01) | (1U << CS00);
    OCR0 = (uint8_t)(F_CPU / 64UL / 1000UL - 1UL);
    TCNT0 = 0;
    TIMSK |= (1U << OCIE0);
}

static void buttons_init(void)
{
    /* Set button pins as inputs */
    ENGINE_BUTTON_A_DDR &= (uint8_t)~(1U << ENGINE_BUTTON_A_PIN);
    ENGINE_BUTTON_X_DDR &= (uint8_t)~(1U << ENGINE_BUTTON_X_PIN);
    ENGINE_BUTTON_Y_DDR &= (uint8_t)~(1U << ENGINE_BUTTON_Y_PIN);
    ENGINE_BUTTON_B_DDR &= (uint8_t)~(1U << ENGINE_BUTTON_B_PIN);
    ENGINE_JOYSTICK_SWITCH_DDR &=
        (uint8_t)~(1U << ENGINE_JOYSTICK_SWITCH_PIN);
    ENGINE_BUTTON_PAUSE_DDR &= (uint8_t)~(1U << ENGINE_BUTTON_PAUSE_PIN);

    /* Enable internal pull-up resistors (active low) */
    ENGINE_BUTTON_A_PORT |= (1U << ENGINE_BUTTON_A_PIN);
    ENGINE_BUTTON_X_PORT |= (1U << ENGINE_BUTTON_X_PIN);
    ENGINE_BUTTON_Y_PORT |= (1U << ENGINE_BUTTON_Y_PIN);
    ENGINE_BUTTON_B_PORT |= (1U << ENGINE_BUTTON_B_PIN);
    ENGINE_JOYSTICK_SWITCH_PORT |= (1U << ENGINE_JOYSTICK_SWITCH_PIN);
    ENGINE_BUTTON_PAUSE_PORT |= (1U << ENGINE_BUTTON_PAUSE_PIN);

    /* Configure INT1 (PD3 / physical pin 17) for falling-edge interrupt */
    MCUCR |= (1U << ISC11);
    MCUCR &= (uint8_t)~(1U << ISC10);
    GIFR |= (1U << INTF1);
    GICR |= (1U << INT1);
}

static uint8_t read_buttons(void)
{
    uint8_t result = 0;

    if ((ENGINE_BUTTON_A_PIN_REGISTER & (1U << ENGINE_BUTTON_A_PIN)) == 0U)
        result |= ENGINE_BUTTON_A;
    if ((ENGINE_BUTTON_X_PIN_REGISTER & (1U << ENGINE_BUTTON_X_PIN)) == 0U)
        result |= ENGINE_BUTTON_X;
    if ((ENGINE_BUTTON_Y_PIN_REGISTER & (1U << ENGINE_BUTTON_Y_PIN)) == 0U)
        result |= ENGINE_BUTTON_Y;
    if ((ENGINE_BUTTON_B_PIN_REGISTER & (1U << ENGINE_BUTTON_B_PIN)) == 0U)
        result |= ENGINE_BUTTON_B;
    if ((ENGINE_JOYSTICK_SWITCH_PIN_REGISTER &
         (1U << ENGINE_JOYSTICK_SWITCH_PIN)) == 0U)
        result |= ENGINE_JOYSTICK_BUTTON;
    if ((ENGINE_BUTTON_PAUSE_PIN_REGISTER & (1U << ENGINE_BUTTON_PAUSE_PIN)) == 0U)
        result |= ENGINE_BUTTON_PAUSE;

    return result;
}

static void adc_init(void)
{
    ENGINE_JOYSTICK_ADC_DDR &=
        (uint8_t)~((1U << ENGINE_JOYSTICK_X_PIN) |
                   (1U << ENGINE_JOYSTICK_Y_PIN));
    ENGINE_JOYSTICK_ADC_PORT &=
        (uint8_t)~((1U << ENGINE_JOYSTICK_X_PIN) |
                   (1U << ENGINE_JOYSTICK_Y_PIN));

    ADMUX = (1U << REFS0);
    ADCSRA = (1U << ADEN) | (1U << ADPS2) | (1U << ADPS1);
}

static uint16_t adc_conversion(void)
{
    uint8_t low;
    uint8_t high;

    ADCSRA |= (1U << ADSC);
    while ((ADCSRA & (1U << ADSC)) != 0U) {
    }

    low = ADCL;
    high = ADCH;
    return (uint16_t)low | ((uint16_t)high << 8);
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (uint8_t)((1U << REFS0) | (channel & 0x1FU));
    _delay_us(10);
    (void)adc_conversion();
    return adc_conversion();
}

static void buzzer_init(void)
{
    ENGINE_BUZZER_DDR |= (1U << ENGINE_BUZZER_PIN);
    ENGINE_BUZZER_PORT &= (uint8_t)~(1U << ENGINE_BUZZER_PIN);
    TCCR1A = 0;
    TCCR1B = 0;
}

void engine_tone_start(uint16_t frequency_hz)
{
    uint32_t compare_value;

    if (frequency_hz == 0U) {
        engine_tone_stop();
        return;
    }

    compare_value = F_CPU / (2UL * 8UL * frequency_hz);
    if (compare_value == 0UL) compare_value = 1UL;
    if (compare_value > 65536UL) compare_value = 65536UL;

    OCR1A = (uint16_t)(compare_value - 1UL);
    TCNT1 = 0;
    TCCR1A = (1U << COM1A0);
    TCCR1B = (1U << WGM12) | (1U << CS11);
}

void engine_tone_stop(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    ENGINE_BUZZER_PORT &= (uint8_t)~(1U << ENGINE_BUZZER_PIN);
}

void engine_platform_init(EngineInput *input)
{
    timer_init();
    buttons_init();
    adc_init();
    buzzer_init();
    display_init();

    input->down = read_buttons();
    input->pressed = 0;
    input->released = 0;
    input->joystick_x = adc_read(ENGINE_JOYSTICK_X_CHANNEL);
    input->joystick_y = adc_read(ENGINE_JOYSTICK_Y_CHANNEL);
}

void engine_platform_update_input(EngineInput *input)
{
    uint8_t previous = input->down;

    input->down = read_buttons();
    input->pressed = (uint8_t)(input->down & (uint8_t)~previous);
    input->released = (uint8_t)(previous & (uint8_t)~input->down);
    input->joystick_x = adc_read(ENGINE_JOYSTICK_X_CHANNEL);
    input->joystick_y = adc_read(ENGINE_JOYSTICK_Y_CHANNEL);
}

uint32_t engine_platform_millis(void)
{
    uint32_t value;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = millisecond_counter;
    }

    return value;
}

u8g2_t *engine_platform_display(void)
{
    return &display;
}

uint8_t engine_platform_is_paused(void)
{
    return engine_is_paused_state;
}

void engine_platform_set_paused(uint8_t paused)
{
    engine_is_paused_state = (paused != 0U) ? 1U : 0U;
}

void engine_platform_toggle_pause(void)
{
    engine_is_paused_state = (engine_is_paused_state == 0U) ? 1U : 0U;
}
