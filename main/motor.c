#include "motor.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

// ========= Motor pins =========
#define IN1 GPIO_NUM_5
#define IN2 GPIO_NUM_6
#define IN3 GPIO_NUM_7
#define IN4 GPIO_NUM_8

// ========= Timing =========
#define RUN_TIME_MS_1      4900
#define RUN_TIME_MS_2      4950
#define RUN_TIME_MS_3      4950
#define RUN_TIME_MS_4      4950
#define RUN_TIME_MS_5      4950
#define RUN_TIME_MS_6      4950
#define REVERSE_TIME_MS   700
#define STEP_DELAY_US    2250
// ==========================

void motor_init(void)
{
    gpio_set_direction(IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN4, GPIO_MODE_OUTPUT);

    motor_off();
}

void motor_off(void)
{
    gpio_set_level(IN1, 0);
    gpio_set_level(IN2, 0);
    gpio_set_level(IN3, 0);
    gpio_set_level(IN4, 0);
}

void step_once(int clockwise)
{
    if (clockwise) {
        gpio_set_level(IN1, 1); gpio_set_level(IN2, 0); gpio_set_level(IN3, 0); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 0); gpio_set_level(IN2, 1); gpio_set_level(IN3, 0); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 0); gpio_set_level(IN2, 0); gpio_set_level(IN3, 1); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 0); gpio_set_level(IN2, 0); gpio_set_level(IN3, 0); gpio_set_level(IN4, 1);
        esp_rom_delay_us(STEP_DELAY_US);
    } else {
        gpio_set_level(IN1, 0); gpio_set_level(IN2, 0); gpio_set_level(IN3, 0); gpio_set_level(IN4, 1);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 0); gpio_set_level(IN2, 0); gpio_set_level(IN3, 1); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 0); gpio_set_level(IN2, 1); gpio_set_level(IN3, 0); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);

        gpio_set_level(IN1, 1); gpio_set_level(IN2, 0); gpio_set_level(IN3, 0); gpio_set_level(IN4, 0);
        esp_rom_delay_us(STEP_DELAY_US);
    }
}

void motor_run_1(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_1);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}

void motor_run_2(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_2);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}
void motor_run_3(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_3);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}
void motor_run_4(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_4);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}
void motor_run_5(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_5);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}
void motor_run_6(motor_direction_t direction)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t run_ticks = pdMS_TO_TICKS(RUN_TIME_MS_6);

    while ((xTaskGetTickCount() - start_ticks) < run_ticks) {
        step_once((int)direction);
    }

    /* Only reverse briefly if original direction was CW */
    if (direction == MOTOR_CW) {
        TickType_t reverse_start = xTaskGetTickCount();
        TickType_t reverse_ticks = pdMS_TO_TICKS(REVERSE_TIME_MS);

        while ((xTaskGetTickCount() - reverse_start) < reverse_ticks) {
            step_once(MOTOR_CCW);
        }
    }

    motor_off();
}

void stepper_test(void *pvParameters)
{
    (void) pvParameters;
    motor_init();
    motor_run_5(MOTOR_CW);
    vTaskDelete(NULL);
}