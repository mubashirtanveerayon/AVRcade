#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include "u8g2.h"

#define ENGINE_BUTTON_A        (1U << 0)
#define ENGINE_BUTTON_X        (1U << 1)
#define ENGINE_BUTTON_Y        (1U << 2)
#define ENGINE_BUTTON_B        (1U << 3)
#define ENGINE_JOYSTICK_BUTTON (1U << 4)
#define ENGINE_BUTTON_PAUSE    (1U << 5)

/* Legacy button names for backward compatibility */
#define ENGINE_BUTTON_1        ENGINE_BUTTON_A
#define ENGINE_BUTTON_2        ENGINE_BUTTON_X
#define ENGINE_BUTTON_3        ENGINE_BUTTON_Y
#define ENGINE_BUTTON_4        ENGINE_BUTTON_B

typedef struct {
    uint8_t down;
    uint8_t pressed;
    uint8_t released;
    uint16_t joystick_x;
    uint16_t joystick_y;
} EngineInput;

/* Implement these three functions in the game's source files. */
void game_init(void);
void game_update(const EngineInput *input, uint16_t delta_ms);
void game_render(u8g2_t *display);

const EngineInput *engine_input(void);
uint8_t engine_button_down(uint8_t button_mask);
uint8_t engine_button_pressed(uint8_t button_mask);
uint8_t engine_button_released(uint8_t button_mask);

/* Returns -1, 0, or +1 after applying the configured dead zone. */
int8_t engine_joystick_x_direction(void);
int8_t engine_joystick_y_direction(void);

uint32_t engine_millis(void);
uint32_t engine_frame_count(void);
uint16_t engine_frame_period_ms(void);

/* Non-blocking passive-buzzer control. Frequency 0 stops the tone. */
void engine_tone_start(uint16_t frequency_hz);
void engine_tone_stop(void);

/* Pause state control (toggled automatically via INT1 interrupt). */
uint8_t engine_is_paused(void);
void engine_set_paused(uint8_t paused);
void engine_toggle_pause(void);

#endif
