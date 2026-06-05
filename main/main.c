#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#include "NRF24L01.h"
#include "NRF24L01_Define.h"
#include "led_patterns.h"
#include "motor.h"
#include "feedback_sender.h"

#define TAG "NRF_WIFI_TCP_GAME"

/* =========================================================
 * Wi-Fi / TCP configuration
 * ========================================================= */
#define WIFI_SSID       "open"
#define WIFI_PASS       "oneplus12"
#define TCP_PORT        5000

#define JSON_SEND_INTERVAL_MS 1000

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY_COUNT    10

/* =========================================================
 * Game settings
 * ========================================================= */
#define MAX_ROWS 6
#define CODE_LEN 4

#define NEXT_ROW_DIRECTION   MOTOR_CW
#define RETURN_DIRECTION     MOTOR_CCW

#define SENSOR_RF_CHANNEL    0x72

/* =========================================================
 * Global variables
 * ========================================================= */
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static uint8_t g_sensor_payload[16] = {0};

static SemaphoreHandle_t g_data_mutex = NULL;
static SemaphoreHandle_t g_nrf_mutex = NULL;
static bool g_has_sensor_data = false;
static bool g_nrf_ready = false;

/* Fixed answer for now: RED RED GREEN GREEN */
static int g_secret_code[CODE_LEN] = {1, 1, 2, 2};

static uint8_t g_current_row = 1;

static int g_row_exact[MAX_ROWS] = {0};
static int g_row_color_only[MAX_ROWS] = {0};

static const uint8_t sensor_addr[5] = {0x01, 0x02, 0x03, 0x04, 0x05};

/* =========================================================
 * Color classification
 * ========================================================= */
typedef enum {
    COLOR_UNDEFINED = 0,
    COLOR_RED       = 1,
    COLOR_GREEN     = 2,
    COLOR_BLUE      = 3,
    COLOR_YELLOW    = 4,
    COLOR_CYAN      = 5,
    COLOR_MAGENTA   = 6
} app_color_t;

static const char *color_id_to_name(int color)
{
    switch (color) {
        case COLOR_RED:     return "RED";
        case COLOR_GREEN:   return "GREEN";
        case COLOR_BLUE:    return "BLUE";
        case COLOR_YELLOW:  return "YELLOW";
        case COLOR_CYAN:    return "CYAN";
        case COLOR_MAGENTA: return "MAGENTA";
        default:            return "UNDEFINED";
    }
}

static app_color_t check_colour_from_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (red > 100) {
        if (green > 100) {
            if (blue < 100) {
                return COLOR_YELLOW;
            }
        } else if (blue > 100) {
            return COLOR_MAGENTA;
        } else {
            return COLOR_RED;
        }
    } else {
        if (green > 100) {
            if (blue > 100) {
                return COLOR_CYAN;
            } else {
                return COLOR_GREEN;
            }
        } else if (blue > 100) {
            return COLOR_BLUE;
        }
    }

    return COLOR_UNDEFINED;
}

/* =========================================================
 * Debug helpers
 * ========================================================= */
static void parse_and_print_rgb_4sensors(const uint8_t *p, int len)
{
    if (p == NULL || len < 16) {
        printf("Payload too short: %d bytes, need 16 bytes\n", len);
        return;
    }

    for (int s = 0; s < 4; s++) {
        uint8_t r = p[s * 4 + 0];
        uint8_t g = p[s * 4 + 1];
        uint8_t b = p[s * 4 + 2];
        uint8_t c = p[s * 4 + 3];

        printf("S%d: R=%3u G=%3u B=%3u C=%3u\n",
               s + 1, r, g, b, c);
    }
}

static void get_guess_colors(int colors[4])
{
    colors[0] = COLOR_UNDEFINED;
    colors[1] = COLOR_UNDEFINED;
    colors[2] = COLOR_UNDEFINED;
    colors[3] = COLOR_UNDEFINED;

    uint8_t local_payload[16] = {0};
    bool has_data = false;

    if (g_data_mutex != NULL &&
        xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

        memcpy(local_payload, g_sensor_payload, 16);
        has_data = g_has_sensor_data;

        xSemaphoreGive(g_data_mutex);
    }

    if (!has_data) {
        ESP_LOGW(TAG, "No NRF data yet. Sending undefined colors.");
        return;
    }

    for (int i = 0; i < 4; i++) {
        uint8_t r = local_payload[i * 4 + 0];
        uint8_t g = local_payload[i * 4 + 1];
        uint8_t b = local_payload[i * 4 + 2];
        uint8_t c = local_payload[i * 4 + 3];

        colors[i] = check_colour_from_rgb(r, g, b);

        ESP_LOGI(TAG,
                 "Classified S%d: R=%u G=%u B=%u C=%u -> colorId=%d (%s)",
                 i + 1, r, g, b, c, colors[i], color_id_to_name(colors[i]));
    }
}

/* =========================================================
 * Mastermind scoring
 * ========================================================= */
static void evaluate_guess(const int guess[4], int *exact, int *color_only)
{
    int secret_count[7] = {0};
    int guess_count[7]  = {0};

    *exact = 0;
    *color_only = 0;

    for (int i = 0; i < CODE_LEN; i++) {
        if (guess[i] == g_secret_code[i]) {
            (*exact)++;
        } else {
            if (g_secret_code[i] >= 1 && g_secret_code[i] <= 6) {
                secret_count[g_secret_code[i]]++;
            }
            if (guess[i] >= 1 && guess[i] <= 6) {
                guess_count[guess[i]]++;
            }
        }
    }

    for (int c = 1; c <= 6; c++) {
        *color_only += (secret_count[c] < guess_count[c]) ? secret_count[c] : guess_count[c];
    }
}

/* =========================================================
 * NRF sensor RX restore
 * =========================================================
 * This is the important fix:
 * after LED feedback TX, fully re-enter sensor RX path.
 */
static void nrf_restore_sensor_rx_mode(void)
{
    memcpy(NRF24L01_TxAddress, sensor_addr, 5);
    memcpy(NRF24L01_RxAddress, sensor_addr, 5);

    /*
     * Re-run the important configuration for sensor RX.
     * This is intentionally more complete than only calling NRF24L01_Rx().
     */
    NRF24L01_Init();

    memcpy(NRF24L01_TxAddress, sensor_addr, 5);
    memcpy(NRF24L01_RxAddress, sensor_addr, 5);

    NRF24L01_WriteReg(NRF24L01_RF_CH, SENSOR_RF_CHANNEL);
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x70);
    NRF24L01_FlushTx();
    NRF24L01_FlushRx();

    NRF24L01_Rx();

    ESP_LOGI(TAG, "NRF fully restored to sensor RX mode.");
}

/* =========================================================
 * Wait for a fresh sensor frame after moving to next row
 * =========================================================
 * Fix for stale APP color display:
 * after stepper moves, clear old-data flag and wait for next sensor frame.
 */
static bool wait_for_new_sensor_frame(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        bool has_data = false;

        if (g_data_mutex != NULL &&
            xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            has_data = g_has_sensor_data;
            xSemaphoreGive(g_data_mutex);
        }

        if (has_data) {
            ESP_LOGI(TAG, "New sensor frame received after row movement.");
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGW(TAG, "Timeout waiting for new sensor frame.");
    return false;
}

/* =========================================================
 * LED feedback replay using teammate's feedback_sender
 * ========================================================= */
static void replay_feedback_board_up_to_row(uint8_t last_row)
{
    if (!g_nrf_ready || g_nrf_mutex == NULL) {
        ESP_LOGW(TAG, "NRF not ready, skip LED feedback replay.");
        return;
    }

    if (xSemaphoreTake(g_nrf_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take NRF mutex for LED feedback.");
        return;
    }

    ESP_LOGI(TAG, "LED replay start, last_row=%d", last_row);

    /*
     * Use teammate's original feedback sender path.
     * Important: give the radio / PIC side enough time.
     */
    feedback_init();
    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t ret = feedback_clear();
    ESP_LOGI(TAG, "feedback_clear ret = %u", ret);
    vTaskDelay(pdMS_TO_TICKS(150));

    for (uint8_t row = 1; row <= last_row; row++) {
        int exact = g_row_exact[row - 1];
        int color_only = g_row_color_only[row - 1];

        led_pos_t order[4] = {
            LED_TOP_LEFT,
            LED_TOP_RIGHT,
            LED_BOTTOM_LEFT,
            LED_BOTTOM_RIGHT
        };

        int idx = 0;

        ESP_LOGI(TAG, "Replay row %d -> exact=%d color_only=%d", row, exact, color_only);

        /* First green LEDs = exact matches */
        for (int i = 0; i < exact && idx < 4; i++) {
            ret = send_feedback(row, order[idx], LED_GREEN);
            ESP_LOGI(TAG, "send_feedback row=%d pos=%d GREEN ret=%u",
                     row, order[idx], ret);
            idx++;

            /*
             * IMPORTANT:
             * Do not send too fast.
             */
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        /* Then red LEDs = color only matches */
        for (int i = 0; i < color_only && idx < 4; i++) {
            ret = send_feedback(row, order[idx], LED_RED);
            ESP_LOGI(TAG, "send_feedback row=%d pos=%d RED ret=%u",
                     row, order[idx], ret);
            idx++;

            vTaskDelay(pdMS_TO_TICKS(120));
        }

        /*
         * Small gap between rows
         */
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    /*
     * Give receiver side time to settle before switching back to RX.
     */
    vTaskDelay(pdMS_TO_TICKS(300));

    nrf_restore_sensor_rx_mode();
    vTaskDelay(pdMS_TO_TICKS(80));

    xSemaphoreGive(g_nrf_mutex);

    ESP_LOGI(TAG, "LED feedback replay done, RX restored.");
}
/* =========================================================
 * Reset game state
 * ========================================================= */
static void start_new_game(void)
{
    g_current_row = 1;

    g_secret_code[0] = COLOR_RED;
    g_secret_code[1] = COLOR_RED;
    g_secret_code[2] = COLOR_GREEN;
    g_secret_code[3] = COLOR_GREEN;

    memset(g_row_exact, 0, sizeof(g_row_exact));
    memset(g_row_color_only, 0, sizeof(g_row_color_only));

    ESP_LOGI(TAG, "New game started. Secret code fixed to [RED, RED, GREEN, GREEN].");

    replay_feedback_board_up_to_row(0);
}

/* =========================================================
 * Stepper helpers
 * ========================================================= */
/* =========================================================
 * Stepper helpers
 * =========================================================
 * New motor logic:
 * - Moving from row 1 -> row 2 uses motor_run_1()
 * - Moving from row 2 -> row 3 uses motor_run_2()
 * - ...
 * - Moving from row 5 -> row 6 uses motor_run_5()
 *
 * When the 6th round is finished and we want to go back to origin,
 * call the motor functions in reverse order:
 * motor_run_6() ... motor_run_1()
 */

static void move_to_next_row(uint8_t current_row)
{
    ESP_LOGI(TAG, "Moving stepper forward from row %d", current_row);

    switch (current_row) {
        case 1:
            motor_run_1(NEXT_ROW_DIRECTION);
            break;
        case 2:
            motor_run_2(NEXT_ROW_DIRECTION);
            break;
        case 3:
            motor_run_3(NEXT_ROW_DIRECTION);
            break;
        case 4:
            motor_run_4(NEXT_ROW_DIRECTION);
            break;
        case 5:
            motor_run_5(NEXT_ROW_DIRECTION);
            break;
        default:
            ESP_LOGW(TAG, "move_to_next_row called with invalid row=%d", current_row);
            break;
    }

    motor_off();
}

static void return_to_first_row(uint8_t current_row)
{
    if (current_row <= 1) {
        ESP_LOGI(TAG, "Already at row 1, no need to return.");
        return;
    }

    ESP_LOGI(TAG, "Returning from row %d back to row 1", current_row);

    /*
     * If current_row == 6:
     * call 6,5,4,3,2,1 in reverse direction
     *
     * If current_row == 4:
     * call 4,3,2,1 in reverse direction
     */
    for (int row = current_row; row >= 1; row--) {
        switch (row) {
            case 1:
                motor_run_1(RETURN_DIRECTION);
                break;
            case 2:
                motor_run_2(RETURN_DIRECTION);
                break;
            case 3:
                motor_run_3(RETURN_DIRECTION);
                break;
            case 4:
                motor_run_4(RETURN_DIRECTION);
                break;
            case 5:
                motor_run_5(RETURN_DIRECTION);
                break;
            case 6:
                motor_run_6(RETURN_DIRECTION);
                break;
            default:
                break;
        }

        motor_off();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
/* =========================================================
 * JSON helpers
 * ========================================================= */
static bool send_all(int sock, const uint8_t *data, size_t len)
{
    size_t total_sent = 0;

    while (total_sent < len) {
        int sent = send(sock, data + total_sent, len - total_sent, 0);

        if (sent < 0) {
            ESP_LOGE(TAG, "send failed, errno=%d", errno);
            return false;
        }

        if (sent == 0) {
            ESP_LOGW(TAG, "send returned 0, client may be disconnected.");
            return false;
        }

        total_sent += sent;
    }

    return true;
}

static bool send_json_line(int sock, const char *json)
{
    if (json == NULL) {
        return false;
    }

    char line[256];
    int len = snprintf(line, sizeof(line), "%s\n", json);

    if (len <= 0 || len >= (int)sizeof(line)) {
        ESP_LOGE(TAG, "JSON line too long or snprintf failed.");
        return false;
    }

    ESP_LOGI(TAG, "SEND -> %s", json);

    return send_all(sock, (const uint8_t *)line, (size_t)len);
}

static bool send_guess_json(int sock)
{
    int colors[4];
    get_guess_colors(colors);

    char json[128];
    snprintf(json,
             sizeof(json),
             "{\"type\":\"GUESS\",\"colors\":[%d,%d,%d,%d]}",
             colors[0], colors[1], colors[2], colors[3]);

    return send_json_line(sock, json);
}

static bool send_feedback_json_dynamic(int sock, int exact, int color_only, int attempts_left, const char *game_state)
{
    char json[192];

    snprintf(json,
             sizeof(json),
             "{\"type\":\"FEEDBACK\",\"exact\":%d,\"color\":%d,\"attemptsLeft\":%d,\"gameState\":\"%s\"}",
             exact, color_only, attempts_left, game_state);

    return send_json_line(sock, json);
}

/* =========================================================
 * APP command handler
 * ========================================================= */
static void handle_app_message(int sock, const uint8_t *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }

    ESP_LOGI(TAG, "Received %d bytes from APP", len);

    char msg[256] = {0};
    int copy_len = len;

    if (copy_len >= (int)sizeof(msg)) {
        copy_len = sizeof(msg) - 1;
    }

    memcpy(msg, data, copy_len);
    msg[copy_len] = '\0';

    ESP_LOGI(TAG, "RECEIVED FROM APP -> %s", msg);

    if (strstr(msg, "\"cmd\":\"SUBMIT_GUESS\"") != NULL ||
        strstr(msg, "SUBMIT_GUESS") != NULL) {

        int guess[4];
        int exact = 0;
        int color_only = 0;
        int attempts_left = 0;
        const char *game_state = "PLAYING";

        get_guess_colors(guess);

        ESP_LOGI(TAG,
                 "Submitting guess on row %d: [%d,%d,%d,%d]",
                 g_current_row, guess[0], guess[1], guess[2], guess[3]);

        evaluate_guess(guess, &exact, &color_only);

        g_row_exact[g_current_row - 1] = exact;
        g_row_color_only[g_current_row - 1] = color_only;

        replay_feedback_board_up_to_row(g_current_row);

        if (exact == 4) {
            game_state = "WON";
            attempts_left = MAX_ROWS - g_current_row;

            send_feedback_json_dynamic(sock, exact, color_only, attempts_left, game_state);

            return_to_first_row(g_current_row);
            start_new_game();

            /* New game starts from row 1, push current guess to APP */
            vTaskDelay(pdMS_TO_TICKS(200));
            send_guess_json(sock);
        }
        else if (g_current_row >= MAX_ROWS) {
            game_state = "LOST";
            attempts_left = 0;

            send_feedback_json_dynamic(sock, exact, color_only, attempts_left, game_state);

            return_to_first_row(g_current_row);
            start_new_game();

            vTaskDelay(pdMS_TO_TICKS(200));
            send_guess_json(sock);
        }
        else {
            game_state = "PLAYING";
            attempts_left = MAX_ROWS - g_current_row;

            send_feedback_json_dynamic(sock, exact, color_only, attempts_left, game_state);

            /*
             * Important fix:
             * After moving to next row, clear old sensor flag,
             * wait for a fresh sensor frame, then immediately send GUESS to APP.
             */
            if (g_data_mutex != NULL &&
                xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_has_sensor_data = false;
                xSemaphoreGive(g_data_mutex);
            }

            move_to_next_row(g_current_row);;
            g_current_row++;

            /* Ensure RX is really active before waiting for fresh data */
            if (g_nrf_mutex != NULL &&
                xSemaphoreTake(g_nrf_mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
                nrf_restore_sensor_rx_mode();
                xSemaphoreGive(g_nrf_mutex);
            }

            wait_for_new_sensor_frame(2000);

            ESP_LOGI(TAG, "Advanced to row %d", g_current_row);

            /* Immediately push the new row's detected colors to APP */
            send_guess_json(sock);
        }
    }
    else if (strstr(msg, "\"cmd\":\"RESET\"") != NULL ||
             strstr(msg, "RESET") != NULL ||
             strstr(msg, "reset") != NULL) {

        ESP_LOGI(TAG, "APP command detected: RESET");
        return_to_first_row(g_current_row);
        start_new_game();
        vTaskDelay(pdMS_TO_TICKS(200));
        send_guess_json(sock);
    }
}

/* =========================================================
 * NRF24L01 receive task
 * ========================================================= */
static void nrf_receive_task(void *arg)
{
    ESP_LOGI(TAG, "Starting NRF24L01 receive task...");

    NRF24L01_GPIO_Init();
    NRF24L01_SPI_Init();
    NRF24L01_Init();

    vTaskDelay(pdMS_TO_TICKS(100));

    if (g_nrf_mutex != NULL &&
        xSemaphoreTake(g_nrf_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        nrf_restore_sensor_rx_mode();
        g_nrf_ready = true;
        xSemaphoreGive(g_nrf_mutex);
    }

    printf("\n--- Start NRF24L01 RX ---\n");

    while (1) {
        uint8_t status = 0;

        if (g_nrf_mutex != NULL &&
            xSemaphoreTake(g_nrf_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            status = NRF24L01_Receive();
            xSemaphoreGive(g_nrf_mutex);
        }

        if (status == 1) {
            if (g_data_mutex != NULL &&
                xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                memcpy(g_sensor_payload, NRF24L01_RxPacket, 16);
                g_has_sensor_data = true;
                xSemaphoreGive(g_data_mutex);
            }

            printf("Received NRF raw16: ");
            for (int i = 0; i < 16; i++) {
                printf("%02X ", NRF24L01_RxPacket[i]);
            }
            printf("\n");

            parse_and_print_rgb_4sensors(NRF24L01_RxPacket, 16);
            printf("-------------------------\n");
        }
        else if (status == 2 || status == 3) {
            ESP_LOGW(TAG, "NRF24L01 receive status error: %d", status);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* =========================================================
 * Wi-Fi event handler
 * ========================================================= */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting to SSID: %s", WIFI_SSID);
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%d", disconnected->reason);

        if (s_retry_num < MAX_RETRY_COUNT) {
            s_retry_num++;
            ESP_LOGW(TAG, "Retrying Wi-Fi connection... (%d/%d)",
                     s_retry_num, MAX_RETRY_COUNT);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi connection failed after max retries.");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Use this IP in Android app with port %d", TCP_PORT);

        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* =========================================================
 * Wi-Fi STA initialization
 * ========================================================= */
static bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group.");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi STA netif.");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL
    ));

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid,
            WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1);

    strncpy((char *)wifi_config.sta.password,
            WIFI_PASS,
            sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished, waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi successfully.");
        return true;
    }

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi.");
        return false;
    }

    ESP_LOGE(TAG, "Wi-Fi connection timeout.");
    return false;
}

/* =========================================================
 * TCP server task
 * ========================================================= */
static void tcp_server_task(void *arg)
{
    ESP_LOGI(TAG, "Starting TCP server task...");

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket, errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed, errno=%d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 1) < 0) {
        ESP_LOGE(TAG, "Socket listen failed, errno=%d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP server listening on port %d", TCP_PORT);
    ESP_LOGI(TAG, "Waiting for Android app connection...");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(
            listen_sock,
            (struct sockaddr *)&client_addr,
            &addr_len
        );

        if (client_sock < 0) {
            ESP_LOGE(TAG, "accept failed, errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        ESP_LOGI(TAG, "Client connected: %s",
                 inet_ntoa(client_addr.sin_addr));

        vTaskDelay(pdMS_TO_TICKS(500));

        struct timeval recv_timeout;
        recv_timeout.tv_sec = 0;
        recv_timeout.tv_usec = 100000;

        setsockopt(client_sock,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &recv_timeout,
                   sizeof(recv_timeout));

        uint8_t rx_buf[256];

        send_guess_json(client_sock);

        TickType_t last_send_tick = xTaskGetTickCount();

        while (1) {
            int rx_len = recv(client_sock, rx_buf, sizeof(rx_buf), 0);

            if (rx_len > 0) {
                handle_app_message(client_sock, rx_buf, rx_len);
            }
            else if (rx_len == 0) {
                ESP_LOGW(TAG, "Client closed connection.");
                break;
            }
            else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGE(TAG, "recv failed, errno=%d", errno);
                    break;
                }
            }

            TickType_t now = xTaskGetTickCount();

            if ((now - last_send_tick) >= pdMS_TO_TICKS(JSON_SEND_INTERVAL_MS)) {
                last_send_tick = now;

                bool ok = send_guess_json(client_sock);
                if (!ok) {
                    ESP_LOGW(TAG, "Client disconnected or GUESS send failed.");
                    break;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        shutdown(client_sock, 0);
        close(client_sock);

        ESP_LOGI(TAG, "Client socket closed. Waiting for new connection...");
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

/* =========================================================
 * app_main
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RUNNING NRF + WIFI TCP + MASTERMIND GAME");
    ESP_LOGI(TAG, "Wi-Fi SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "TCP port: %d", TCP_PORT);
    ESP_LOGI(TAG, "Rows: %d", MAX_ROWS);
    ESP_LOGI(TAG, "Secret code fixed to RED RED GREEN GREEN");
    ESP_LOGI(TAG, "GUESS = real classified sensor colors");
    ESP_LOGI(TAG, "SUBMIT_GUESS = score + LED feedback + stepper move");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    g_data_mutex = xSemaphoreCreateMutex();
    g_nrf_mutex  = xSemaphoreCreateMutex();

    if (g_data_mutex == NULL || g_nrf_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex.");
        return;
    }

    motor_init();
    motor_off();

    BaseType_t nrf_task_ok = xTaskCreate(
        nrf_receive_task,
        "nrf_receive_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (nrf_task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NRF receive task.");
        return;
    }

    bool wifi_ok = wifi_init_sta();

    if (!wifi_ok) {
        ESP_LOGE(TAG, "Wi-Fi not connected, TCP server not started.");
        ESP_LOGE(TAG, "Please check SSID/password, 2.4GHz hotspot, and Wi-Fi security mode.");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    start_new_game();

    BaseType_t tcp_task_ok = xTaskCreate(
        tcp_server_task,
        "tcp_server_task",
        8192,
        NULL,
        5,
        NULL
    );

    if (tcp_task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task.");
        return;
    }

    ESP_LOGI(TAG, "System started successfully.");
}