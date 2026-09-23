#include "engine_config.h"
#include "engine.h"
#include "engine_platform.h"

#include <util/delay.h>
#include <avr/interrupt.h>

#define ENGINE_FRAME_PERIOD ((uint16_t)(1000U / ENGINE_TARGET_FPS))

static EngineInput current_input;
static uint32_t completed_frames = 0;

const EngineInput *engine_input(void)
{
    return &current_input;
}

uint8_t engine_button_down(uint8_t button_mask)
{
    return (current_input.down & button_mask) != 0U;
}

uint8_t engine_button_pressed(uint8_t button_mask)
{
    return (current_input.pressed & button_mask) != 0U;
}

uint8_t engine_button_released(uint8_t button_mask)
{
    return (current_input.released & button_mask) != 0U;
}

int8_t engine_joystick_x_direction(void)
{
    if (current_input.joystick_x < ENGINE_JOYSTICK_LOW) return -1;
    if (current_input.joystick_x > ENGINE_JOYSTICK_HIGH) return 1;
    return 0;
}

int8_t engine_joystick_y_direction(void)
{
    if (current_input.joystick_y < ENGINE_JOYSTICK_LOW) return -1;
    if (current_input.joystick_y > ENGINE_JOYSTICK_HIGH) return 1;
    return 0;
}

uint32_t engine_millis(void)
{
    return engine_platform_millis();
}

uint32_t engine_frame_count(void)
{
    return completed_frames;
}

uint16_t engine_frame_period_ms(void)
{
    return ENGINE_FRAME_PERIOD;
}

uint8_t engine_is_paused(void)
{
    return engine_platform_is_paused();
}

void engine_set_paused(uint8_t paused)
{
    engine_platform_set_paused(paused);
}

void engine_toggle_pause(void)
{
    engine_platform_toggle_pause();
}

int main(void)
{
    uint32_t next_frame;
    uint32_t now;
    u8g2_t *display;

    engine_platform_init(&current_input);
    display = engine_platform_display();
    sei();

    game_init();
    next_frame = engine_platform_millis();

    while (1) {
        now = engine_platform_millis();

        if ((int32_t)(now - next_frame) >= 0) {
            next_frame += ENGINE_FRAME_PERIOD;

            /* Drop accumulated delay instead of running many catch-up frames. */
            if ((int32_t)(now - next_frame) >= (int32_t)ENGINE_FRAME_PERIOD) {
                next_frame = now + ENGINE_FRAME_PERIOD;
            }

            engine_platform_update_input(&current_input);

            if (engine_is_paused()) {
                /* While paused, stop sound output and keep next_frame aligned with current time */
                engine_tone_stop();
                next_frame = now + ENGINE_FRAME_PERIOD;
            } else {
                game_update(&current_input, ENGINE_FRAME_PERIOD);
            }

            /*
             * With page buffering, game_render is called multiple times.
             * Never update game state inside game_render.
             */
            u8g2_FirstPage(display);
            do {
                game_render(display);
                if (engine_is_paused()) {
                    /* Draw centered pause dialog banner */
                    u8g2_SetDrawColor(display, 0U);
                    u8g2_DrawBox(display, 34U, 22U, 60U, 18U);
                    u8g2_SetDrawColor(display, 1U);
                    u8g2_DrawFrame(display, 34U, 22U, 60U, 18U);
                    u8g2_SetFont(display, u8g2_font_5x7_tf);
                    u8g2_DrawStr(display, 47U, 34U, "PAUSED");
                }
            } while (u8g2_NextPage(display));

            completed_frames++;
        } else {
            _delay_ms(1);
        }
    }
}
