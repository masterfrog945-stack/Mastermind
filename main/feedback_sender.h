#ifndef FEEDBACK_SENDER_H
#define FEEDBACK_SENDER_H

#include <stdint.h>
#include "led_patterns.h"

void feedback_init(void);

/* Add one logical LED to the current output and send updated buffer */
uint8_t send_feedback(uint8_t row, led_pos_t pos, led_color_t color);

/* Add raw bits to current buffer and send updated buffer */
uint8_t send_feedback_raw(uint16_t a, uint16_t b, uint16_t c);

/* Clear all buffered bits and send all-off */
uint8_t feedback_clear(void);

/* Resend current buffered state without changing it */
uint8_t feedback_send_current(void);

#endif