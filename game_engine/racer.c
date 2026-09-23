#include "engine.h"

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <util/delay.h>

/* Screen and road dimensions */
#define SCREEN_WIDTH         128
#define SCREEN_HEIGHT         64
#define ROAD_LEFT             24
#define ROAD_RIGHT           104
#define ROAD_WIDTH   (ROAD_RIGHT - ROAD_LEFT)

#define FP_SHIFT               6
#define FP_ONE       (1 << FP_SHIFT)

#define PLAYER_WIDTH          10
#define PLAYER_HEIGHT         14
#define PLAYER_Y              46

#define MAX_TRAFFIC            4
#define BASE_SPAWN_INTERVAL  55
#define MIN_SPAWN_INTERVAL   20

/* MPU-6050 TWI settings */
#define MPU6050_ADDR        0xD0  /* (0x68 << 1) */
#define MPU_DEADZONE        1500
#define MPU_MAX_TILT       12000

/* Car types */
enum {
    CAR_SEDAN,
    CAR_TRUCK,
    CAR_SPORT
};

typedef struct {
    int16_t x_fp;
    int16_t y_fp;
    int16_t vy_fp;
    uint8_t width;
    uint8_t height;
    uint8_t type;
    uint8_t active;
} TrafficCar;

/* Game state */
static int16_t player_x_fp;
static int16_t player_vx_fp;
static TrafficCar traffic[MAX_TRAFFIC];

static uint32_t distance_m;
static uint16_t score;
static uint16_t top_score = 0;
static uint16_t rng_state;

static uint16_t game_speed_fp;
static uint16_t base_speed_fp;
static uint16_t road_scroll;
static uint8_t spawn_timer;
static uint8_t spawn_interval;
static uint8_t boosting;
static uint8_t game_over;
static uint8_t crash_timer;
static uint8_t sound_frames;
static uint8_t mpu_detected;

static int16_t current_accel_y = 0;
static uint8_t mpu_i2c_addr = 0xD0;

/* -------------------------------------------------------------------------
 * Hardware TWI (I2C) driver for MPU-6050
 * ------------------------------------------------------------------------- */
static void twi_init(void)
{
    /* Enable internal pull-ups on SCL (PC0) and SDA (PC1) */
    DDRC &= (uint8_t)~((1U << PC0) | (1U << PC1));
    PORTC |= (1U << PC0) | (1U << PC1);

    /* 100 kHz SCL at 8 MHz F_CPU: TWBR = 32, prescaler = 1 */
    TWSR = 0x00;
    TWBR = 32;
    TWCR = (1U << TWEN);
}

static uint8_t twi_start(uint8_t address_rw)
{
    uint16_t timeout = 2500;
    TWCR = (1U << TWINT) | (1U << TWSTA) | (1U << TWEN);
    while (!(TWCR & (1U << TWINT))) {
        if (--timeout == 0) return 0;
    }

    TWDR = address_rw;
    TWCR = (1U << TWINT) | (1U << TWEN);
    timeout = 2500;
    while (!(TWCR & (1U << TWINT))) {
        if (--timeout == 0) return 0;
    }

    uint8_t status = TWSR & 0xF8;
    if (status != 0x18 && status != 0x40) return 0; /* SLA+W ACK or SLA+R ACK */
    return 1;
}

static void twi_stop(void)
{
    TWCR = (1U << TWINT) | (1U << TWSTO) | (1U << TWEN);
    _delay_us(10);
}

static uint8_t twi_write(uint8_t data)
{
    uint16_t timeout = 2500;
    TWDR = data;
    TWCR = (1U << TWINT) | (1U << TWEN);
    while (!(TWCR & (1U << TWINT))) {
        if (--timeout == 0) return 0;
    }
    return 1;
}

static uint8_t twi_read_ack(void)
{
    uint16_t timeout = 2500;
    TWCR = (1U << TWINT) | (1U << TWEN) | (1U << TWEA);
    while (!(TWCR & (1U << TWINT))) {
        if (--timeout == 0) return 0;
    }
    return TWDR;
}

static uint8_t twi_read_nack(void)
{
    uint16_t timeout = 2500;
    TWCR = (1U << TWINT) | (1U << TWEN);
    while (!(TWCR & (1U << TWINT))) {
        if (--timeout == 0) return 0;
    }
    return TWDR;
}

static uint8_t mpu6050_init(void)
{
    twi_init();
    _delay_ms(5);

    /* Try 0x68 (0xD0) */
    mpu_i2c_addr = 0xD0;
    if (twi_start(mpu_i2c_addr)) {
        if (twi_write(0x6B) && twi_write(0x00)) {
            twi_stop();
            _delay_ms(10);
            return 1;
        }
        twi_stop();
    }

    /* Try 0x69 (0xD2) */
    mpu_i2c_addr = 0xD2;
    if (twi_start(mpu_i2c_addr)) {
        if (twi_write(0x6B) && twi_write(0x00)) {
            twi_stop();
            _delay_ms(10);
            return 1;
        }
        twi_stop();
    }

    return 0;
}

static int16_t mpu6050_read_accel_y(void)
{
    uint8_t high, low;

    if (!twi_start(mpu_i2c_addr)) {
        twi_stop();
        return 0;
    }
    if (!twi_write(0x3D)) { /* ACCEL_YOUT_H */
        twi_stop();
        return 0;
    }

    if (!twi_start(mpu_i2c_addr | 0x01)) { /* Read */
        twi_stop();
        return 0;
    }
    high = twi_read_ack();
    low = twi_read_nack();
    twi_stop();

    return (int16_t)(((uint16_t)high << 8) | low);
}

/* -------------------------------------------------------------------------
 * Utility & Sound
 * ------------------------------------------------------------------------- */
static uint16_t random_next(void)
{
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return rng_state;
}

static void seed_random(void)
{
    const EngineInput *input = engine_input();
    rng_state = (uint16_t)(input->joystick_x ^ (input->joystick_y << 5) ^
                           (uint16_t)engine_millis() ^ 0x5D93U);
    if (rng_state == 0U) rng_state = 0x5D93U;
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

static void number_to_text(uint32_t value, char *text)
{
    char reversed[8];
    uint8_t length = 0;
    uint8_t i;

    do {
        reversed[length++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && length < sizeof(reversed));

    for (i = 0; i < length; i++) text[i] = reversed[length - i - 1U];
    text[length] = '\0';
}

/* -------------------------------------------------------------------------
 * Game Logic
 * ------------------------------------------------------------------------- */
static void spawn_traffic_car(void)
{
    int8_t free_slot = -1;
    uint8_t lane;
    uint8_t type;
    int16_t spawn_x;
    uint8_t i;

    for (i = 0; i < MAX_TRAFFIC; i++) {
        if (!traffic[i].active) {
            free_slot = (int8_t)i;
            break;
        }
    }
    if (free_slot < 0) return;

    lane = (uint8_t)(random_next() % 3U);
    type = (uint8_t)(random_next() % 3U);

    if (lane == 0) spawn_x = ROAD_LEFT + 4;
    else if (lane == 1) spawn_x = ROAD_LEFT + 28;
    else spawn_x = ROAD_LEFT + 52;

    /* Ensure no active traffic car is currently at the top of this lane */
    for (i = 0; i < MAX_TRAFFIC; i++) {
        if (traffic[i].active && traffic[i].y_fp < (16 * FP_ONE)) {
            int16_t diff = (traffic[i].x_fp >> FP_SHIFT) - spawn_x;
            if (diff > -14 && diff < 14) return; /* Lane occupied at top */
        }
    }

    traffic[(uint8_t)free_slot].x_fp = spawn_x * FP_ONE;
    traffic[(uint8_t)free_slot].y_fp = -20 * FP_ONE;
    traffic[(uint8_t)free_slot].type = type;
    traffic[(uint8_t)free_slot].active = 1;

    if (type == CAR_TRUCK) {
        traffic[(uint8_t)free_slot].width = 11;
        traffic[(uint8_t)free_slot].height = 18;
        traffic[(uint8_t)free_slot].vy_fp = (base_speed_fp * 6) / 10; /* Slower */
    } else if (type == CAR_SPORT) {
        traffic[(uint8_t)free_slot].width = 10;
        traffic[(uint8_t)free_slot].height = 12;
        traffic[(uint8_t)free_slot].vy_fp = (base_speed_fp * 9) / 10; /* Fast */
    } else {
        traffic[(uint8_t)free_slot].width = 10;
        traffic[(uint8_t)free_slot].height = 14;
        traffic[(uint8_t)free_slot].vy_fp = (base_speed_fp * 7) / 10; /* Normal */
    }
}

static void start_game(void)
{
    uint8_t i;

    seed_random();

    player_x_fp = ((ROAD_LEFT + ROAD_RIGHT - PLAYER_WIDTH) / 2) * FP_ONE;
    player_vx_fp = 0;

    for (i = 0; i < MAX_TRAFFIC; i++) {
        traffic[i].active = 0;
    }

    distance_m = 0;
    score = 0;
    base_speed_fp = 2 * FP_ONE; /* 2.0 pixels per frame */
    game_speed_fp = base_speed_fp;
    spawn_timer = 15;
    spawn_interval = BASE_SPAWN_INTERVAL;
    road_scroll = 0;
    boosting = 0;
    game_over = 0;
    crash_timer = 0;
    sound_frames = 0;
    engine_tone_stop();
}

void game_init(void)
{
    mpu_detected = mpu6050_init();
    start_game();
}

void game_update(const EngineInput *input, uint16_t delta_ms)
{
    int16_t steer_input = 0;
    uint8_t i;
    (void)delta_ms;

    update_sound();

    if (game_over) {
        if ((input->pressed & ENGINE_BUTTON_A) != 0U) {
            start_game();
            play_sound(800, 2);
        }
        return;
    }

    /* Retry MPU-6050 probe if not connected yet */
    if (!mpu_detected) {
        mpu_detected = mpu6050_init();
    }

    /* Steer car EXCLUSIVELY using MPU-6050 IMU Y-axis tilt: Y+ for Left, Y- for Right */
    if (mpu_detected) {
        int16_t accel_y = mpu6050_read_accel_y();
        current_accel_y = accel_y;

        if (accel_y > MPU_DEADZONE) {
            steer_input = (int16_t)(accel_y - MPU_DEADZONE);
        } else if (accel_y < -MPU_DEADZONE) {
            steer_input = (int16_t)(accel_y + MPU_DEADZONE);
        } else {
            steer_input = 0;
        }

        /* Y+ yields negative velocity (steer left), Y- yields positive velocity (steer right) */
        player_vx_fp = (int16_t)(-(((int32_t)steer_input * (4 * FP_ONE)) / MPU_MAX_TILT));
    } else {
        player_vx_fp = 0;
    }

    /* Boost / Accelerate with Button A or Button X */
    boosting = ((input->down & ENGINE_BUTTON_A) != 0U ||
                (input->down & ENGINE_BUTTON_X) != 0U);

    if (boosting) {
        game_speed_fp = base_speed_fp + (FP_ONE * 5 / 4); /* +1.25 pixels/frame */
    } else {
        game_speed_fp = base_speed_fp;
    }

    /* Apply steering with inertia */
    player_x_fp += player_vx_fp;

    /* Clamp player within road boundaries */
    if (player_x_fp < (ROAD_LEFT * FP_ONE)) {
        player_x_fp = ROAD_LEFT * FP_ONE;
        player_vx_fp = 0;
    } else if (player_x_fp > ((ROAD_RIGHT - PLAYER_WIDTH) * FP_ONE)) {
        player_x_fp = (ROAD_RIGHT - PLAYER_WIDTH) * FP_ONE;
        player_vx_fp = 0;
    }

    /* Scroll road */
    road_scroll = (uint8_t)(road_scroll + (game_speed_fp >> FP_SHIFT));

    /* Distance and score progression */
    distance_m += boosting ? 2U : 1U;
    score = (uint16_t)(distance_m / 2U);
    if (score > top_score) top_score = score;

    /* Gradually increase base speed and spawn rate over distance */
    if ((distance_m % 200U) == 0U && base_speed_fp < (5 * FP_ONE)) {
        base_speed_fp += (FP_ONE / 8); /* +0.125 px/frame every 200m */
        if (spawn_interval > MIN_SPAWN_INTERVAL) {
            spawn_interval--;
        }
    }

    /* Spawn traffic */
    if (spawn_timer == 0) {
        spawn_traffic_car();
        spawn_timer = spawn_interval;
    } else {
        spawn_timer--;
    }

    /* Update traffic cars */
    for (i = 0; i < MAX_TRAFFIC; i++) {
        if (!traffic[i].active) continue;

        /* Move traffic down relative to player speed */
        int16_t relative_speed = (int16_t)(game_speed_fp - traffic[i].vy_fp);
        if (relative_speed < (FP_ONE / 2)) relative_speed = FP_ONE / 2;
        traffic[i].y_fp += relative_speed;

        /* Check passed traffic */
        if (traffic[i].y_fp > (SCREEN_HEIGHT * FP_ONE)) {
            traffic[i].active = 0;
            score += 10;
        }

        /* Collision detection */
        int16_t tx = traffic[i].x_fp >> FP_SHIFT;
        int16_t ty = traffic[i].y_fp >> FP_SHIFT;
        int16_t px = player_x_fp >> FP_SHIFT;
        int16_t py = PLAYER_Y;

        if (px + 1 < tx + traffic[i].width - 1 &&
            px + PLAYER_WIDTH - 1 > tx + 1 &&
            py + 1 < ty + traffic[i].height - 1 &&
            py + PLAYER_HEIGHT - 1 > ty + 1) {
            /* Crash! */
            game_over = 1;
            crash_timer = 20;
            play_sound(120, 12);
            return;
        }
    }

    /* Engine hum sound based on speed */
    if (sound_frames == 0) {
        uint16_t engine_freq = boosting ? 260 : 180;
        engine_freq += (uint16_t)((game_speed_fp >> FP_SHIFT) * 15U);
        engine_tone_start(engine_freq);
    }
}

/* -------------------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------------------- */
static void draw_player_car(u8g2_t *display, int16_t x, int16_t y)
{
    /* Main body */
    u8g2_DrawBox(display, (u8g2_uint_t)(x + 2), (u8g2_uint_t)y, 6, PLAYER_HEIGHT);
    /* Left and Right Tires */
    u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + 2), 2, 4);
    u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + 9), 2, 4);
    u8g2_DrawBox(display, (u8g2_uint_t)(x + 8), (u8g2_uint_t)(y + 2), 2, 4);
    u8g2_DrawBox(display, (u8g2_uint_t)(x + 8), (u8g2_uint_t)(y + 9), 2, 4);
    /* Windshield cutout */
    u8g2_SetDrawColor(display, 0);
    u8g2_DrawBox(display, (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 4), 4, 3);
    /* Rear spoiler */
    u8g2_SetDrawColor(display, 1);
    u8g2_DrawHLine(display, (u8g2_uint_t)(x + 1), (u8g2_uint_t)(y + PLAYER_HEIGHT - 1), 8);
}

static void draw_traffic_car(u8g2_t *display, const TrafficCar *car)
{
    int16_t x = car->x_fp >> FP_SHIFT;
    int16_t y = car->y_fp >> FP_SHIFT;

    if (car->type == CAR_TRUCK) {
        /* Truck: solid cab and trailer */
        u8g2_DrawBox(display, (u8g2_uint_t)(x + 1), (u8g2_uint_t)y, (u8g2_uint_t)(car->width - 2), (u8g2_uint_t)car->height);
        u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + 1), (u8g2_uint_t)car->width, 3);
        u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + car->height - 4), (u8g2_uint_t)car->width, 3);
        /* Trailer gap */
        u8g2_SetDrawColor(display, 0);
        u8g2_DrawHLine(display, (u8g2_uint_t)(x + 2), (u8g2_uint_t)(y + 5), (u8g2_uint_t)(car->width - 4));
        u8g2_SetDrawColor(display, 1);
    } else {
        /* Sedan / Sport */
        u8g2_DrawBox(display, (u8g2_uint_t)(x + 2), (u8g2_uint_t)y, 6, (u8g2_uint_t)car->height);
        u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + 2), 2, 3);
        u8g2_DrawBox(display, (u8g2_uint_t)x, (u8g2_uint_t)(y + car->height - 5), 2, 3);
        u8g2_DrawBox(display, (u8g2_uint_t)(x + 8), (u8g2_uint_t)(y + 2), 2, 3);
        u8g2_DrawBox(display, (u8g2_uint_t)(x + 8), (u8g2_uint_t)(y + car->height - 5), 2, 3);
        /* Windshield */
        u8g2_SetDrawColor(display, 0);
        u8g2_DrawBox(display, (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + car->height - 6), 4, 2);
        u8g2_SetDrawColor(display, 1);
    }
}

static void draw_road(u8g2_t *display)
{
    uint8_t y;
    uint8_t offset = road_scroll % 12;

    /* Road borders */
    u8g2_DrawVLine(display, ROAD_LEFT - 1, 0, SCREEN_HEIGHT);
    u8g2_DrawVLine(display, ROAD_RIGHT, 0, SCREEN_HEIGHT);

    /* Alternating curb shoulder stripes */
    for (y = 0; y < SCREEN_HEIGHT; y += 6) {
        uint8_t curb_y = (uint8_t)(y + offset);
        if (curb_y < SCREEN_HEIGHT) {
            u8g2_DrawBox(display, ROAD_LEFT - 4, curb_y, 3, 3);
            u8g2_DrawBox(display, ROAD_RIGHT + 1, curb_y, 3, 3);
        }
    }

    /* Dashed lane dividers */
    for (y = 0; y < SCREEN_HEIGHT; y += 12) {
        int16_t dash_y = (int16_t)(y + offset);
        if (dash_y < SCREEN_HEIGHT) {
            u8g2_DrawVLine(display, ROAD_LEFT + 26, (u8g2_uint_t)dash_y, 6);
            u8g2_DrawVLine(display, ROAD_LEFT + 53, (u8g2_uint_t)dash_y, 6);
        }
    }
}

static void draw_hud(u8g2_t *display)
{
    char text_buf[10];

    u8g2_SetFont(display, u8g2_font_5x7_tf);

    /* Left grass HUD: Score */
    u8g2_DrawStr(display, 1, 8, "SCR");
    number_to_text(score, text_buf);
    u8g2_DrawStr(display, 1, 17, text_buf);

    /* Distance in meters */
    u8g2_DrawStr(display, 1, 32, "DST");
    number_to_text(distance_m, text_buf);
    u8g2_DrawStr(display, 1, 41, text_buf);

    /* Right grass HUD: Speed & Tilt indicator */
    uint16_t mph = (uint16_t)(45U + (uint16_t)(game_speed_fp * 18U / FP_ONE));
    u8g2_DrawStr(display, 107, 8, "MPH");
    number_to_text(mph, text_buf);
    u8g2_DrawStr(display, 107, 17, text_buf);

    if (boosting) {
        u8g2_DrawStr(display, 107, 30, "NOS");
    }

    if (mpu_detected) {
        u8g2_DrawStr(display, 107, 44, "TILT");
        /* Live tilt cursor indicator */
        u8g2_DrawFrame(display, 107, 47, 18, 5);
        int8_t tilt_offset = (int8_t)(((int32_t)current_accel_y * 7) / MPU_MAX_TILT);
        if (tilt_offset > 7) tilt_offset = 7;
        if (tilt_offset < -7) tilt_offset = -7;
        int8_t bar_x = (int8_t)(115 - tilt_offset);
        u8g2_DrawVLine(display, (u8g2_uint_t)bar_x, 48, 3);
    } else {
        u8g2_DrawStr(display, 107, 44, "NO IMU");
    }
}

static void draw_game_over(u8g2_t *display)
{
    char text_buf[10];

    u8g2_DrawFrame(display, 16, 8, 96, 48);
    u8g2_SetDrawColor(display, 0);
    u8g2_DrawBox(display, 17, 9, 94, 46);
    u8g2_SetDrawColor(display, 1);

    u8g2_SetFont(display, u8g2_font_6x12_tr);
    u8g2_DrawStr(display, 37, 21, "CRASHED!");

    u8g2_SetFont(display, u8g2_font_5x7_tf);
    u8g2_DrawStr(display, 24, 33, "SCORE:");
    number_to_text(score, text_buf);
    u8g2_DrawStr(display, 62, 33, text_buf);

    u8g2_DrawStr(display, 24, 42, "TOP:");
    number_to_text(top_score, text_buf);
    u8g2_DrawStr(display, 62, 42, text_buf);

    u8g2_DrawStr(display, 24, 52, "PRESS A RESTART");
}

void game_render(u8g2_t *display)
{
    uint8_t i;

    draw_road(display);

    /* Draw traffic */
    for (i = 0; i < MAX_TRAFFIC; i++) {
        if (traffic[i].active) {
            draw_traffic_car(display, &traffic[i]);
        }
    }

    /* Draw player car */
    if (!game_over || (crash_timer & 2U)) {
        draw_player_car(display, player_x_fp >> FP_SHIFT, PLAYER_Y);
    }

    draw_hud(display);

    if (game_over) {
        draw_game_over(display);
    }
}
