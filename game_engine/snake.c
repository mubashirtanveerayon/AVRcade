#include "engine.h"

#include <stdint.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define HUD_HEIGHT  9
#define CELL_SIZE   4
#define GRID_WIDTH  (SCREEN_WIDTH / CELL_SIZE)
#define GRID_HEIGHT ((SCREEN_HEIGHT - HUD_HEIGHT) / CELL_SIZE)
#define MAX_SNAKE_LENGTH 96

#define JOYSTICK_CENTER   512
#define JOYSTICK_DEADZONE 150
#define MOVE_PERIOD_MS    100

enum {
    DIRECTION_UP,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
    DIRECTION_RIGHT
};

static uint8_t snake_x[MAX_SNAKE_LENGTH];
static uint8_t snake_y[MAX_SNAKE_LENGTH];
static uint8_t snake_length;
static uint8_t direction;
static uint8_t pending_direction;
static uint8_t food_x;
static uint8_t food_y;
static uint8_t game_over;
static uint8_t sound_frames;
static uint16_t score;
static uint16_t movement_time;
static uint16_t rng_state;

static uint16_t random_next(void)
{
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return rng_state;
}

static void seed_random(void)
{
    const EngineInput *input;

    if (rng_state != 0U) return;

    input = engine_input();
    rng_state = (uint16_t)(input->joystick_x ^
                           (input->joystick_y << 5) ^
                           (uint16_t)engine_millis() ^ 0xB47DU);
    if (rng_state == 0U) rng_state = 0xB47DU;
}

static int16_t absolute16(int16_t value)
{
    return value < 0 ? (int16_t)-value : value;
}

static uint8_t directions_are_opposite(uint8_t first, uint8_t second)
{
    return ((first == DIRECTION_UP && second == DIRECTION_DOWN) ||
            (first == DIRECTION_DOWN && second == DIRECTION_UP) ||
            (first == DIRECTION_LEFT && second == DIRECTION_RIGHT) ||
            (first == DIRECTION_RIGHT && second == DIRECTION_LEFT));
}

static uint8_t cell_is_on_snake(uint8_t x, uint8_t y)
{
    uint8_t i;

    for (i = 0; i < snake_length; i++) {
        if (snake_x[i] == x && snake_y[i] == y) return 1U;
    }
    return 0U;
}

static uint8_t head_hits_body(int16_t x, int16_t y)
{
    uint8_t i;

    /* The last tail cell moves away during a normal step. */
    for (i = 0; i + 1U < snake_length; i++) {
        if (snake_x[i] == x && snake_y[i] == y) return 1U;
    }
    return 0U;
}

static void spawn_food(void)
{
    uint8_t x;
    uint8_t y;

    do {
        x = (uint8_t)(random_next() % GRID_WIDTH);
        y = (uint8_t)(random_next() % GRID_HEIGHT);
    } while (cell_is_on_snake(x, y) != 0U);

    food_x = x;
    food_y = y;
}

static void play_sound(uint16_t frequency, uint8_t frames)
{
    engine_tone_start(frequency);
    sound_frames = frames;
}

static void update_sound(void)
{
    if (sound_frames == 0U) return;

    sound_frames--;
    if (sound_frames == 0U) engine_tone_stop();
}

static void start_game(void)
{
    seed_random();

    snake_length = 3U;
    snake_x[0] = GRID_WIDTH / 2U;
    snake_y[0] = GRID_HEIGHT / 2U;
    snake_x[1] = snake_x[0] - 1U;
    snake_y[1] = snake_y[0];
    snake_x[2] = snake_x[0] - 2U;
    snake_y[2] = snake_y[0];

    direction = DIRECTION_RIGHT;
    pending_direction = DIRECTION_RIGHT;
    movement_time = 0U;
    score = 0U;
    game_over = 0U;
    sound_frames = 0U;
    engine_tone_stop();
    spawn_food();
}

static void read_joystick_direction(const EngineInput *input)
{
    int16_t x_offset;
    int16_t y_offset;
    uint8_t requested;

    /* Accept only one queued turn between two snake movements. */
    if (pending_direction != direction) return;

    x_offset = (int16_t)input->joystick_x - JOYSTICK_CENTER;
    y_offset = (int16_t)input->joystick_y - JOYSTICK_CENTER;

    if (absolute16(x_offset) < JOYSTICK_DEADZONE &&
        absolute16(y_offset) < JOYSTICK_DEADZONE) {
        return;
    }

    if (absolute16(x_offset) >= absolute16(y_offset)) {
        requested = x_offset < 0 ? DIRECTION_LEFT : DIRECTION_RIGHT;
    } else {
        requested = y_offset < 0 ? DIRECTION_UP : DIRECTION_DOWN;
    }

    if (directions_are_opposite(direction, requested) == 0U) {
        pending_direction = requested;
    }
}

static uint8_t move_snake(void)
{
    int16_t new_x = snake_x[0];
    int16_t new_y = snake_y[0];
    uint8_t ate;
    uint8_t i;

    direction = pending_direction;

    if (direction == DIRECTION_UP) {
        new_y--;
    } else if (direction == DIRECTION_DOWN) {
        new_y++;
    } else if (direction == DIRECTION_LEFT) {
        new_x--;
    } else {
        new_x++;
    }

    if (new_x < 0 || new_x >= GRID_WIDTH ||
        new_y < 0 || new_y >= GRID_HEIGHT) {
        return 1U;
    }

    if (head_hits_body(new_x, new_y) != 0U) return 1U;

    ate = (new_x == food_x && new_y == food_y);

    if (ate != 0U && snake_length < MAX_SNAKE_LENGTH) {
        for (i = snake_length; i > 0U; i--) {
            snake_x[i] = snake_x[i - 1U];
            snake_y[i] = snake_y[i - 1U];
        }
        snake_length++;
    } else {
        for (i = (uint8_t)(snake_length - 1U); i > 0U; i--) {
            snake_x[i] = snake_x[i - 1U];
            snake_y[i] = snake_y[i - 1U];
        }
    }

    snake_x[0] = (uint8_t)new_x;
    snake_y[0] = (uint8_t)new_y;

    if (ate != 0U) {
        score++;
        spawn_food();
        play_sound(1200U, 2U);
    }

    return 0U;
}

static void number_to_text(uint16_t value, char *text)
{
    char reversed[5];
    uint8_t length = 0;
    uint8_t i;

    do {
        reversed[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && length < sizeof(reversed));

    for (i = 0; i < length; i++) text[i] = reversed[length - i - 1U];
    text[length] = '\0';
}

static void draw_game(u8g2_t *display)
{
    uint8_t i;
    char score_text[6];

    number_to_text(score, score_text);
    u8g2_SetFont(display, u8g2_font_5x7_tf);
    u8g2_DrawStr(display, 1U, 7U, "SCORE:");
    u8g2_DrawStr(display, 37U, 7U, score_text);

    u8g2_DrawFrame(display, 0U, HUD_HEIGHT,
                   SCREEN_WIDTH, (u8g2_uint_t)(GRID_HEIGHT * CELL_SIZE + 2U));

    u8g2_DrawBox(display,
        (u8g2_uint_t)(food_x * CELL_SIZE + 1U),
        (u8g2_uint_t)(HUD_HEIGHT + food_y * CELL_SIZE + 1U),
        CELL_SIZE - 2U, CELL_SIZE - 2U);

    for (i = 0; i < snake_length; i++) {
        u8g2_DrawBox(display,
            (u8g2_uint_t)(snake_x[i] * CELL_SIZE + 1U),
            (u8g2_uint_t)(HUD_HEIGHT + snake_y[i] * CELL_SIZE + 1U),
            CELL_SIZE - 2U, CELL_SIZE - 2U);
    }
}

static void draw_game_over(u8g2_t *display)
{
    char score_text[6];

    number_to_text(score, score_text);
    u8g2_DrawFrame(display, 0U, 0U, SCREEN_WIDTH, SCREEN_HEIGHT);
    u8g2_SetFont(display, u8g2_font_5x7_tf);
    u8g2_DrawStr(display, 36U, 17U, "GAME OVER");
    u8g2_DrawStr(display, 31U, 32U, "SCORE:");
    u8g2_DrawStr(display, 67U, 32U, score_text);
    u8g2_DrawStr(display, 20U, 52U, "PRESS A RESTART");
}

void game_init(void)
{
    start_game();
}

void game_update(const EngineInput *input, uint16_t delta_ms)
{
    update_sound();

    if (game_over != 0U) {
        if ((input->pressed & ENGINE_BUTTON_A) != 0U) start_game();
        return;
    }

    read_joystick_direction(input);
    movement_time = (uint16_t)(movement_time + delta_ms);

    if (movement_time >= MOVE_PERIOD_MS) {
        movement_time = (uint16_t)(movement_time - MOVE_PERIOD_MS);

        if (move_snake() != 0U) {
            game_over = 1U;
            play_sound(180U, 10U);
        }
    }
}

void game_render(u8g2_t *display)
{
    if (game_over != 0U) {
        draw_game_over(display);
    } else {
        draw_game(display);
    }
}
