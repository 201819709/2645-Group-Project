#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "PWM.h"
#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern PWM_cfg_t pwm_cfg;
extern PWM_cfg_t pwm_green_cfg;

// Game tuning
#define GAME1_FRAME_TIME_MS 30U
#define GAME1_SCREEN_W 240
#define GAME1_SCREEN_H 240
#define GAME1_GROUND_Y 192
// Player stays near the left edge
#define GAME1_PLAYER_X 34
#define GAME1_PLAYER_SCALE 1
#define GAME1_PLAYER_W 24
#define GAME1_PLAYER_H 24
// Final size on screen after scaling
#define GAME1_PLAYER_DRAW_W (GAME1_PLAYER_W * GAME1_PLAYER_SCALE)
#define GAME1_PLAYER_DRAW_H (GAME1_PLAYER_H * GAME1_PLAYER_SCALE)
// Standing position measured from the top of the sprite
#define GAME1_PLAYER_GROUND_Y (GAME1_GROUND_Y - GAME1_PLAYER_DRAW_H)
#define GAME1_OBSTACLE_SCALE 1
#define GAME1_CLOUD_SCALE 2
#define GAME1_MAX_OBSTACLES 3
// Upward launch speed
#define GAME1_JUMP_VELOCITY_TENTHS -140
// Downward pull each frame
#define GAME1_GRAVITY_TENTHS 12
#define GAME1_LED_PWM_FREQ_HZ 1000U
#define GAME1_SCORE_FLASH_MS 140U
#define GAME1_GAME_OVER_RED_DUTY 80U
#define GAME1_MAX_LIVES 3U

// Obstacle kinds drawn on screen
typedef enum {
    GAME1_OBSTACLE_CACTUS = 0,
    GAME1_OBSTACLE_ROCK = 1,
    GAME1_OBSTACLE_SPIKE = 2,
    GAME1_OBSTACLE_CRATE = 3,
    GAME1_OBSTACLE_HEART = 4
} Game1ObstacleType;

// High-level run state
typedef enum {
    GAME1_PHASE_PLAYING = 0,
    GAME1_PHASE_GAME_OVER
} Game1Phase;

// Player Y and velocity are kept in tenths for smoother motion
typedef struct {
    // Draw X on screen
    int16_t x;
    // Draw Y in tenths
    int16_t y_tenths;
    // Vertical speed in tenths
    int16_t velocity_tenths;
    // Non-zero while standing
    uint8_t on_ground;
} Game1Player;

// One moving object in the lane
typedef struct {
    // Draw X on screen
    int16_t x;
    // Sprite / behaviour kind
    uint8_t type;
    // Stops double scoring
    uint8_t scored;
} Game1Obstacle;

// Static background cloud
typedef struct {
    // Draw X on screen
    int16_t x;
    // Draw Y on screen
    int16_t y;
} Game1Cloud;

// Full game state for one run
typedef struct {
    // Current mode
    Game1Phase phase;
    // Player state
    Game1Player player;
    // Current obstacle positions
    Game1Obstacle obstacles[GAME1_MAX_OBSTACLES];
    // Last frame obstacle positions
    Game1Obstacle prev_obstacles[GAME1_MAX_OBSTACLES];
    // Background decoration
    Game1Cloud clouds[2];
    // Last frame player Y
    int16_t prev_player_y;
    // Current score
    uint32_t score;
    // Last score drawn on HUD
    uint32_t prev_score;
    // Best score for this session
    uint32_t best_score;
    // Current life count
    uint8_t lives;
    // Last life count drawn on HUD
    uint8_t prev_lives;
    // Lane scroll speed
    uint16_t speed_px;
    // Random seed
    uint32_t rng_state;
    // Tick time when green flash ends
    uint32_t green_flash_until;
} Game1State;

// Embedded sprite data
// Converted from: game_1/kenney_pixel-platformer
static const uint8_t kenney_runner_a[24 * 24] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 14, 14, 14, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 14, 14, 14, 255, 255, 255, 255,
    255, 255, 255, 14, 14, 1, 1, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 1, 1, 14, 14, 255, 255, 255,
    255, 255, 255, 14, 1, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 1, 14, 255, 255, 255,
    255, 255, 14, 14, 1, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 1, 14, 14, 255, 255,
    255, 255, 14, 1, 14, 14, 14, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 14, 14, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 14, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 14, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 3, 14, 14, 14, 14, 14, 14, 14, 14, 3, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 14, 6, 6, 6, 6, 6, 6, 6, 6, 14, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 3, 3, 6, 9, 9, 6, 6, 9, 9, 6, 6, 6, 3, 3, 14, 1, 14, 255, 255,
    255, 255, 14, 1, 14, 14, 3, 6, 9, 9, 6, 6, 9, 9, 6, 6, 6, 3, 14, 14, 1, 14, 255, 255,
    255, 255, 14, 14, 1, 14, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 14, 1, 14, 14, 255, 255,
    255, 255, 255, 14, 1, 14, 14, 14, 6, 6, 9, 9, 6, 6, 6, 6, 14, 14, 14, 1, 14, 255, 255, 255,
    255, 255, 255, 14, 14, 1, 1, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 1, 1, 14, 14, 255, 255, 255,
    255, 255, 255, 255, 14, 14, 14, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 14, 14, 14, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 3, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 3, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 3, 3, 6, 6, 6, 3, 3, 6, 6, 6, 3, 3, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 3, 3, 6, 6, 3, 3, 6, 6, 3, 3, 255, 255, 255, 255, 255, 255, 255
};

static const uint8_t kenney_obstacle_a[18 * 18] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 12, 12, 12, 12, 255, 255, 12, 12, 12, 12, 255, 255, 255, 255,
    255, 255, 255, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 255, 255, 255,
    255, 255, 12, 12, 12, 5, 5, 12, 12, 12, 12, 5, 5, 12, 12, 12, 255, 255,
    255, 255, 12, 12, 5, 5, 5, 5, 12, 12, 5, 5, 5, 5, 12, 12, 255, 255,
    255, 255, 12, 12, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 12, 12, 255, 255,
    255, 255, 12, 12, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 12, 12, 255, 255,
    255, 255, 12, 12, 12, 5, 2, 2, 2, 2, 2, 2, 5, 12, 12, 12, 255, 255,
    255, 255, 255, 12, 12, 12, 2, 2, 2, 2, 2, 2, 12, 12, 12, 255, 255, 255,
    255, 255, 255, 255, 12, 12, 12, 2, 2, 2, 2, 12, 12, 12, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 12, 12, 12, 2, 2, 12, 12, 12, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 12, 12, 12, 12, 12, 12, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 12, 12, 12, 12, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static const uint8_t kenney_obstacle_b[18 * 18] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,12,12,12,12,255,255,10,10,10,10,255,255,255,255,
    255,255,255,12,12,12,12,12,12,10,10,10,10,10,10,255,255,255,
    255,255,12,12,12,5,5,12,12,10,10,255,255,10,10,10,255,255,
    255,255,12,12,5,5,5,5,12,10,255,255,255,255,10,10,255,255,
    255,255,12,12,5,5,5,5,5,255,255,255,255,255,10,10,255,255,
    255,255,12,12,5,5,5,5,5,255,255,255,255,255,10,10,255,255,
    255,255,12,12,12,5,2,2,2,255,255,255,255,10,10,10,255,255,
    255,255,255,12,12,12,2,2,2,255,255,255,10,10,10,255,255,255,
    255,255,255,255,12,12,12,2,2,255,255,10,10,10,255,255,255,255,
    255,255,255,255,255,12,12,12,2,255,10,10,10,255,255,255,255,255,
    255,255,255,255,255,255,12,12,12,10,10,10,255,255,255,255,255,255,
    255,255,255,255,255,255,255,12,12,10,10,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

static const uint8_t kenney_obstacle_c[18 * 18] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,12,12,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,12,5,5,12,255,255,255,255,255,255,255,255,
    255,255,255,255,255,12,5,2,2,5,12,255,255,255,255,255,255,255,
    255,255,255,255,12,5,2,2,2,2,5,12,255,255,255,255,255,255,
    255,255,255,12,5,2,2,2,2,2,2,5,12,255,255,255,255,255,
    255,255,12,5,2,2,2,2,2,2,2,2,5,12,255,255,255,255,
    255,12,5,2,2,2,2,2,2,2,2,2,2,5,12,255,255,255,
    12,5,2,2,2,2,2,2,2,2,2,2,2,2,5,12,255,255,
    255,12,5,2,2,2,2,2,2,2,2,2,2,5,12,255,255,255,
    255,255,12,5,2,2,2,2,2,2,2,2,5,12,255,255,255,255,
    255,255,255,12,5,2,2,2,2,2,2,5,12,255,255,255,255,255,
    255,255,255,255,12,5,2,2,2,2,5,12,255,255,255,255,255,255,
    255,255,255,255,255,12,5,2,2,5,12,255,255,255,255,255,255,255,
    255,255,255,255,255,255,12,5,5,12,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,12,12,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

static const uint8_t kenney_obstacle_d[18 * 18] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,12,12,12,12,12,12,12,12,255,255,255,255,255,
    255,255,255,255,12,10,10,10,10,10,10,10,10,12,255,255,255,255,
    255,255,255,12,10,10,6,6,6,6,6,6,10,10,12,255,255,255,
    255,255,255,12,10,6,10,10,10,10,10,10,6,10,12,255,255,255,
    255,255,255,12,10,6,10,10,10,10,10,10,6,10,12,255,255,255,
    255,255,255,12,10,6,10,10,10,10,10,10,6,10,12,255,255,255,
    255,255,255,12,10,6,10,10,10,10,10,10,6,10,12,255,255,255,
    255,255,255,12,10,6,10,10,10,10,10,10,6,10,12,255,255,255,
    255,255,255,12,10,10,6,6,6,6,6,6,10,10,12,255,255,255,
    255,255,255,255,12,10,10,10,10,10,10,10,10,12,255,255,255,255,
    255,255,255,255,255,12,12,12,12,12,12,12,12,255,255,255,255,255,
    255,255,255,255,255,12,255,255,255,255,255,255,12,255,255,255,255,255,
    255,255,255,255,255,12,255,255,255,255,255,255,12,255,255,255,255,255,
    255,255,255,255,255,12,255,255,255,255,255,255,12,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

static const uint8_t heart_sprite[18 * 18] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255, 2, 2,255,255, 2, 2,255,255,255,255,255,255,
    255,255,255,255,255, 2, 4, 4, 2, 2, 4, 4, 2,255,255,255,255,255,
    255,255,255,255, 2, 4, 4, 4, 4, 4, 4, 4, 4, 2,255,255,255,255,
    255,255,255, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2,255,255,255,
    255,255,255, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2,255,255,255,
    255,255,255,255, 2, 4, 4, 4, 4, 4, 4, 4, 4, 2,255,255,255,255,
    255,255,255,255,255, 2, 4, 4, 4, 4, 4, 4, 2,255,255,255,255,255,
    255,255,255,255,255,255, 2, 4, 4, 4, 4, 2,255,255,255,255,255,255,
    255,255,255,255,255,255,255, 2, 4, 4, 2,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255, 2, 2,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

static const uint8_t kenney_floor_tile[18 * 18] = {
    255, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 2, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    12, 12, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    12, 12, 5, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 1, 1, 1, 2, 2, 1, 1, 2, 2, 2, 2, 2,
    12, 12, 2, 1, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    12, 12, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    13, 13, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    13, 13, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    13, 13, 10, 10, 10, 10, 10, 13, 13, 13, 13, 10, 10, 10, 10, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    255, 13, 13, 13, 13, 13, 13, 13, 255, 255, 13, 13, 13, 13, 13, 13, 255, 255
};

static const uint8_t cloud_sprite[12 * 6] = {
    255,255,255,255, 14, 14,255,255, 14, 14,255,255,
    255,255,255, 14,  1,  1, 14, 14,  1,  1, 14,255,
    255,255, 14,  1,  1,  1,  1,  1,  1,  1,  1,14,
    255, 14,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1,
    255,255, 14,  1,  1,  1,  1,  1,  1,  1, 14,255,
    255,255,255, 14, 14, 14, 14, 14, 14, 14,255,255
};

static uint16_t game1_text_width_px(
    const char *text,
    uint8_t font_size
)
{
    // LCD font is 6 pixels wide per character
    return (uint16_t)(strlen(text) * 6U * font_size);
}

// Print a centered paragraph and wrap before it runs off the screen
static void game1_print_wrapped_centered(
    const char *text,
    uint16_t y,
    uint16_t max_width,
    uint8_t colour,
    uint8_t font_size,
    uint16_t line_spacing
)
{
    // Work out how many characters fit in the allowed width
    const size_t max_chars_per_line = max_width / (6U * font_size);
    char line_buffer[64];
    // Cursor walks through the original string
    const char *cursor = text;

    if (max_chars_per_line == 0U) {
        return;
    }

    // Keep slicing lines off until the string is done
    while (*cursor != '\0') {
        size_t line_len = 0U;
        const char *line_start = cursor;
        const char *last_space = NULL;
        uint16_t text_width;
        uint16_t x = 0U;

        // Skip leading spaces on the next line
        while (*cursor == ' ') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        // Mark the real start of this line
        line_start = cursor;

        // Advance until the line is full or the string ends
        while (*cursor != '\0' && line_len < max_chars_per_line) {
            if (*cursor == ' ') {
                last_space = cursor;
            }
            cursor++;
            line_len++;
        }

        // Wrap on the last space so words stay together
        if (*cursor != '\0' && line_len == max_chars_per_line && last_space != NULL) {
            line_len = (size_t)(last_space - line_start);
            cursor = last_space + 1;
        }

        if (line_len >= sizeof(line_buffer)) {
            line_len = sizeof(line_buffer) - 1U;
        }

        // Copy this line into a temporary buffer for printing
        memcpy(line_buffer, line_start, line_len);
        line_buffer[line_len] = '\0';

        // Center the current line before drawing it
        text_width = game1_text_width_px(line_buffer, font_size);
        if (text_width < GAME1_SCREEN_W) {
            x = (uint16_t)((GAME1_SCREEN_W - text_width) / 2U);
        }

        LCD_printString(line_buffer, x, y, colour, font_size);
        // Move down by one text row plus the requested gap
        y += (uint16_t)((7U * font_size) + line_spacing);
    }
}

// Force both LEDs off
static void game1_leds_off(void)
{
    PWM_SetDuty(&pwm_cfg, 0);
    PWM_SetDuty(&pwm_green_cfg, 0);
}

// Update LED behaviour from the current game state
static void game1_update_leds(
    const Game1State *state
)
{
    // Red blinks on game over, green flashes when score changes
    if (state->phase == GAME1_PHASE_GAME_OVER) {
        uint32_t blink_phase = (HAL_GetTick() / 500U) % 2U;
        PWM_SetFreq(&pwm_cfg, GAME1_LED_PWM_FREQ_HZ);
        PWM_SetDuty(&pwm_cfg, blink_phase == 0U ? GAME1_GAME_OVER_RED_DUTY : 0U);
        PWM_SetDuty(&pwm_green_cfg, 0);
        return;
    }

    PWM_SetDuty(&pwm_cfg, 0);
    PWM_SetFreq(&pwm_green_cfg, GAME1_LED_PWM_FREQ_HZ);
    if (HAL_GetTick() < state->green_flash_until) {
        PWM_SetDuty(&pwm_green_cfg, 100);
    } else {
        PWM_SetDuty(&pwm_green_cfg, 0);
    }
}

// Draw a sprite safely even if part of it is off-screen
static void game1_draw_sprite_clipped(
    int16_t x0,
    int16_t y0,
    uint16_t nrows,
    uint16_t ncols,
    const uint8_t *sprite,
    uint8_t scale
)
{
    uint16_t row;
    uint16_t col;

    // Nothing to draw if scale is zero
    if (scale == 0U) {
        return;
    }

    // Walk the source sprite a row at a time
    for (row = 0; row < nrows; row++) {
        for (col = 0; col < ncols; col++) {
            uint8_t pixel = sprite[(row * ncols) + col];
            uint8_t dy;
            uint8_t dx;

            // 255 is transparent in these sprite tables
            if (pixel == 255U) {
                continue;
            }

            // Scale the source pixel into a small block
            for (dy = 0; dy < scale; dy++) {
                int16_t draw_y = (int16_t)(y0 + ((int16_t)row * scale) + dy);

                // Skip rows that land outside the LCD
                if (draw_y < 0 || draw_y >= GAME1_SCREEN_H) {
                    continue;
                }

                // Draw the current scaled row of that block
                for (dx = 0; dx < scale; dx++) {
                    int16_t draw_x = (int16_t)(x0 + ((int16_t)col * scale) + dx);

                    // Skip columns that land outside the LCD
                    if (draw_x < 0 || draw_x >= GAME1_SCREEN_W) {
                        continue;
                    }

                    LCD_Set_Pixel((uint16_t)draw_x, (uint16_t)draw_y, pixel);
                }
            }
        }
    }
}

// Fill a rectangle, clipped to the LCD area
static void game1_fill_rect_clipped(
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    uint8_t colour
)
{
    // Clip first so LCD_Draw_Rect never sees a bad area
    int16_t x0 = x;
    int16_t y0 = y;
    int16_t x1 = x + width;
    int16_t y1 = y + height;

    if (x1 <= 0 || y1 <= 0 || x0 >= GAME1_SCREEN_W || y0 >= GAME1_SCREEN_H) {
        return;
    }

    // Bring the rectangle back inside the visible area
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > GAME1_SCREEN_W) {
        x1 = GAME1_SCREEN_W;
    }
    if (y1 > GAME1_SCREEN_H) {
        y1 = GAME1_SCREEN_H;
    }

    LCD_Draw_Rect(
        (uint16_t)x0,
        (uint16_t)y0,
        (uint16_t)(x1 - x0),
        (uint16_t)(y1 - y0),
        colour,
        1
    );
}

// Small trim line under the top banner
static void game1_draw_banner_accent(
    int16_t x,
    int16_t width
)
{
    game1_fill_rect_clipped(x, 56, width, 3, 14);
    game1_fill_rect_clipped(x, 59, width, 3, 1);
}

// Repaint the ground tiles for one horizontal range
static void game1_redraw_floor_region(
    int16_t x,
    int16_t width
)
{
    // Restore just the part of the floor that was covered before
    int16_t x0 = x;
    int16_t x1 = x + width;
    int16_t tile_start;

    if (x1 <= 0 || x0 >= GAME1_SCREEN_W) {
        return;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > GAME1_SCREEN_W) {
        x1 = GAME1_SCREEN_W;
    }

    game1_fill_rect_clipped(
        x0,
        GAME1_GROUND_Y,
        x1 - x0,
        GAME1_SCREEN_H - GAME1_GROUND_Y,
        13
    );

    tile_start = (int16_t)((x0 / 18) * 18);
    if (tile_start > x0) {
        tile_start -= 18;
    }

    // Redraw every floor tile that overlaps this strip
    for (; tile_start < x1; tile_start += 18) {
        game1_draw_sprite_clipped(
            tile_start,
            GAME1_GROUND_Y,
            18,
            18,
            kenney_floor_tile,
            1
        );
    }
}

// Repaint the right background for a dirty rectangle
static void game1_redraw_background_region(
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height
)
{
    // Restore the background under a moving object
    int16_t region_bottom = y + height;

    if (region_bottom <= 0 || y >= GAME1_SCREEN_H) {
        return;
    }

    if (y < 70) {
        int16_t banner_height = region_bottom < 70 ? height : (70 - y);

        // Restore the blue banner part
        game1_fill_rect_clipped(x, y, width, banner_height, 9);
        // Banner trim has to be redrawn separately
        if (region_bottom > 56 && y < 62) {
            game1_draw_banner_accent(x, width);
        }
    }

    if (region_bottom > 70 && y < GAME1_GROUND_Y) {
        int16_t play_y = y < 70 ? 70 : y;
        int16_t play_bottom =
            region_bottom > GAME1_GROUND_Y ? GAME1_GROUND_Y : region_bottom;

        // Restore the main black play area
        game1_fill_rect_clipped(x, play_y, width, play_bottom - play_y, 0);
    }

    if (region_bottom > GAME1_GROUND_Y) {
        game1_redraw_floor_region(x, width);
    }
}

// Advance the random seed
static uint32_t game1_random_next(Game1State *state)
{
    // Small LCG is enough for obstacle spacing and type picks
    state->rng_state = (state->rng_state * 1664525UL) + 1013904223UL;
    return state->rng_state;
}

// Get a random value in the inclusive range [min_value, max_value]
static uint16_t game1_random_range(
    Game1State *state,
    uint16_t min_value,
    uint16_t max_value
)
{
    uint32_t span = (uint32_t)(max_value - min_value + 1U);
    return (uint16_t)(min_value + (game1_random_next(state) % span));
}

// All current obstacles use the same draw width
static int16_t game1_get_obstacle_width(
    uint8_t type
)
{
    (void)type;
    return 18 * GAME1_OBSTACLE_SCALE;
}

// Hearts are shorter than the ground obstacles
static int16_t game1_get_obstacle_height(
    uint8_t type
)
{
    if (type == GAME1_OBSTACLE_HEART) {
        return 12 * GAME1_OBSTACLE_SCALE;
    }

    return 18 * GAME1_OBSTACLE_SCALE;
}

// Return the top Y position for the obstacle type
static int16_t game1_get_obstacle_y(
    uint8_t type
)
{
    int16_t height = game1_get_obstacle_height(type);

    // Hearts float above the ground so they read as pickups
    if (type == GAME1_OBSTACLE_HEART) {
        return (int16_t)(GAME1_GROUND_Y - height - 22);
    }

    return (int16_t)(GAME1_GROUND_Y - height);
}

// Map obstacle type to sprite data
static const uint8_t *game1_get_obstacle_sprite(
    uint8_t type
)
{
    if (type == GAME1_OBSTACLE_CACTUS) {
        return kenney_obstacle_a;
    }
    if (type == GAME1_OBSTACLE_ROCK) {
        return kenney_obstacle_b;
    }
    if (type == GAME1_OBSTACLE_SPIKE) {
        return kenney_obstacle_c;
    }
    if (type == GAME1_OBSTACLE_CRATE) {
        return kenney_obstacle_d;
    }

    return heart_sprite;
}

// Only the heart is a collectible
static uint8_t game1_is_collectible(
    uint8_t type
)
{
    return type == GAME1_OBSTACLE_HEART ? 1U : 0U;
}

// Find the right-most obstacle so respawns keep their spacing
static int16_t game1_get_farthest_obstacle_x(
    const Game1State *state
)
{
    int16_t farthest_x = 0;
    uint8_t i;

    // Used so respawned objects stay ahead of the rest
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        if (state->obstacles[i].x > farthest_x) {
            farthest_x = state->obstacles[i].x;
        }
    }

    return farthest_x;
}

// Spawn one new object to the right of the screen
static void game1_spawn_obstacle(
    Game1State *state,
    Game1Obstacle *obstacle,
    int16_t start_x
)
{
    uint16_t roll;

    obstacle->x = start_x;
    roll = game1_random_range(state, 0U, 99U);
    // Keep hearts less common than hazards
    if (roll < 14U) {
        obstacle->type = GAME1_OBSTACLE_HEART;
    } else {
        obstacle->type = (uint8_t)game1_random_range(
            state,
            GAME1_OBSTACLE_CACTUS,
            GAME1_OBSTACLE_CRATE
        );
    }
    obstacle->scored = 0U;
}

// Reset player, score, lives and obstacle layout for a fresh run
static void game1_reset(
    Game1State *state
)
{
    uint8_t i;

    // Reset one run but keep best score
    state->phase = GAME1_PHASE_PLAYING;
    state->score = 0U;
    state->lives = GAME1_MAX_LIVES;
    state->speed_px = 3U;
    state->player.x = GAME1_PLAYER_X;
    state->player.y_tenths = GAME1_PLAYER_GROUND_Y * 10;
    state->player.velocity_tenths = 0;
    state->player.on_ground = 1U;
    state->prev_player_y = GAME1_PLAYER_GROUND_Y;
    state->prev_score = 0U;
    state->prev_lives = GAME1_MAX_LIVES;
    state->green_flash_until = 0U;

    state->clouds[0].x = 150;
    state->clouds[0].y = 34;
    state->clouds[1].x = 52;
    state->clouds[1].y = 72;

    // Previous state starts equal to current state
    // Seed the first set of obstacles with some spacing between them
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        game1_spawn_obstacle(
            state,
            &state->obstacles[i],
            (int16_t)(GAME1_SCREEN_W + 50 + (i * 78))
        );
        state->prev_obstacles[i] = state->obstacles[i];
    }
}

// Simple box collision used for player vs obstacle checks
static uint8_t game1_boxes_overlap(
    int16_t ax,
    int16_t ay,
    int16_t aw,
    int16_t ah,
    int16_t bx,
    int16_t by,
    int16_t bw,
    int16_t bh
)
{
    // Simple AABB check
    // If one box sits fully to one side, there is no overlap
    if ((ax + aw) <= bx) {
        return 0U;
    }
    if ((bx + bw) <= ax) {
        return 0U;
    }
    if ((ay + ah) <= by) {
        return 0U;
    }
    if ((by + bh) <= ay) {
        return 0U;
    }

    return 1U;
}

// Handle jump input and vertical motion
static void game1_update_player(
    Game1State *state
)
{
    // Jump only starts from the ground
    if (current_input.btn2_pressed && state->player.on_ground) {
        // Negative velocity sends the player upward
        state->player.velocity_tenths = GAME1_JUMP_VELOCITY_TENTHS;
        state->player.on_ground = 0U;
        buzzer_tone(&buzzer_cfg, 1150, 25);
        HAL_Delay(8);
        buzzer_off(&buzzer_cfg);
    }

    // Apply gravity until the player lands
    if (!state->player.on_ground) {
        // Gravity changes speed first, then position
        state->player.velocity_tenths += GAME1_GRAVITY_TENTHS;
        state->player.y_tenths += state->player.velocity_tenths;

        // Stop exactly on the floor
        if (state->player.y_tenths >= (GAME1_PLAYER_GROUND_Y * 10)) {
            state->player.y_tenths = GAME1_PLAYER_GROUND_Y * 10;
            state->player.velocity_tenths = 0;
            state->player.on_ground = 1U;
        }
    }
}

// Update obstacle positions, scoring, collisions and respawns
static void game1_update_obstacles(
    Game1State *state
)
{
    uint8_t i;
    // Right-most obstacle decides where recycled ones go next
    int16_t farthest_x = game1_get_farthest_obstacle_x(state);

    // Move, score, collide, then recycle off-screen objects
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        Game1Obstacle *obstacle = &state->obstacles[i];
        int16_t obstacle_width = game1_get_obstacle_width(obstacle->type);
        int16_t obstacle_height = game1_get_obstacle_height(obstacle->type);
        int16_t obstacle_y = game1_get_obstacle_y(obstacle->type);

        // Scroll the lane from right to left
        obstacle->x -= (int16_t)state->speed_px;

        // Hearts do not count toward score
        if (!game1_is_collectible(obstacle->type) &&
            !obstacle->scored &&
            (obstacle->x + obstacle_width) < state->player.x) {
            // Score once the player has fully passed the obstacle
            obstacle->scored = 1U;
            state->score++;
            // Same timer drives the HUD highlight and green LED flash
            state->green_flash_until = HAL_GetTick() + GAME1_SCORE_FLASH_MS;

            if ((state->score % 6U) == 0U && state->speed_px < 10U) {
                // Speed ramps up slowly as the run gets longer
                state->speed_px++;
            }
        }

        // Use a slightly smaller hitbox than the sprite art
        if (game1_boxes_overlap(
                (int16_t)(state->player.x + 6),
                (int16_t)((state->player.y_tenths / 10) + 4),
                (int16_t)(GAME1_PLAYER_DRAW_W - 12),
                (int16_t)(GAME1_PLAYER_DRAW_H - 6),
                (int16_t)(obstacle->x + 3),
                (int16_t)(obstacle_y + 3),
                (int16_t)(obstacle_width - 6),
                (int16_t)(obstacle_height - 3)
            )) {
            // Choose the next spacing before this object is recycled
            uint16_t gap = game1_random_range(state, 144U, 252U);

            if (game1_is_collectible(obstacle->type)) {
                // Heart gives one life, up to the cap
                if (state->lives < GAME1_MAX_LIVES) {
                    state->lives++;
                }
                // Use the same visual cue as scoring
                state->green_flash_until = HAL_GetTick() + GAME1_SCORE_FLASH_MS;
                buzzer_tone(&buzzer_cfg, 1200, 20);
                HAL_Delay(20);
                buzzer_off(&buzzer_cfg);
                // Remove the heart and place a new object farther ahead
                game1_spawn_obstacle(state, obstacle, (int16_t)(farthest_x + gap));
                // Update the new right-most point after respawn
                farthest_x = obstacle->x;
                continue;
            }

            if (state->lives > 0U) {
                // Spend one life on a hit
                state->lives--;
            }

            // Normal obstacle takes a life
            if (state->lives == 0U) {
                // Last life spent ends the run
                state->phase = GAME1_PHASE_GAME_OVER;
                buzzer_tone(&buzzer_cfg, 180, 35);
                HAL_Delay(140);
                buzzer_off(&buzzer_cfg);
            } else {
                // Keep the run going and recycle the obstacle
                buzzer_tone(&buzzer_cfg, 260, 25);
                HAL_Delay(35);
                buzzer_off(&buzzer_cfg);
                game1_spawn_obstacle(state, obstacle, (int16_t)(farthest_x + gap));
                // Update the new right-most point after respawn
                farthest_x = obstacle->x;
                continue;
            }
        }

        if ((obstacle->x + obstacle_width) < 0) {
            uint16_t gap = game1_random_range(state, 144U, 252U);
            // Once an object leaves the screen, reuse it ahead of the pack
            game1_spawn_obstacle(state, obstacle, (int16_t)(farthest_x + gap));
            // Update the new right-most point after respawn
            farthest_x = obstacle->x;
        }
    }
}

// Draw the top status bar
static void game1_render_hud(
    const Game1State *state
)
{
    char score_str[32];
    char best_str[32];
    char lives_str[16];
    // Score flashes while the green LED flash window is active
    uint8_t score_colour =
        (state->green_flash_until != 0U && HAL_GetTick() < state->green_flash_until) ? 10 : 1;

    // Build the HUD text each frame it needs updating
    sprintf(score_str, "SCORE %lu", state->score);
    sprintf(best_str, "BEST %lu", state->best_score);
    sprintf(lives_str, "LIFE %u", state->lives);

    // Fixed positions keep the top bar easy to read
    LCD_printString("HOPPING DINOSAUR", 14, 10, 1, 1);
    LCD_printString(score_str, 14, 24, score_colour, 1);
    LCD_printString(lives_str, 92, 24, 1, 1);
    LCD_printString(best_str, 150, 24, 1, 1);
}

// Draw the ground band and repeat the floor tile across it
static void game1_render_ground(void)
{
    int16_t tile_x;

    LCD_Draw_Rect(
        0,
        GAME1_GROUND_Y,
        GAME1_SCREEN_W,
        (GAME1_SCREEN_H - GAME1_GROUND_Y),
        13,
        1
    );

    for (tile_x = 0; tile_x < GAME1_SCREEN_W; tile_x += 18) {
        // Repeat one tile across the whole floor band
        game1_draw_sprite_clipped(tile_x, GAME1_GROUND_Y, 18, 18, kenney_floor_tile, 1);
    }
}

// Draw the full static scene used at the start of a run
static void game1_draw_static_scene(
    const Game1State *state
)
{
    uint8_t i;

    // Draw the parts that do not change every frame
    LCD_Fill_Buffer(0);
    // Blue top banner
    LCD_Draw_Rect(0, 0, GAME1_SCREEN_W, 70, 9, 1);
    // Black play field
    LCD_Draw_Rect(0, 70, GAME1_SCREEN_W, (GAME1_GROUND_Y - 70), 0, 1);
    game1_draw_banner_accent(0, GAME1_SCREEN_W);

    for (i = 0; i < 2U; i++) {
        // Clouds are static, so draw them once here
        game1_draw_sprite_clipped(
            state->clouds[i].x,
            state->clouds[i].y,
            6,
            12,
            cloud_sprite,
            GAME1_CLOUD_SCALE
        );
    }

    game1_render_ground();
    game1_render_hud(state);
}

// Draw one gameplay frame using partial redraws
static void game1_render_playing(
    Game1State *state
)
{
    uint8_t i;
    const uint8_t *player_sprite = kenney_runner_a;

    // Redraw only the areas that changed to cut down flicker
    // First clear the old player position
    game1_redraw_background_region(
        state->player.x - 2,
        state->prev_player_y - 2,
        GAME1_PLAYER_DRAW_W + 4,
        GAME1_PLAYER_DRAW_H + 4
    );

    // Restore old obstacle areas before drawing the new frame
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        int16_t prev_width = game1_get_obstacle_width(state->prev_obstacles[i].type);
        int16_t prev_height = game1_get_obstacle_height(state->prev_obstacles[i].type);
        int16_t prev_y = game1_get_obstacle_y(state->prev_obstacles[i].type);

        // Then clear where each obstacle used to be
        game1_redraw_background_region(
            state->prev_obstacles[i].x - 2,
            prev_y - 2,
            prev_width + 4,
            prev_height + 4
        );
    }

    // Draw current obstacle positions
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        const uint8_t *sprite = game1_get_obstacle_sprite(state->obstacles[i].type);
        int16_t y = game1_get_obstacle_y(state->obstacles[i].type);

        // Obstacles are drawn after the background is restored
        game1_draw_sprite_clipped(
            state->obstacles[i].x,
            y,
            18,
            18,
            sprite,
            GAME1_OBSTACLE_SCALE
        );
    }

    // Draw the player on top
    game1_draw_sprite_clipped(
        state->player.x,
        (int16_t)(state->player.y_tenths / 10),
        GAME1_PLAYER_H,
        GAME1_PLAYER_W,
        player_sprite,
        GAME1_PLAYER_SCALE
    );

    // HUD only needs redrawing when score or life changes
    if (state->score != state->prev_score || state->lives != state->prev_lives) {
        game1_fill_rect_clipped(0, 0, GAME1_SCREEN_W, 34, 9);
        game1_render_hud(state);
    }

    // Save positions for the next partial redraw pass
    // Next frame uses these values as the "old" positions
    state->prev_player_y = (int16_t)(state->player.y_tenths / 10);
    state->prev_score = state->score;
    state->prev_lives = state->lives;
    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        state->prev_obstacles[i] = state->obstacles[i];
    }

    LCD_Refresh(&cfg0);
}

// Draw the frozen game-over view and menu box
static void game1_render_game_over(
    const Game1State *state
)
{
    char score_str[32];
    char best_str[32];
    uint8_t i;
    const uint8_t *player_sprite = kenney_runner_a;

    // Game over screen is drawn in one pass to avoid flashing
    LCD_Fill_Buffer(0);

    // Rebuild the scene under the message box
    LCD_Draw_Rect(0, 0, GAME1_SCREEN_W, 70, 9, 1);
    LCD_Draw_Rect(0, 70, GAME1_SCREEN_W, (GAME1_GROUND_Y - 70), 0, 1);

    for (i = 0; i < 2U; i++) {
        // Clouds belong to the scene behind the message box
        game1_draw_sprite_clipped(
            state->clouds[i].x,
            state->clouds[i].y,
            6,
            12,
            cloud_sprite,
            GAME1_CLOUD_SCALE
        );
    }

    game1_render_ground();

    for (i = 0; i < GAME1_MAX_OBSTACLES; i++) {
        const uint8_t *sprite = game1_get_obstacle_sprite(state->obstacles[i].type);
        int16_t y = game1_get_obstacle_y(state->obstacles[i].type);

        // Obstacles stay frozen where the run ended
        game1_draw_sprite_clipped(
            state->obstacles[i].x,
            y,
            18,
            18,
            sprite,
            GAME1_OBSTACLE_SCALE
        );
    }

    // Draw the player in the crash frame
    game1_draw_sprite_clipped(
        state->player.x,
        (int16_t)(state->player.y_tenths / 10),
        GAME1_PLAYER_H,
        GAME1_PLAYER_W,
        player_sprite,
        GAME1_PLAYER_SCALE
    );

    game1_render_hud(state);

    // Box and text for end-of-run actions
    LCD_Draw_Rect(20, 74, 200, 112, 0, 1);
    LCD_Draw_Rect(20, 74, 200, 112, 1, 0);
    LCD_printString("GAME OVER", 50, 88, 2, 2);

    sprintf(score_str, "Score: %lu", state->score);
    sprintf(best_str, "Best: %lu", state->best_score);
    LCD_printString(score_str, 58, 120, 1, 1);
    LCD_printString(best_str, 64, 136, 1, 1);
    LCD_printString("BT2 Retry", 62, 154, 1, 1);
    LCD_printString("BT3 Menu", 68, 168, 1, 1);
    LCD_Refresh(&cfg0);
}

// Intro screen shown before gameplay starts
static void game1_show_intro(void)
{
    static const char *intro_title = "Hopping Dinosaur";
    static const char *intro_body = "BT2 jumps over rocks. Survive longer to raise your score.";
    static const char *intro_prompt = "Press BT3 to start";

    // Stay here until the player chooses to begin
    while (1) {
        // Make the start prompt pulse a little
        uint32_t blink = (HAL_GetTick() / 400U) % 2U;

        // Poll input on the intro screen too
        Input_Read();
        game1_leds_off();

        // Redraw the full intro each pass so the prompt can blink
        LCD_Fill_Buffer(0);
        LCD_Draw_Rect(0, 0, GAME1_SCREEN_W, 74, 9, 1);
        game1_print_wrapped_centered(
            intro_title,
            18,
            220,
            1,
            3,
            6
        );
        game1_print_wrapped_centered(
            intro_body,
            96,
            210,
            1,
            2,
            6
        );
        // Prompt colour flips so BT3 stands out
        game1_print_wrapped_centered(
            intro_prompt,
            212,
            220,
            blink == 0U ? 1 : 13,
            1,
            4
        );
        LCD_Refresh(&cfg0);

        if (current_input.btn3_pressed) {
            // Leave the intro and start play
            break;
        }

        HAL_Delay(30);
    }
}

// Entry point called by the menu
MenuState Game1_Run(void)
{
    Game1State state;

    // Best score survives retries while this game session is open
    state.best_score = 0U;
    // Simple changing seed for obstacle order and spacing
    state.rng_state = HAL_GetTick() ^ 0x2645A51EU;
    game1_leds_off();

    // Brief start sound before the intro screen
    buzzer_tone(&buzzer_cfg, 900, 24);
    HAL_Delay(50);
    buzzer_off(&buzzer_cfg);

    // Do not create the run state until the intro is accepted
    game1_show_intro();
    game1_reset(&state);
    game1_draw_static_scene(&state);
    LCD_Refresh(&cfg0);

    while (1) {
        // Used below to enforce a steady frame time
        uint32_t frame_start = HAL_GetTick();

        // Read buttons once per frame
        Input_Read();
        game1_update_leds(&state);

        if (current_input.btn3_pressed) {
            // BT3 always exits back to the menu
            game1_leds_off();
            return MENU_STATE_HOME;
        }

        // Main loop: update, draw, then hold the frame time
        if (state.phase == GAME1_PHASE_PLAYING) {
            // Update motion first
            game1_update_player(&state);
            game1_update_obstacles(&state);

            // Track the best score while the run is active
            if (state.score > state.best_score) {
                state.best_score = state.score;
            }

            // Then draw the live frame
            game1_render_playing(&state);
        } else {
            if (state.score > state.best_score) {
                // Keep best score updated after the crash too
                state.best_score = state.score;
            }

            // Frozen scene plus retry/menu box
            game1_render_game_over(&state);

            if (current_input.btn2_pressed) {
                // Retry starts a fresh run without leaving the game
                game1_reset(&state);
                game1_draw_static_scene(&state);
                LCD_Refresh(&cfg0);
            }
        }

        // Hold a fixed frame time for stable game speed
        if ((HAL_GetTick() - frame_start) < GAME1_FRAME_TIME_MS) {
            HAL_Delay(GAME1_FRAME_TIME_MS - (HAL_GetTick() - frame_start));
        }
    }
}
