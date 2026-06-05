idf_component_register(
    SRCS "main.c" "NRF24L01.c" "motor.c" "led_patterns.c" "feedback_sender.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_driver_gpio esp_driver_spi
)