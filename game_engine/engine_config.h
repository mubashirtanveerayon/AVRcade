#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>

/* Timing and display configuration. */
#define ENGINE_TARGET_FPS             30U
#define ENGINE_DISPLAY_BUFFER_MODE    2U  /* 1=128 B, 2=256 B, 0=full 1024 B */
#define ENGINE_SSD1309_VARIANT        0U  /* 0=NONAME0, 2=NONAME2 offset */
#define ENGINE_DISPLAY_ROTATION       U8G2_R0
#define ENGINE_OLED_CONTRAST          0xCFU

/* OLED and shared hardware-SPI bus. */
#define ENGINE_SPI_DDR                DDRB
#define ENGINE_SPI_PORT               PORTB
#define ENGINE_SPI_MOSI_PIN           PB5
#define ENGINE_SPI_MISO_PIN           PB6
#define ENGINE_SPI_SCK_PIN            PB7

#define ENGINE_OLED_DDR               DDRB
#define ENGINE_OLED_PORT              PORTB
#define ENGINE_OLED_DC_PIN            PB0
#define ENGINE_OLED_RESET_PIN         PB1
#define ENGINE_OLED_CS_PIN            PB3

/* The SD card stays deselected while a game uses the OLED. */
#define ENGINE_SD_DDR                 DDRB
#define ENGINE_SD_PORT                PORTB
#define ENGINE_SD_CS_PIN              PB4

/* Active-low buttons, each wired between its MCU pin and ground. */
#define ENGINE_BUTTON_A_DDR            DDRD
#define ENGINE_BUTTON_A_PORT           PORTD
#define ENGINE_BUTTON_A_PIN_REGISTER   PIND
#define ENGINE_BUTTON_A_PIN            PD0

#define ENGINE_BUTTON_X_DDR            DDRD
#define ENGINE_BUTTON_X_PORT           PORTD
#define ENGINE_BUTTON_X_PIN_REGISTER   PIND
#define ENGINE_BUTTON_X_PIN            PD1

#define ENGINE_BUTTON_Y_DDR            DDRD
#define ENGINE_BUTTON_Y_PORT           PORTD
#define ENGINE_BUTTON_Y_PIN_REGISTER   PIND
#define ENGINE_BUTTON_Y_PIN            PD2

#define ENGINE_BUTTON_B_DDR            DDRD
#define ENGINE_BUTTON_B_PORT           PORTD
#define ENGINE_BUTTON_B_PIN_REGISTER   PIND
#define ENGINE_BUTTON_B_PIN            PD6

/* Pause button on INT1 (PD3 / physical pin 17) with external interrupt. */
#define ENGINE_BUTTON_PAUSE_DDR          DDRD
#define ENGINE_BUTTON_PAUSE_PORT         PORTD
#define ENGINE_BUTTON_PAUSE_PIN_REGISTER PIND
#define ENGINE_BUTTON_PAUSE_PIN          PD3

/* Legacy aliases for backward compatibility */
#define ENGINE_BUTTON1_DDR            ENGINE_BUTTON_A_DDR
#define ENGINE_BUTTON1_PORT           ENGINE_BUTTON_A_PORT
#define ENGINE_BUTTON1_PIN_REGISTER   ENGINE_BUTTON_A_PIN_REGISTER
#define ENGINE_BUTTON1_PIN            ENGINE_BUTTON_A_PIN

#define ENGINE_BUTTON2_DDR            ENGINE_BUTTON_X_DDR
#define ENGINE_BUTTON2_PORT           ENGINE_BUTTON_X_PORT
#define ENGINE_BUTTON2_PIN_REGISTER   ENGINE_BUTTON_X_PIN_REGISTER
#define ENGINE_BUTTON2_PIN            ENGINE_BUTTON_X_PIN

#define ENGINE_BUTTON3_DDR            ENGINE_BUTTON_Y_DDR
#define ENGINE_BUTTON3_PORT           ENGINE_BUTTON_Y_PORT
#define ENGINE_BUTTON3_PIN_REGISTER   ENGINE_BUTTON_Y_PIN_REGISTER
#define ENGINE_BUTTON3_PIN            ENGINE_BUTTON_Y_PIN

#define ENGINE_BUTTON4_DDR            ENGINE_BUTTON_B_DDR
#define ENGINE_BUTTON4_PORT           ENGINE_BUTTON_B_PORT
#define ENGINE_BUTTON4_PIN_REGISTER   ENGINE_BUTTON_B_PIN_REGISTER
#define ENGINE_BUTTON4_PIN            ENGINE_BUTTON_B_PIN

#define ENGINE_JOYSTICK_SWITCH_DDR          DDRD
#define ENGINE_JOYSTICK_SWITCH_PORT         PORTD
#define ENGINE_JOYSTICK_SWITCH_PIN_REGISTER PIND
#define ENGINE_JOYSTICK_SWITCH_PIN          PD4

/* Joystick analog axes. AVCC is used as the ADC reference. */
#define ENGINE_JOYSTICK_ADC_DDR       DDRA
#define ENGINE_JOYSTICK_ADC_PORT      PORTA
#define ENGINE_JOYSTICK_X_PIN         PA0
#define ENGINE_JOYSTICK_Y_PIN         PA1
#define ENGINE_JOYSTICK_X_CHANNEL     0U
#define ENGINE_JOYSTICK_Y_CHANNEL     1U
#define ENGINE_JOYSTICK_LOW           350U
#define ENGINE_JOYSTICK_HIGH          670U

/* Passive buzzer output driven by Timer1 OC1A. */
#define ENGINE_BUZZER_DDR             DDRD
#define ENGINE_BUZZER_PORT            PORTD
#define ENGINE_BUZZER_PIN             PD5

#if ENGINE_TARGET_FPS == 0U || ENGINE_TARGET_FPS > 100U
#error ENGINE_TARGET_FPS must be between 1 and 100
#endif

#if ENGINE_DISPLAY_BUFFER_MODE != 0U && \
    ENGINE_DISPLAY_BUFFER_MODE != 1U && \
    ENGINE_DISPLAY_BUFFER_MODE != 2U
#error ENGINE_DISPLAY_BUFFER_MODE must be 0, 1, or 2
#endif

#endif
