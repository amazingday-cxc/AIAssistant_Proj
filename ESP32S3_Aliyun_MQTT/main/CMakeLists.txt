idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi nvs_flash mqtt mbedtls esp_event esp_netif driver esp-tls
)
