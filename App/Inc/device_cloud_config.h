/**
 * @file device_cloud_config.h
 * @brief Configuration for asynchronous device-state reporting.
 */

#ifndef DEVICE_CLOUD_CONFIG_H
#define DEVICE_CLOUD_CONFIG_H

/* Number of GUI operations that may wait for the ESP32-S3 link. */
#define DEVICE_CLOUD_EVENT_QUEUE_DEPTH        8U

/* UART transaction retry policy. */
#define DEVICE_CLOUD_SEND_RETRY_COUNT         3U
#define DEVICE_CLOUD_RETRY_DELAY_MS           200U

/* STM32 <-> ESP32-S3 UART transaction timeouts. */
#define ESP32S3_LINK_UART_TX_TIMEOUT_MS       100U
#define ESP32S3_LINK_ACK_TIMEOUT_MS           300U

#endif /* DEVICE_CLOUD_CONFIG_H */
