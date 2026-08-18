/**
 * @file app_config.h
 * @brief User-editable Wi-Fi, Aliyun IoT and STM32-link parameters.
 */

#pragma once

/* Wi-Fi station credentials. */
#define APP_WIFI_SSID                         "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD                     "YOUR_WIFI_PASSWORD"

/* Aliyun IoT Platform device triple and region. */
#define ALIYUN_PRODUCT_KEY                    "YOUR_PRODUCT_KEY"
#define ALIYUN_DEVICE_NAME                    "YOUR_DEVICE_NAME"
#define ALIYUN_DEVICE_SECRET                  "YOUR_DEVICE_SECRET"
#define ALIYUN_REGION_ID                      "cn-shanghai"

/* MQTT connection parameters. TLS (mqtts/8883) is recommended. */
#define ALIYUN_MQTT_USE_TLS                   1
#define ALIYUN_MQTT_KEEPALIVE_SECONDS         60
#define ALIYUN_MQTT_RECONNECT_TIMEOUT_MS      3000
#define ALIYUN_MQTT_PUBLISH_RETRY_MS          500
#define ALIYUN_MQTT_QOS                       1
#define ALIYUN_MQTT_CLIENT_ID                 ALIYUN_DEVICE_NAME
/* This value participates in Aliyun HMAC signing; change it if required by
 * your device-provisioning policy. The same string is used in clientId. */
#define ALIYUN_MQTT_TIMESTAMP                 "2524608000000"

/* Optional MQTT interface overrides. Leave empty to use Aliyun's standard
 * endpoint and property-post topic derived from the device triple/region. */
#define ALIYUN_MQTT_BROKER_URI_OVERRIDE       ""
#define ALIYUN_MQTT_PROPERTY_TOPIC_OVERRIDE   ""

/* Aliyun Thing Model property identifiers. Create matching properties in the
 * product model, or change these macros to your existing identifiers. */
#define ALIYUN_PROPERTY_LIGHT_IDENTIFIER      "LightSwitch"
#define ALIYUN_PROPERTY_VOLUME_IDENTIFIER     "Volume"

/* ESP32-S3 UART connected to STM32 USART2:
 *   STM32 PA2 (USART2_TX) -> ESP32-S3 RX GPIO
 *   STM32 PA3 (USART2_RX) <- ESP32-S3 TX GPIO
 *   GND                    <-> GND
 * Both sides are 3.3 V TTL. Do not connect RS-232 voltage levels. */
#define STM32_LINK_UART_PORT                  UART_NUM_1
#define STM32_LINK_UART_BAUDRATE              115200
#define STM32_LINK_UART_TX_GPIO               17
#define STM32_LINK_UART_RX_GPIO               18
#define STM32_LINK_UART_RX_BUFFER_SIZE         1024

#define MQTT_PROPERTY_QUEUE_DEPTH             16
