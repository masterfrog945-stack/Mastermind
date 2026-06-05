#include "feedback_sender.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "NRF24L01.h"
#include "NRF24L01_Define.h"

#define PAYLOAD_SIZE    32
#define RF_CHANNEL_PIC  0x72

static const uint8_t pic_addr[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
static bool feedback_ready = false;

/* Persistent output buffer */
static uint16_t current_a = 0x0000;
static uint16_t current_b = 0x0000;
static uint16_t current_c = 0x0000;

static uint8_t reverse_bits8(uint8_t x)
{
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    return x;
}

static uint8_t send_words(uint16_t a, uint16_t b, uint16_t c)
{
    uint8_t a_low;
    uint8_t a_high;
    uint8_t b_low;
    uint8_t b_high;
    uint8_t c_low;
    uint8_t c_high;

    if (!feedback_ready) {
        return 255;
    }

    a_low  = (uint8_t)(a & 0xFF);
    a_high = (uint8_t)((a >> 8) & 0xFF);
    b_low  = (uint8_t)(b & 0xFF);
    b_high = (uint8_t)((b >> 8) & 0xFF);
    c_low  = (uint8_t)(c & 0xFF);
    c_high = (uint8_t)((c >> 8) & 0xFF);

    memset(NRF24L01_TxPacket, 0x00, PAYLOAD_SIZE);

    /* Receiver expects: [A low, A high, B low, B high, C low, C high] */
    NRF24L01_TxPacket[0] = reverse_bits8(a_low);
    NRF24L01_TxPacket[1] = reverse_bits8(a_high);
    NRF24L01_TxPacket[2] = reverse_bits8(b_low);
    NRF24L01_TxPacket[3] = reverse_bits8(b_high);
    NRF24L01_TxPacket[4] = reverse_bits8(c_low);
    NRF24L01_TxPacket[5] = reverse_bits8(c_high);

    return NRF24L01_Send();
}

void feedback_init(void)
{
    NRF24L01_GPIO_Init();
    NRF24L01_SPI_Init();
    NRF24L01_Init();

    vTaskDelay(pdMS_TO_TICKS(100));

    memcpy(NRF24L01_TxAddress, pic_addr, 5);
    memcpy(NRF24L01_RxAddress, pic_addr, 5);

    NRF24L01_WriteReg(NRF24L01_RF_CH, RF_CHANNEL_PIC);
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x70);
    NRF24L01_FlushTx();
    NRF24L01_FlushRx();

    current_a = 0x0000;
    current_b = 0x0000;
    current_c = 0x0000;

    feedback_ready = true;
    printf("Feedback sender ready\r\n");
}

uint8_t feedback_send_current(void)
{
    return send_words(current_a, current_b, current_c);
}

uint8_t feedback_clear(void)
{
    current_a = 0x0000;
    current_b = 0x0000;
    current_c = 0x0000;
    return send_words(current_a, current_b, current_c);
}

uint8_t send_feedback_raw(uint16_t a, uint16_t b, uint16_t c)
{
    /* Add bits to existing buffer */
    current_a |= a;
    current_b |= b;
    current_c |= c;

    return send_words(current_a, current_b, current_c);
}

uint8_t send_feedback(uint8_t row, led_pos_t pos, led_color_t color)
{
    led_pattern_t p = led_get_pattern(row, pos, color);
    return send_feedback_raw(p.a, p.b, p.c);
}