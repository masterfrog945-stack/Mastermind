#ifndef LED_PATTERNS_H
#define LED_PATTERNS_H

#include <stdint.h>

typedef enum {
    LED_TOP_LEFT = 0,
    LED_TOP_RIGHT,
    LED_BOTTOM_LEFT,
    LED_BOTTOM_RIGHT
} led_pos_t;

typedef enum {
    LED_RED = 0,
    LED_GREEN
} led_color_t;

typedef struct {
    uint16_t a;
    uint16_t b;
    uint16_t c;
} led_pattern_t;

/* row is 1..6 */
led_pattern_t led_get_pattern(uint8_t row, led_pos_t pos, led_color_t color);

#endif