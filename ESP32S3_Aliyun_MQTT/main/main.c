/**
 * @file main.c
 * @brief ESP32-S3 UART-to-Aliyun-MQTT bridge for AIAssistant_Proj.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "driver/uart.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define LINK_SOF_0                 0xA5U
#define LINK_SOF_1                 0x5AU
#define LINK_PROTOCOL_VERSION      0x01U
#define LINK_FRAME_PROPERTY        0x01U
#define LINK_FRAME_ACK             0x81U
#define LINK_PROPERTY_PAYLOAD_SIZE 9U
#define LINK_HEADER_SIZE           6U
#define LINK_MAX_PAYLOAD_SIZE      32U

#define PROPERTY_LIGHT_SWITCH      1U
#define PROPERTY_VOLUME            2U

#define ACK_OK                     0U
#define ACK_INVALID_PROPERTY       1U
#define ACK_QUEUE_FULL             2U

#define WIFI_CONNECTED_BIT         BIT0
#define MQTT_CONNECTED_BIT         BIT1

typedef struct {
    uint16_t sequence;
    uint8_t property;
    int32_t value;
    uint32_t operation_time_ms;
} property_message_t;

static const char *TAG = "aliyun_bridge";
static EventGroupHandle_t s_connection_events;
static QueueHandle_t s_property_queue;
static esp_mqtt_client_handle_t s_mqtt_client;

static char s_broker_uri[160];
static char s_mqtt_username[96];
static char s_mqtt_client_id[192];
static char s_mqtt_password[65];
static char s_property_topic[192];

static bool property_messages_equal(const property_message_t *left,
                                    const property_message_t *right)
{
    return (left->sequence == right->sequence) &&
           (left->property == right->property) &&
           (left->value == right->value) &&
           (left->operation_time_ms == right->operation_time_ms);
}

static uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t size)
{
    for (size_t index = 0U; index < size; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U)
                      ? (uint16_t)((crc << 1U) ^ 0x1021U)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static bool configuration_is_valid(void)
{
    return (strncmp(APP_WIFI_SSID, "YOUR_", 5U) != 0) &&
           (strncmp(ALIYUN_PRODUCT_KEY, "YOUR_", 5U) != 0) &&
           (strncmp(ALIYUN_DEVICE_NAME, "YOUR_", 5U) != 0) &&
           (strncmp(ALIYUN_DEVICE_SECRET, "YOUR_", 5U) != 0);
}

static esp_err_t build_aliyun_credentials(void)
{
    char sign_source[384];
    uint8_t digest[32];
    const int secure_mode = ALIYUN_MQTT_USE_TLS ? 2 : 3;

    if (ALIYUN_MQTT_BROKER_URI_OVERRIDE[0] != '\0') {
        strlcpy(s_broker_uri, ALIYUN_MQTT_BROKER_URI_OVERRIDE,
                sizeof(s_broker_uri));
    } else {
        snprintf(s_broker_uri, sizeof(s_broker_uri),
                 "%s://%s.iot-as-mqtt.%s.aliyuncs.com:%d",
                 ALIYUN_MQTT_USE_TLS ? "mqtts" : "mqtt",
                 ALIYUN_PRODUCT_KEY, ALIYUN_REGION_ID,
                 ALIYUN_MQTT_USE_TLS ? 8883 : 1883);
    }
    snprintf(s_mqtt_username, sizeof(s_mqtt_username), "%s&%s",
             ALIYUN_DEVICE_NAME, ALIYUN_PRODUCT_KEY);
    snprintf(s_mqtt_client_id, sizeof(s_mqtt_client_id),
             "%s|securemode=%d,signmethod=hmacsha256,timestamp=%s|",
             ALIYUN_MQTT_CLIENT_ID, secure_mode, ALIYUN_MQTT_TIMESTAMP);
    snprintf(sign_source, sizeof(sign_source),
             "clientId%sdeviceName%sproductKey%stimestamp%s",
             ALIYUN_MQTT_CLIENT_ID, ALIYUN_DEVICE_NAME,
             ALIYUN_PRODUCT_KEY, ALIYUN_MQTT_TIMESTAMP);
    if (ALIYUN_MQTT_PROPERTY_TOPIC_OVERRIDE[0] != '\0') {
        strlcpy(s_property_topic, ALIYUN_MQTT_PROPERTY_TOPIC_OVERRIDE,
                sizeof(s_property_topic));
    } else {
        snprintf(s_property_topic, sizeof(s_property_topic),
                 "/sys/%s/%s/thing/event/property/post",
                 ALIYUN_PRODUCT_KEY, ALIYUN_DEVICE_NAME);
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if ((md_info == NULL) ||
        (mbedtls_md_hmac(md_info,
                         (const unsigned char *)ALIYUN_DEVICE_SECRET,
                         strlen(ALIYUN_DEVICE_SECRET),
                         (const unsigned char *)sign_source,
                         strlen(sign_source), digest) != 0)) {
        return ESP_FAIL;
    }

    for (size_t index = 0U; index < sizeof(digest); ++index) {
        snprintf(&s_mqtt_password[index * 2U], 3U, "%02x", digest[index]);
    }
    s_mqtt_password[64] = '\0';
    return ESP_OK;
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) &&
               (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(s_connection_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        xEventGroupSetBits(s_connection_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static esp_err_t wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, APP_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    return esp_wifi_start();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(s_connection_events, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "Aliyun MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_connection_events, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "Aliyun MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT transport error");
        break;
    default:
        break;
    }
}

static esp_err_t mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_mqtt_client_id,
        .credentials.username = s_mqtt_username,
        .credentials.authentication.password = s_mqtt_password,
        .session.keepalive = ALIYUN_MQTT_KEEPALIVE_SECONDS,
        .network.reconnect_timeout_ms = ALIYUN_MQTT_RECONNECT_TIMEOUT_MS,
    };
#if ALIYUN_MQTT_USE_TLS
    mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    s_mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (s_mqtt_client == NULL) {
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL));
    return esp_mqtt_client_start(s_mqtt_client);
}

static int uart_read_exact(uint8_t *data, size_t size, TickType_t timeout)
{
    size_t received = 0U;
    const TickType_t started_at = xTaskGetTickCount();

    while (received < size) {
        const TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= timeout) {
            return -1;
        }
        const int count = uart_read_bytes(STM32_LINK_UART_PORT,
                                          &data[received], size - received,
                                          timeout - elapsed);
        if (count <= 0) {
            return -1;
        }
        received += (size_t)count;
    }
    return (int)received;
}

static void uart_send_ack(uint16_t sequence, uint8_t status)
{
    uint8_t frame[2U + LINK_HEADER_SIZE + 1U + 2U];
    uint8_t *header = &frame[2];
    uint8_t *payload = &frame[2U + LINK_HEADER_SIZE];

    frame[0] = LINK_SOF_0;
    frame[1] = LINK_SOF_1;
    header[0] = LINK_PROTOCOL_VERSION;
    header[1] = LINK_FRAME_ACK;
    put_u16_le(&header[2], sequence);
    put_u16_le(&header[4], 1U);
    payload[0] = status;

    uint16_t crc = crc16_ccitt_update(0xFFFFU, header, LINK_HEADER_SIZE);
    crc = crc16_ccitt_update(crc, payload, 1U);
    put_u16_le(&payload[1], crc);
    uart_write_bytes(STM32_LINK_UART_PORT, frame, sizeof(frame));
}

static void stm32_uart_task(void *argument)
{
    uint8_t byte;
    uint8_t sof_state = 0U;
    uint8_t header[LINK_HEADER_SIZE];
    uint8_t payload[LINK_MAX_PAYLOAD_SIZE];
    uint8_t received_crc[2];
    property_message_t last_accepted_message = {0};
    bool have_last_accepted_message = false;
    (void)argument;

    for (;;) {
        if (uart_read_bytes(STM32_LINK_UART_PORT, &byte, 1U, portMAX_DELAY) != 1) {
            continue;
        }
        if (sof_state == 0U) {
            sof_state = (byte == LINK_SOF_0) ? 1U : 0U;
            continue;
        }
        if (byte != LINK_SOF_1) {
            sof_state = (byte == LINK_SOF_0) ? 1U : 0U;
            continue;
        }
        sof_state = 0U;

        if (uart_read_exact(header, sizeof(header), pdMS_TO_TICKS(200)) < 0) {
            continue;
        }
        const uint16_t payload_size = get_u16_le(&header[4]);
        if ((payload_size > LINK_MAX_PAYLOAD_SIZE) ||
            (uart_read_exact(payload, payload_size, pdMS_TO_TICKS(200)) < 0) ||
            (uart_read_exact(received_crc, sizeof(received_crc),
                             pdMS_TO_TICKS(200)) < 0)) {
            continue;
        }

        uint16_t crc = crc16_ccitt_update(0xFFFFU, header, sizeof(header));
        crc = crc16_ccitt_update(crc, payload, payload_size);
        const uint16_t sequence = get_u16_le(&header[2]);
        if ((header[0] != LINK_PROTOCOL_VERSION) ||
            (header[1] != LINK_FRAME_PROPERTY) ||
            (payload_size != LINK_PROPERTY_PAYLOAD_SIZE) ||
            (get_u16_le(received_crc) != crc)) {
            continue;
        }

        property_message_t message = {
            .sequence = sequence,
            .property = payload[0],
            .value = (int32_t)get_u32_le(&payload[1]),
            .operation_time_ms = get_u32_le(&payload[5])
        };
        if (have_last_accepted_message &&
            property_messages_equal(&message, &last_accepted_message)) {
            uart_send_ack(sequence, ACK_OK);
        } else if ((message.property != PROPERTY_LIGHT_SWITCH) &&
            (message.property != PROPERTY_VOLUME)) {
            uart_send_ack(sequence, ACK_INVALID_PROPERTY);
        } else if (xQueueSend(s_property_queue, &message, 0U) != pdPASS) {
            uart_send_ack(sequence, ACK_QUEUE_FULL);
        } else {
            last_accepted_message = message;
            have_last_accepted_message = true;
            uart_send_ack(sequence, ACK_OK);
        }
    }
}

static void mqtt_publish_task(void *argument)
{
    property_message_t message;
    char payload[256];
    bool have_pending_message = false;
    (void)argument;

    for (;;) {
        xEventGroupWaitBits(s_connection_events, MQTT_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        if (!have_pending_message) {
            if (xQueueReceive(s_property_queue, &message, portMAX_DELAY) !=
                pdPASS) {
                continue;
            }
            have_pending_message = true;
        }

        const char *identifier = (message.property == PROPERTY_LIGHT_SWITCH)
                                     ? ALIYUN_PROPERTY_LIGHT_IDENTIFIER
                                     : ALIYUN_PROPERTY_VOLUME_IDENTIFIER;
        snprintf(payload, sizeof(payload),
                 "{\"id\":\"%u\",\"version\":\"1.0\","
                 "\"params\":{\"%s\":%" PRId32 "},"
                 "\"method\":\"thing.event.property.post\"}",
                 message.sequence, identifier, message.value);

        const int message_id = esp_mqtt_client_publish(s_mqtt_client,
                                                       s_property_topic,
                                                       payload, 0,
                                                       ALIYUN_MQTT_QOS, 0);
        if (message_id < 0) {
            /* A negative result can also mean a transient local outbox/resource
             * shortage while the MQTT connection is still alive. Do not clear
             * the connection bit here; the MQTT event callback owns it. */
            ESP_LOGW(TAG, "Publish deferred; retrying current message");
            vTaskDelay(pdMS_TO_TICKS(ALIYUN_MQTT_PUBLISH_RETRY_MS));
        } else {
            ESP_LOGI(TAG, "Published seq=%u property=%s value=%" PRId32,
                     message.sequence, identifier, message.value);
            have_pending_message = false;
        }
    }
}

static esp_err_t stm32_uart_start(void)
{
    const uart_config_t uart_config = {
        .baud_rate = STM32_LINK_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(STM32_LINK_UART_PORT,
                                        STM32_LINK_UART_RX_BUFFER_SIZE,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(STM32_LINK_UART_PORT, &uart_config));
    return uart_set_pin(STM32_LINK_UART_PORT,
                        STM32_LINK_UART_TX_GPIO,
                        STM32_LINK_UART_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void)
{
    esp_err_t status = nvs_flash_init();
    if ((status == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (status == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(status);

    if (!configuration_is_valid()) {
        ESP_LOGE(TAG, "Configure Wi-Fi and Aliyun parameters in main/app_config.h");
        return;
    }
    ESP_ERROR_CHECK(build_aliyun_credentials());

    s_connection_events = xEventGroupCreate();
    s_property_queue = xQueueCreate(MQTT_PROPERTY_QUEUE_DEPTH,
                                    sizeof(property_message_t));
    if ((s_connection_events == NULL) || (s_property_queue == NULL)) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS objects");
        return;
    }

    ESP_ERROR_CHECK(stm32_uart_start());
    if (xTaskCreate(stm32_uart_task, "stm32_uart", 4096, NULL, 12, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create STM32 UART task");
        return;
    }

    /* Wi-Fi and MQTT reconnect asynchronously. Starting the UART task first
     * allows STM32 operations to enter the local queue while the network is
     * unavailable or still associating. */
    ESP_ERROR_CHECK(wifi_start());
    ESP_ERROR_CHECK(mqtt_start());

    if (xTaskCreate(mqtt_publish_task, "mqtt_publish", 4096, NULL, 10, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT publish task");
    }
}
