#include "Menu.h"
#include "LCD.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "PWM.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;  // LCD configuration from main.c
extern Joystick_cfg_t joystick_cfg;  // Joystick configuration
extern Joystick_t joystick_data;     // Current joystick readings
extern PWM_cfg_t pwm_cfg;            // PWM LED on PB6
extern PWM_cfg_t pwm_green_cfg;      // PWM LED on PB8

// Menu options
static const char* menu_options[] = {
    "Hopping Dinosaur",
    "AimLab", 
    "Air Battle"
};
#define NUM_MENU_OPTIONS 3

// Frame rate for menu (in milliseconds)
#define MENU_FRAME_TIME_MS 30  // ~33 FPS

static uint16_t LCD_TextWidthPx(const char *text, uint8_t font_size) {
    return (uint16_t)(strlen(text) * 6U * font_size);
}

static void LCD_PrintWrapped(const char *text, uint16_t y, uint16_t max_width,
                             uint8_t colour, uint8_t font_size,
                             uint16_t line_spacing) {
    const size_t max_chars_per_line = max_width / (6U * font_size);
    char line_buffer[64];
    const char *cursor = text;

    if (max_chars_per_line == 0U) {
        return;
    }

    while (*cursor != '\0') {
        while (*cursor == ' ') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        size_t line_len = 0;
        const char *line_start = cursor;
        const char *last_space = NULL;

        while (*cursor != '\0' && line_len < max_chars_per_line) {
            if (*cursor == ' ') {
                last_space = cursor;
            }
            cursor++;
            line_len++;
        }

        if (*cursor != '\0' && line_len == max_chars_per_line && last_space != NULL) {
            line_len = (size_t)(last_space - line_start);
            cursor = last_space + 1;
        }

        if (line_len >= sizeof(line_buffer)) {
            line_len = sizeof(line_buffer) - 1U;
        }

        memcpy(line_buffer, line_start, line_len);
        line_buffer[line_len] = '\0';

        uint16_t text_width = LCD_TextWidthPx(line_buffer, font_size);
        uint16_t x = 0;
        if (text_width < 240U) {
            x = (uint16_t)((240U - text_width) / 2U);
        }

        LCD_printString(line_buffer, x, y, colour, font_size);
        y += (7U * font_size) + line_spacing;
    }
}

/**
 * @brief Render the home menu screen
 */
static void render_home_menu(MenuSystem* menu) {
    uint32_t blink_phase = (HAL_GetTick() / 500U) % 2U;
    static const uint16_t option_y_positions[NUM_MENU_OPTIONS] = {64, 122, 174};

    // PB6 and PB8 flash at 1 Hz while the menu is visible.
    PWM_SetDuty(&pwm_cfg, blink_phase == 0U ? 100 : 0);
    PWM_SetDuty(&pwm_green_cfg, blink_phase == 0U ? 100 : 0);

    LCD_Fill_Buffer(0);
    
    // Title
    LCD_PrintWrapped("Choose a Game!", 10, 220, 1, 2, 6);
    
    // Menu options with selection highlight
    for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
        uint16_t y_pos = option_y_positions[i];
        uint8_t text_size = 2;
        
        if (i == menu->selected_option) {
            LCD_printString(">", 40, y_pos, 1, text_size);
        }
        
        LCD_PrintWrapped(menu_options[i], y_pos, 150, 1, text_size, 4);
    }
    
    LCD_PrintWrapped("Press BT3", 220, 220, blink_phase == 0U ? 1 : 13, 1, 4);

    LCD_Refresh(&cfg0);
}

// ==============================================
// PUBLIC API IMPLEMENTATION
// ==============================================

void Menu_Init(MenuSystem* menu) {
    menu->selected_option = 0;
}

MenuState Menu_Run(MenuSystem* menu) {
    static Direction last_direction = CENTRE;  // Track last direction for debouncing
    MenuState selected_game = MENU_STATE_HOME;  // Which game was selected
    
    // Menu's own loop - runs until game is selected
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // Read input
        Input_Read();
        
        // Read current joystick position
        Joystick_Read(&joystick_cfg, &joystick_data);
        
        // Handle joystick navigation (up/down to select option)
        Direction current_direction = joystick_data.direction;
        
        if (current_direction == S && last_direction != S) {  // Joystick pushed DOWN
            // Move selection down
            menu->selected_option++;
            if (menu->selected_option >= NUM_MENU_OPTIONS) {
                menu->selected_option = 0;  // Wrap around
            }
        } 
        else if (current_direction == N && last_direction != N) {  // Joystick pushed UP
            // Move selection up
            if (menu->selected_option == 0) {
                menu->selected_option = NUM_MENU_OPTIONS - 1;  // Wrap around
            } else {
                menu->selected_option--;
            }
        }
        
        last_direction = current_direction;
        
        // Handle button press to select current option
        if (current_input.btn3_pressed) {
            // User pressed button - select the highlighted option
            if (menu->selected_option == 0) {
                selected_game = MENU_STATE_GAME_1;
            } else if (menu->selected_option == 1) {
                selected_game = MENU_STATE_GAME_2;
            } else if (menu->selected_option == 2) {
                selected_game = MENU_STATE_GAME_3;
            }
            break;  // Exit menu loop - game selected!
        }
        
        // Render menu
        render_home_menu(menu);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < MENU_FRAME_TIME_MS) {
            HAL_Delay(MENU_FRAME_TIME_MS - frame_time);
        }
    }
    
    return selected_game;  // Return which game was selected
}
