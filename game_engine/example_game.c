#include "engine.h"

static int16_t ball_x;
static int16_t ball_y;
static int8_t ball_dx;
static int8_t ball_dy;
static int16_t paddle_x;
static uint8_t filled_ball;

void game_init(void)
{
    ball_x = 36;
    ball_y = 30;
    ball_dx = 2;
    ball_dy = 1;
    paddle_x = 52;
    filled_ball = 1;
}

void game_update(const EngineInput *input, uint16_t delta_ms)
{
    int8_t joystick_direction;

    (void)delta_ms;

    joystick_direction = engine_joystick_x_direction();
    paddle_x += (int16_t)joystick_direction * 3;

    if (paddle_x < 2) paddle_x = 2;
    if (paddle_x > 102) paddle_x = 102;

    ball_x += ball_dx;
    ball_y += ball_dy;

    if (ball_x <= 4 || ball_x >= 123) {
        ball_dx = (int8_t)-ball_dx;
        engine_tone_start(900);
    }

    if (ball_y <= 13 || ball_y >= 58) {
        ball_dy = (int8_t)-ball_dy;
        engine_tone_start(1200);
    }

    if ((input->pressed & ENGINE_BUTTON_A) != 0U) {
        filled_ball ^= 1U;
        engine_tone_start(1600);
    }

    if ((input->pressed & ENGINE_BUTTON_X) != 0U) {
        ball_x = 36;
        ball_y = 30;
        ball_dx = 2;
        ball_dy = 1;
        engine_tone_start(600);
    }

    /* Each sound lasts approximately one frame. */
    if (input->pressed == 0U &&
        ball_x > 4 && ball_x < 123 && ball_y > 13 && ball_y < 58) {
        engine_tone_stop();
    }
}

void game_render(u8g2_t *display)
{
    u8g2_SetFont(display, u8g2_font_5x7_tf);
    u8g2_DrawStr(display, 3, 8, "ENGINE 30 FPS");
    u8g2_DrawFrame(display, 0, 10, 128, 54);
    u8g2_DrawBox(display, (u8g2_uint_t)paddle_x, 58, 24, 3);

    if (filled_ball) {
        u8g2_DrawDisc(display, (u8g2_uint_t)ball_x,
                     (u8g2_uint_t)ball_y, 3, U8G2_DRAW_ALL);
    } else {
        u8g2_DrawCircle(display, (u8g2_uint_t)ball_x,
                       (u8g2_uint_t)ball_y, 3, U8G2_DRAW_ALL);
    }
}
