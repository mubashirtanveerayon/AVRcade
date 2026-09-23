#include "engine.h"

#include <avr/pgmspace.h>
#include <stdint.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define FP_SHIFT 4
#define FP_ONE   (1 << FP_SHIFT)
#define WIDTH_FP  ((int16_t)(SCREEN_WIDTH * FP_ONE))
#define HEIGHT_FP ((int16_t)(SCREEN_HEIGHT * FP_ONE))

#define MAX_ASTEROIDS 6
#define MAX_BULLETS   4

#define SHIP_RADIUS   3
#define SHIP_NOSE     5
#define SHIP_BACK     4

#define TURN_DEADZONE 40
#define TURN_DIVISOR  24
#define THRUST_ACCEL  6
#define MAX_SPEED     56
#define DRAG_SHIFT    5

#define BULLET_SPEED    72
#define BULLET_LIFETIME 35
#define SHOOT_COOLDOWN  4

#define ASTEROID_LARGE  11
#define ASTEROID_MEDIUM 7
#define ASTEROID_SMALL  4

#define SCORE_LARGE  20
#define SCORE_MEDIUM 50
#define SCORE_SMALL  100

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t angle;
} Ship;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t life;
} Bullet;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t radius;
} Asteroid;

static Ship ship;
static Bullet bullets[MAX_BULLETS];
static Asteroid asteroids[MAX_ASTEROIDS];

static uint16_t score;
static uint16_t rng_state;
static uint8_t wave;
static uint8_t wave_delay;
static uint8_t shoot_cooldown;
static uint8_t thrusting;
static uint8_t game_over;
static uint8_t sound_frames;

/* 0..90 degrees, scaled so 127 represents 1.0. */
static const uint8_t sine_quarter[65] PROGMEM = {
      0,   3,   6,   9,  12,  16,  19,  22,
     25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,
     71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104,
    106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124,
    125, 125, 126, 126, 126, 127, 127, 127,
    127
};

static int8_t fixed_sin(uint8_t angle)
{
    uint8_t quadrant = angle >> 6;
    uint8_t offset = angle & 63U;
    int8_t value;

    if (quadrant == 0U) {
        value = (int8_t)pgm_read_byte(&sine_quarter[offset]);
    } else if (quadrant == 1U) {
        value = (int8_t)pgm_read_byte(&sine_quarter[64U - offset]);
    } else if (quadrant == 2U) {
        value = (int8_t)-((int8_t)pgm_read_byte(&sine_quarter[offset]));
    } else {
        value = (int8_t)-((int8_t)pgm_read_byte(
            &sine_quarter[64U - offset]));
    }

    return value;
}

static int8_t fixed_cos(uint8_t angle)
{
    return fixed_sin((uint8_t)(angle + 64U));
}

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
                           (uint16_t)engine_millis() ^ 0xACE1U);
    if (rng_state == 0U) rng_state = 0xACE1U;
}

static int16_t absolute16(int16_t value)
{
    return value < 0 ? (int16_t)-value : value;
}

static int16_t wrap_fixed(int16_t value, int16_t maximum)
{
    while (value < 0) value += maximum;
    while (value >= maximum) value -= maximum;
    return value;
}

static int16_t wrap_x(int16_t x)
{
    while (x < 0) x += SCREEN_WIDTH;
    while (x >= SCREEN_WIDTH) x -= SCREEN_WIDTH;
    return x;
}

static int16_t wrap_y(int16_t y)
{
    while (y < 0) y += SCREEN_HEIGHT;
    while (y >= SCREEN_HEIGHT) y -= SCREEN_HEIGHT;
    return y;
}

static int16_t wrapped_difference(int16_t a, int16_t b, int16_t size)
{
    int16_t difference = absolute16((int16_t)(a - b));
    if (difference > size / 2) difference = (int16_t)(size - difference);
    return difference;
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

static void clamp_speed(int16_t *vx, int16_t *vy)
{
    if (*vx > MAX_SPEED) *vx = MAX_SPEED;
    if (*vx < -MAX_SPEED) *vx = -MAX_SPEED;
    if (*vy > MAX_SPEED) *vy = MAX_SPEED;
    if (*vy < -MAX_SPEED) *vy = -MAX_SPEED;
}

static int8_t find_free_bullet(void)
{
    uint8_t i;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].life == 0U) return (int8_t)i;
    }
    return -1;
}

static int8_t find_free_asteroid(void)
{
    uint8_t i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].radius == 0U) return (int8_t)i;
    }
    return -1;
}

static uint8_t asteroids_alive(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].radius != 0U) count++;
    }
    return count;
}

static void spawn_asteroid_at(int16_t x, int16_t y, uint8_t radius)
{
    int8_t slot = find_free_asteroid();
    uint8_t direction;
    uint16_t speed;

    if (slot < 0) return;

    direction = (uint8_t)random_next();
    if (radius == ASTEROID_LARGE) {
        speed = (uint16_t)(4U + random_next() % 5U);
    } else if (radius == ASTEROID_MEDIUM) {
        speed = (uint16_t)(6U + random_next() % 6U);
    } else {
        speed = (uint16_t)(8U + random_next() % 6U);
    }

    asteroids[(uint8_t)slot].x = x;
    asteroids[(uint8_t)slot].y = y;
    asteroids[(uint8_t)slot].vx =
        (int16_t)(((int32_t)fixed_cos(direction) * speed) >> 7);
    asteroids[(uint8_t)slot].vy =
        (int16_t)(((int32_t)fixed_sin(direction) * speed) >> 7);
    asteroids[(uint8_t)slot].radius = radius;
}

static void spawn_wave(uint8_t count)
{
    uint8_t i;

    if (count > MAX_ASTEROIDS) count = MAX_ASTEROIDS;

    for (i = 0; i < count; i++) {
        int16_t x;
        int16_t y;
        int16_t dx;
        int16_t dy;

        do {
            x = (int16_t)((random_next() % SCREEN_WIDTH) * FP_ONE);
            y = (int16_t)((random_next() % SCREEN_HEIGHT) * FP_ONE);
            dx = wrapped_difference((int16_t)(x >> FP_SHIFT),
                                    (int16_t)(ship.x >> FP_SHIFT),
                                    SCREEN_WIDTH);
            dy = wrapped_difference((int16_t)(y >> FP_SHIFT),
                                    (int16_t)(ship.y >> FP_SHIFT),
                                    SCREEN_HEIGHT);
        } while (dx < 32 && dy < 32);

        spawn_asteroid_at(x, y, ASTEROID_LARGE);
    }
}

static void reset_ship(void)
{
    ship.x = (SCREEN_WIDTH / 2) * FP_ONE;
    ship.y = (SCREEN_HEIGHT / 2) * FP_ONE;
    ship.vx = 0;
    ship.vy = 0;
    ship.angle = 0U;
}

static void start_game(void)
{
    uint8_t i;

    seed_random();

    for (i = 0; i < MAX_BULLETS; i++) bullets[i].life = 0U;
    for (i = 0; i < MAX_ASTEROIDS; i++) asteroids[i].radius = 0U;

    reset_ship();
    score = 0U;
    wave = 1U;
    wave_delay = 0U;
    shoot_cooldown = 0U;
    thrusting = 0U;
    game_over = 0U;
    sound_frames = 0U;
    engine_tone_stop();

    spawn_wave(2U);
}

static void update_ship(const EngineInput *input)
{
    int16_t joystick_offset = (int16_t)input->joystick_x - 512;

    /* Moving the joystick left/right rotates the triangular ship's tip. */
    if (absolute16(joystick_offset) > TURN_DEADZONE) {
        ship.angle = (uint8_t)(ship.angle -
                     (int8_t)(joystick_offset / TURN_DIVISOR));
    }

    thrusting = (input->down & ENGINE_BUTTON_A) != 0U;
    if (thrusting != 0U) {
        ship.vx += (int16_t)(((int32_t)fixed_cos(ship.angle) *
                              THRUST_ACCEL) >> 7);
        ship.vy -= (int16_t)(((int32_t)fixed_sin(ship.angle) *
                              THRUST_ACCEL) >> 7);
    }

    clamp_speed(&ship.vx, &ship.vy);

    ship.vx -= ship.vx >> DRAG_SHIFT;
    ship.vy -= ship.vy >> DRAG_SHIFT;

    ship.x = wrap_fixed((int16_t)(ship.x + ship.vx), WIDTH_FP);
    ship.y = wrap_fixed((int16_t)(ship.y + ship.vy), HEIGHT_FP);
}

static void try_to_shoot(const EngineInput *input)
{
    int8_t slot;
    int16_t offset_x;
    int16_t offset_y;

    if (shoot_cooldown != 0U) return;
    if ((input->down & ENGINE_BUTTON_X) == 0U) return;

    slot = find_free_bullet();
    if (slot < 0) return;

    offset_x = (int16_t)(((int32_t)fixed_cos(ship.angle) *
                          (SHIP_NOSE * FP_ONE)) >> 7);
    offset_y = (int16_t)(((int32_t)fixed_sin(ship.angle) *
                          (SHIP_NOSE * FP_ONE)) >> 7);

    bullets[(uint8_t)slot].x = (int16_t)(ship.x + offset_x);
    bullets[(uint8_t)slot].y = (int16_t)(ship.y - offset_y);
    bullets[(uint8_t)slot].vx =
        (int16_t)(ship.vx + (((int32_t)fixed_cos(ship.angle) *
                              BULLET_SPEED) >> 7));
    bullets[(uint8_t)slot].vy =
        (int16_t)(ship.vy - (((int32_t)fixed_sin(ship.angle) *
                              BULLET_SPEED) >> 7));
    bullets[(uint8_t)slot].life = BULLET_LIFETIME;
    shoot_cooldown = SHOOT_COOLDOWN;
    play_sound(1350U, 1U);
}

static void update_bullets(void)
{
    uint8_t i;

    if (shoot_cooldown != 0U) shoot_cooldown--;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].life == 0U) continue;

        bullets[i].x = (int16_t)(bullets[i].x + bullets[i].vx);
        bullets[i].y = (int16_t)(bullets[i].y + bullets[i].vy);
        bullets[i].life--;

        if (bullets[i].x < 0 || bullets[i].x >= WIDTH_FP ||
            bullets[i].y < 0 || bullets[i].y >= HEIGHT_FP) {
            bullets[i].life = 0U;
        }
    }
}

static void update_asteroids(void)
{
    uint8_t i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].radius == 0U) continue;

        asteroids[i].x = wrap_fixed(
            (int16_t)(asteroids[i].x + asteroids[i].vx), WIDTH_FP);
        asteroids[i].y = wrap_fixed(
            (int16_t)(asteroids[i].y + asteroids[i].vy), HEIGHT_FP);
    }
}

static void destroy_asteroid(uint8_t index)
{
    uint8_t radius = asteroids[index].radius;
    int16_t x = asteroids[index].x;
    int16_t y = asteroids[index].y;

    asteroids[index].radius = 0U;

    if (radius == ASTEROID_LARGE) {
        score = (uint16_t)(score + SCORE_LARGE);
        spawn_asteroid_at(x, y, ASTEROID_MEDIUM);
        spawn_asteroid_at(x, y, ASTEROID_MEDIUM);
    } else if (radius == ASTEROID_MEDIUM) {
        score = (uint16_t)(score + SCORE_MEDIUM);
        spawn_asteroid_at(x, y, ASTEROID_SMALL);
        spawn_asteroid_at(x, y, ASTEROID_SMALL);
    } else {
        score = (uint16_t)(score + SCORE_SMALL);
    }

    play_sound(520U, 2U);
}

static uint8_t check_collisions(void)
{
    uint8_t bullet_index;
    uint8_t asteroid_index;
    int16_t ship_x = (int16_t)(ship.x >> FP_SHIFT);
    int16_t ship_y = (int16_t)(ship.y >> FP_SHIFT);

    for (bullet_index = 0; bullet_index < MAX_BULLETS; bullet_index++) {
        int16_t bullet_x;
        int16_t bullet_y;

        if (bullets[bullet_index].life == 0U) continue;
        bullet_x = (int16_t)(bullets[bullet_index].x >> FP_SHIFT);
        bullet_y = (int16_t)(bullets[bullet_index].y >> FP_SHIFT);

        for (asteroid_index = 0;
             asteroid_index < MAX_ASTEROIDS;
             asteroid_index++) {
            int16_t dx;
            int16_t dy;
            int16_t radius;

            if (asteroids[asteroid_index].radius == 0U) continue;

            dx = (int16_t)(bullet_x -
                 (asteroids[asteroid_index].x >> FP_SHIFT));
            dy = (int16_t)(bullet_y -
                 (asteroids[asteroid_index].y >> FP_SHIFT));
            radius = asteroids[asteroid_index].radius;

            if ((int32_t)dx * dx + (int32_t)dy * dy <=
                (int32_t)radius * radius) {
                bullets[bullet_index].life = 0U;
                destroy_asteroid(asteroid_index);
                break;
            }
        }
    }

    for (asteroid_index = 0;
         asteroid_index < MAX_ASTEROIDS;
         asteroid_index++) {
        int16_t dx;
        int16_t dy;
        int16_t radius;

        if (asteroids[asteroid_index].radius == 0U) continue;

        dx = wrapped_difference(ship_x,
             (int16_t)(asteroids[asteroid_index].x >> FP_SHIFT),
             SCREEN_WIDTH);
        dy = wrapped_difference(ship_y,
             (int16_t)(asteroids[asteroid_index].y >> FP_SHIFT),
             SCREEN_HEIGHT);
        radius = (int16_t)(asteroids[asteroid_index].radius + SHIP_RADIUS);

        if ((int32_t)dx * dx + (int32_t)dy * dy <=
            (int32_t)radius * radius) {
            return 1U;
        }
    }

    return 0U;
}

static void draw_wrapped_pixel(u8g2_t *display, int16_t x, int16_t y)
{
    u8g2_DrawPixel(display, (u8g2_uint_t)wrap_x(x),
                   (u8g2_uint_t)wrap_y(y));
}

static void draw_wrapped_line(u8g2_t *display,
                              int16_t x0, int16_t y0,
                              int16_t x1, int16_t y1)
{
    int16_t dx = absolute16((int16_t)(x1 - x0));
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = (int16_t)-absolute16((int16_t)(y1 - y0));
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t error = (int16_t)(dx + dy);

    while (1) {
        int16_t error2;

        draw_wrapped_pixel(display, x0, y0);
        if (x0 == x1 && y0 == y1) break;

        error2 = (int16_t)(2 * error);
        if (error2 >= dy) {
            error = (int16_t)(error + dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (error2 <= dx) {
            error = (int16_t)(error + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

static void draw_wrapped_circle(u8g2_t *display,
                                int16_t center_x,
                                int16_t center_y,
                                int16_t radius)
{
    int16_t x = radius;
    int16_t y = 0;
    int16_t error = 1 - radius;

    while (x >= y) {
        draw_wrapped_pixel(display, center_x + x, center_y + y);
        draw_wrapped_pixel(display, center_x + y, center_y + x);
        draw_wrapped_pixel(display, center_x - y, center_y + x);
        draw_wrapped_pixel(display, center_x - x, center_y + y);
        draw_wrapped_pixel(display, center_x - x, center_y - y);
        draw_wrapped_pixel(display, center_x - y, center_y - x);
        draw_wrapped_pixel(display, center_x + y, center_y - x);
        draw_wrapped_pixel(display, center_x + x, center_y - y);

        y++;
        if (error < 0) {
            error = (int16_t)(error + 2 * y + 1);
        } else {
            x--;
            error = (int16_t)(error + 2 * (y - x) + 1);
        }
    }
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

static void draw_ship(u8g2_t *display)
{
    int16_t center_x = (int16_t)(ship.x >> FP_SHIFT);
    int16_t center_y = (int16_t)(ship.y >> FP_SHIFT);
    int16_t nose_x;
    int16_t nose_y;
    int16_t back_left_x;
    int16_t back_left_y;
    int16_t back_right_x;
    int16_t back_right_y;
    uint8_t back_left_angle = (uint8_t)(ship.angle + 96U);
    uint8_t back_right_angle = (uint8_t)(ship.angle - 96U);

    nose_x = (int16_t)(center_x +
             (((int32_t)fixed_cos(ship.angle) * SHIP_NOSE) >> 7));
    nose_y = (int16_t)(center_y -
             (((int32_t)fixed_sin(ship.angle) * SHIP_NOSE) >> 7));
    back_left_x = (int16_t)(center_x +
                  (((int32_t)fixed_cos(back_left_angle) * SHIP_BACK) >> 7));
    back_left_y = (int16_t)(center_y -
                  (((int32_t)fixed_sin(back_left_angle) * SHIP_BACK) >> 7));
    back_right_x = (int16_t)(center_x +
                   (((int32_t)fixed_cos(back_right_angle) * SHIP_BACK) >> 7));
    back_right_y = (int16_t)(center_y -
                   (((int32_t)fixed_sin(back_right_angle) * SHIP_BACK) >> 7));

    draw_wrapped_line(display, nose_x, nose_y, back_left_x, back_left_y);
    draw_wrapped_line(display, nose_x, nose_y, back_right_x, back_right_y);
    draw_wrapped_line(display, back_left_x, back_left_y,
                      back_right_x, back_right_y);

    if (thrusting != 0U) {
        uint8_t flame_angle = (uint8_t)(ship.angle + 128U);
        int16_t flame_x = (int16_t)(center_x +
            (((int32_t)fixed_cos(flame_angle) * (SHIP_BACK + 3)) >> 7));
        int16_t flame_y = (int16_t)(center_y -
            (((int32_t)fixed_sin(flame_angle) * (SHIP_BACK + 3)) >> 7));

        draw_wrapped_line(display, back_left_x, back_left_y, flame_x, flame_y);
        draw_wrapped_line(display, back_right_x, back_right_y, flame_x, flame_y);
    }
}

static void draw_playfield(u8g2_t *display)
{
    uint8_t i;
    char score_text[6];
    char wave_text[4];

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].radius == 0U) continue;
        draw_wrapped_circle(display,
            (int16_t)(asteroids[i].x >> FP_SHIFT),
            (int16_t)(asteroids[i].y >> FP_SHIFT),
            asteroids[i].radius);
    }

    for (i = 0; i < MAX_BULLETS; i++) {
        int16_t x;
        int16_t y;

        if (bullets[i].life == 0U) continue;
        x = (int16_t)(bullets[i].x >> FP_SHIFT);
        y = (int16_t)(bullets[i].y >> FP_SHIFT);
        u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)y, 2U, 2U);
    }

    draw_ship(display);

    number_to_text(score, score_text);
    number_to_text(wave, wave_text);

    /* Keep the score readable even when an asteroid passes behind it. */
    u8g2_SetDrawColor(display, 0U);
    u8g2_DrawBox(display, 0U, 0U, 75U, 8U);
    u8g2_SetDrawColor(display, 1U);
    u8g2_SetFont(display, u8g2_font_5x7_tf);
    u8g2_DrawStr(display, 1U, 7U, "S:");
    u8g2_DrawStr(display, 13U, 7U, score_text);
    u8g2_DrawStr(display, 48U, 7U, "W:");
    u8g2_DrawStr(display, 60U, 7U, wave_text);
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
    uint8_t count;

    (void)delta_ms;
    update_sound();

    if (game_over != 0U) {
        if ((input->pressed & ENGINE_BUTTON_A) != 0U) start_game();
        return;
    }

    update_ship(input);
    try_to_shoot(input);
    update_bullets();
    update_asteroids();

    if (check_collisions() != 0U) {
        game_over = 1U;
        thrusting = 0U;
        play_sound(180U, 10U);
        return;
    }

    if (asteroids_alive() == 0U) {
        if (wave_delay == 0U) {
            wave_delay = 30U; /* 1 second breathing room before next wave */
        } else {
            wave_delay--;
            if (wave_delay == 0U) {
                if (wave < 99U) wave++;
                count = (uint8_t)(1U + (wave / 2U));
                if (count > 4U) count = 4U;
                spawn_wave(count);
                play_sound(900U, 4U);
            }
        }
    } else {
        wave_delay = 0U;
    }
}

void game_render(u8g2_t *display)
{
    if (game_over != 0U) {
        draw_game_over(display);
    } else {
        draw_playfield(display);
    }
}
