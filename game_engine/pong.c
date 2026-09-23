#include "engine.h"

#include <stdint.h>

#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT       64
#define PLAYFIELD_TOP       10

#define PADDLE_WIDTH         3
#define PADDLE_HEIGHT       15
#define PADDLE_SPEED         3
#define LEFT_PADDLE_X        3
#define RIGHT_PADDLE_X     122

#define BALL_SIZE            3
#define FP_SHIFT             8
#define FP_ONE              (1 << FP_SHIFT)
#define BALL_START_SPEED_X  640  /* 2.5 pixels per frame. */
#define BALL_START_SPEED_Y  384  /* 1.5 pixels per frame. */
#define BALL_MAX_SPEED_X   1024  /* 4 pixels per frame. */

#define SERVE_DELAY_FRAMES  24   /* 0.8 seconds at 30 FPS. */
#define WINNING_SCORE        9

static int16_t left_paddle_y;
static int16_t right_paddle_y;

/* Ball position and velocity use signed 8.8 fixed-point values. */
static int32_t ball_x_fp;
static int32_t ball_y_fp;
static int16_t ball_vx_fp;
static int16_t ball_vy_fp;

static uint8_t left_score;
static uint8_t right_score;
static uint8_t serve_delay;
static uint8_t winner;
static uint8_t sound_frames;
static int8_t next_vertical_direction;

static int16_t clamp_paddle(int16_t position)
{
    const int16_t minimum = PLAYFIELD_TOP + 1;
    const int16_t maximum = SCREEN_HEIGHT - PADDLE_HEIGHT - 1;

    if (position < minimum) return minimum;
    if (position > maximum) return maximum;
    return position;
}

static uint8_t ranges_overlap(int16_t first_start,
                              int16_t first_size,
                              int16_t second_start,
                              int16_t second_size)
{
    return first_start < second_start + second_size &&
           first_start + first_size > second_start;
}

static void play_sound(uint16_t frequency, uint8_t frames)
{
    engine_tone_start(frequency);
    sound_frames = frames;
}

static void update_sound(void)
{
    if (sound_frames != 0U) {
        sound_frames--;
        if (sound_frames == 0U) {
            engine_tone_stop();
        }
    }
}

static void reset_ball(int8_t horizontal_direction)
{
    ball_x_fp = (int16_t)((SCREEN_WIDTH - BALL_SIZE) / 2) * FP_ONE;
    ball_y_fp = (int16_t)((PLAYFIELD_TOP + SCREEN_HEIGHT - BALL_SIZE) / 2) *
                FP_ONE;
    ball_vx_fp = (int16_t)horizontal_direction * BALL_START_SPEED_X;
    ball_vy_fp = (int16_t)next_vertical_direction * BALL_START_SPEED_Y;
    next_vertical_direction = (int8_t)-next_vertical_direction;
    serve_delay = SERVE_DELAY_FRAMES;
}

static void reset_match(void)
{
    left_paddle_y = (SCREEN_HEIGHT - PADDLE_HEIGHT + PLAYFIELD_TOP) / 2;
    right_paddle_y = left_paddle_y;
    left_score = 0;
    right_score = 0;
    winner = 0;
    next_vertical_direction = 1;
    reset_ball(1);
}

static void bounce_from_paddle(int16_t paddle_y, int8_t direction)
{
    int16_t speed_x;
    int16_t ball_center;
    int16_t paddle_center;
    int16_t offset;

    speed_x = ball_vx_fp < 0 ? (int16_t)-ball_vx_fp : ball_vx_fp;
    speed_x += 32; /* Increase by 0.125 pixels/frame on every paddle hit. */
    if (speed_x > BALL_MAX_SPEED_X) speed_x = BALL_MAX_SPEED_X;
    ball_vx_fp = (int16_t)direction * speed_x;

    ball_center = (int16_t)(ball_y_fp >> FP_SHIFT) + BALL_SIZE / 2;
    paddle_center = paddle_y + PADDLE_HEIGHT / 2;
    offset = ball_center - paddle_center;

    /* Contact position controls the outgoing vertical direction. */
    ball_vy_fp = offset * 72;
    if (ball_vy_fp > 576) ball_vy_fp = 576;
    if (ball_vy_fp < -576) ball_vy_fp = -576;

    if (ball_vy_fp >= 0 && ball_vy_fp < 128) ball_vy_fp = 128;
    if (ball_vy_fp < 0 && ball_vy_fp > -128) ball_vy_fp = -128;

    play_sound(1450, 2);
}

static void award_point(uint8_t player)
{
    if (player == 1U) {
        left_score++;
        if (left_score >= WINNING_SCORE) {
            winner = 1;
        } else {
            reset_ball(1);
        }
    } else {
        right_score++;
        if (right_score >= WINNING_SCORE) {
            winner = 2;
        } else {
            reset_ball(-1);
        }
    }

    play_sound(winner != 0U ? 1800 : 350, winner != 0U ? 10 : 5);
}

void game_init(void)
{
    sound_frames = 0;
    engine_tone_stop();
    reset_match();
}

void game_update(const EngineInput *input, uint16_t delta_ms)
{
    int16_t ball_x;
    int16_t ball_y;

    (void)delta_ms;
    update_sound();

    if (winner != 0U) {
        if (input->pressed != 0U) {
            reset_match();
            play_sound(900, 3);
        }
        return;
    }

    int8_t joy_y = engine_joystick_y_direction();
    if (joy_y < 0) {
        left_paddle_y -= PADDLE_SPEED;
    } else if (joy_y > 0) {
        left_paddle_y += PADDLE_SPEED;
    }

    if ((input->down & ENGINE_BUTTON_Y) != 0U &&
        (input->down & ENGINE_BUTTON_A) == 0U) {
        right_paddle_y -= PADDLE_SPEED;
    } else if ((input->down & ENGINE_BUTTON_A) != 0U &&
               (input->down & ENGINE_BUTTON_Y) == 0U) {
        right_paddle_y += PADDLE_SPEED;
    }

    left_paddle_y = clamp_paddle(left_paddle_y);
    right_paddle_y = clamp_paddle(right_paddle_y);

    if (serve_delay != 0U) {
        serve_delay--;
        return;
    }

    ball_x_fp += ball_vx_fp;
    ball_y_fp += ball_vy_fp;
    ball_x = ball_x_fp >> FP_SHIFT;
    ball_y = ball_y_fp >> FP_SHIFT;

    if (ball_y <= PLAYFIELD_TOP) {
        ball_y_fp = PLAYFIELD_TOP * FP_ONE;
        if (ball_vy_fp < 0) ball_vy_fp = (int16_t)-ball_vy_fp;
        play_sound(720, 1);
    } else if (ball_y + BALL_SIZE >= SCREEN_HEIGHT) {
        ball_y_fp = (SCREEN_HEIGHT - BALL_SIZE) * FP_ONE;
        if (ball_vy_fp > 0) ball_vy_fp = (int16_t)-ball_vy_fp;
        play_sound(720, 1);
    }

    ball_x = ball_x_fp >> FP_SHIFT;
    ball_y = ball_y_fp >> FP_SHIFT;

    if (ball_vx_fp < 0 &&
        ball_x <= LEFT_PADDLE_X + PADDLE_WIDTH &&
        ball_x + BALL_SIZE >= LEFT_PADDLE_X &&
        ranges_overlap(ball_y, BALL_SIZE,
                       left_paddle_y, PADDLE_HEIGHT)) {
        ball_x_fp = (LEFT_PADDLE_X + PADDLE_WIDTH) * FP_ONE;
        bounce_from_paddle(left_paddle_y, 1);
    } else if (ball_vx_fp > 0 &&
               ball_x + BALL_SIZE >= RIGHT_PADDLE_X &&
               ball_x <= RIGHT_PADDLE_X + PADDLE_WIDTH &&
               ranges_overlap(ball_y, BALL_SIZE,
                              right_paddle_y, PADDLE_HEIGHT)) {
        ball_x_fp = (RIGHT_PADDLE_X - BALL_SIZE) * FP_ONE;
        bounce_from_paddle(right_paddle_y, -1);
    }

    ball_x = ball_x_fp >> FP_SHIFT;
    if (ball_x + BALL_SIZE < 0) {
        award_point(2);
    } else if (ball_x >= SCREEN_WIDTH) {
        award_point(1);
    }
}

static void draw_score(u8g2_t *display, uint8_t score, uint8_t x)
{
    char text[2];

    text[0] = (char)('0' + score);
    text[1] = '\0';
    u8g2_DrawStr(display, x, 8, text);
}

void game_render(u8g2_t *display)
{
    uint8_t dash_y;
    int16_t ball_x = ball_x_fp >> FP_SHIFT;
    int16_t ball_y = ball_y_fp >> FP_SHIFT;

    u8g2_SetFont(display, u8g2_font_5x7_tf);
    draw_score(display, left_score, 51);
    draw_score(display, right_score, 73);
    u8g2_DrawHLine(display, 0, PLAYFIELD_TOP - 1, SCREEN_WIDTH);

    for (dash_y = PLAYFIELD_TOP + 2; dash_y < SCREEN_HEIGHT; dash_y += 6) {
        u8g2_DrawVLine(display, 63, dash_y, 3);
    }

    u8g2_DrawBox(display, LEFT_PADDLE_X, (u8g2_uint_t)left_paddle_y,
                 PADDLE_WIDTH, PADDLE_HEIGHT);
    u8g2_DrawBox(display, RIGHT_PADDLE_X, (u8g2_uint_t)right_paddle_y,
                 PADDLE_WIDTH, PADDLE_HEIGHT);

    if (winner == 0U) {
        u8g2_DrawBox(display, (u8g2_uint_t)ball_x, (u8g2_uint_t)ball_y,
                     BALL_SIZE, BALL_SIZE);

        if (serve_delay != 0U) {
            u8g2_DrawStr(display, 51, 37, "READY");
        }
    } else {
        u8g2_DrawBox(display, 22, 25, 84, 20);
        u8g2_SetDrawColor(display, 0);
        u8g2_DrawStr(display, winner == 1U ? 34 : 31, 34,
                     winner == 1U ? "LEFT WINS" : "RIGHT WINS");
        u8g2_DrawStr(display, 31, 42, "PRESS BUTTON");
        u8g2_SetDrawColor(display, 1);
    }
}
