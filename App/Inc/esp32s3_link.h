/**
 * @file esp32s3_link.h
 * @brief Framed UART protocol used to hand cloud properties to an ESP32-S3.
 */

#ifndef ESP32S3_LINK_H
#define ESP32S3_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP32S3_PROPERTY_LIGHT_SWITCH = 1,
    ESP32S3_PROPERTY_VOLUME = 2
} ESP32S3_PropertyId;

typedef enum {
    ESP32S3_LINK_OK = 0,
    ESP32S3_LINK_UART_ERROR,
    ESP32S3_LINK_ACK_TIMEOUT,
    ESP32S3_LINK_PROTOCOL_ERROR,
    ESP32S3_LINK_REMOTE_REJECTED
} ESP32S3_LinkStatus;

/* WiFi encryption types */
typedef enum {
    ESP32S3_WIFI_AUTH_OPEN = 0,
    ESP32S3_WIFI_AUTH_WEP,
    ESP32S3_WIFI_AUTH_WPA_PSK,
    ESP32S3_WIFI_AUTH_WPA2_PSK,
    ESP32S3_WIFI_AUTH_WPA_WPA2_PSK,
    ESP32S3_WIFI_AUTH_WPA3_PSK,
    ESP32S3_WIFI_AUTH_UNKNOWN
} ESP32S3_WiFiAuthMode;

/* WiFi connection status */
typedef enum {
    ESP32S3_WIFI_STATUS_IDLE = 0,
    ESP32S3_WIFI_STATUS_CONNECTING,
    ESP32S3_WIFI_STATUS_CONNECTED,
    ESP32S3_WIFI_STATUS_DISCONNECTED,
    ESP32S3_WIFI_STATUS_FAILED
} ESP32S3_WiFiStatus;

/* WiFi AP info */
typedef struct {
    char ssid[33];                      /* SSID (max 32 chars + null) */
    int8_t rssi;                        /* Signal strength */
    ESP32S3_WiFiAuthMode auth_mode;     /* Encryption type */
} ESP32S3_WiFiApInfo;

/**
 * Send one property to the ESP32-S3 and wait until its local MQTT queue accepts
 * the message. The ESP32-S3 performs Wi-Fi/MQTT reconnect and cloud publishing.
 */
ESP32S3_LinkStatus ESP32S3_Link_SendProperty(uint16_t sequence,
                                             ESP32S3_PropertyId property,
                                             int32_t value,
                                             uint32_t operation_time_ms);

/**
 * Request WiFi scan from ESP32-S3.
 * ESP32-S3 will perform scan and send back WiFi list via notification.
 */
ESP32S3_LinkStatus ESP32S3_Link_RequestWiFiScan(uint16_t sequence);

/**
 * Request WiFi connection to specified SSID with password.
 */
ESP32S3_LinkStatus ESP32S3_Link_ConnectWiFi(uint16_t sequence,
                                            const char *ssid,
                                            const char *password);

/**
 * Enable or disable WiFi on ESP32-S3.
 */
ESP32S3_LinkStatus ESP32S3_Link_SetWiFiEnabled(uint16_t sequence, uint8_t enabled);

/**
 * Get current WiFi status from ESP32-S3.
 */
ESP32S3_LinkStatus ESP32S3_Link_GetWiFiStatus(uint16_t sequence);

/**
 * Check for WiFi notification (scan results or status update).
 * Non-blocking. Returns OK with data, or NO_DATA if nothing available.
 * 
 * @param ap_list Buffer to receive WiFi AP list
 * @param max_count Maximum number of APs to receive
 * @param actual_count Output: actual number of APs received
 * @param wifi_status Output: current WiFi connection status
 */
ESP32S3_LinkStatus ESP32S3_Link_CheckWiFiNotification(ESP32S3_WiFiApInfo *ap_list,
                                                       uint8_t max_count,
                                                       uint8_t *actual_count,
                                                       ESP32S3_WiFiStatus *wifi_status);

#ifdef __cplusplus
}
#endif

#endif /* ESP32S3_LINK_H */
