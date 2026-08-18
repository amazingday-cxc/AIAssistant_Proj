/**
 * @file esp32s3_link.c
 * @brief STM32 side of the custom ESP32-S3 cloud-property UART protocol.
 *
 * Frame (little-endian multi-byte fields):
 *   A5 5A | version | type | sequence:u16 | payload_len:u16 | payload | crc:u16
 * CRC is CRC-16/CCITT-FALSE over version through the end of payload.
 *
 * Property payload:
 *   property_id:u8 | value:i32 | STM32 operation tick:u32
 * ACK payload:
 *   status:u8 (0 means accepted by the ESP32-S3 local MQTT queue)
 */

#include "esp32s3_link.h"

#include <stdbool.h>
#include <stddef.h>

#include "device_cloud_config.h"
#include "usart.h"

#define LINK_SOF_0                 0xA5U
#define LINK_SOF_1                 0x5AU
#define LINK_PROTOCOL_VERSION      0x01U
#define LINK_FRAME_PROPERTY        0x01U
#define LINK_FRAME_ACK             0x81U
#define LINK_FRAME_WIFI_CMD        0x02U
#define LINK_FRAME_WIFI_NOTIF      0x82U
#define LINK_PROPERTY_PAYLOAD_SIZE 9U
#define LINK_ACK_PAYLOAD_SIZE      1U
#define LINK_HEADER_SIZE           6U
#define LINK_MAX_RX_PAYLOAD_SIZE   16U
#define LINK_MAX_WIFI_PAYLOAD      256U

/* WiFi command types */
#define WIFI_CMD_SCAN              0x01U
#define WIFI_CMD_CONNECT           0x02U
#define WIFI_CMD_SET_ENABLE        0x03U
#define WIFI_CMD_GET_STATUS        0x04U

/**
 * @brief 更新 CRC-16/CCITT-FALSE 校验值。
 *
 * @param crc  当前 CRC 值；开始计算一段新数据时通常传入 0xFFFF。
 * @param data 待计算的数据缓冲区。
 * @param size 待计算的数据长度，单位为字节。
 * @return 更新后的 16 位 CRC 值。
 */
static uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t size)
{
    /* 逐字节更新 CRC 值 */
    for (size_t index = 0U; index < size; ++index) {
        /* 将当前字节异或到 CRC 的高 8 位 */
        crc ^= (uint16_t)data[index] << 8U;
        
        /* 处理 8 位数据 */
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            /* 如果最高位为 1，左移并异或多项式 0x1021；否则仅左移 */
            crc = ((crc & 0x8000U) != 0U)
                      ? (uint16_t)((crc << 1U) ^ 0x1021U)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

/**
 * @brief 将 16 位整数按小端序写入字节缓冲区。
 *
 * @param destination 目标缓冲区，至少需要 2 个字节空间。
 * @param value       待写入的 16 位整数。
 */
static void put_u16_le(uint8_t *destination, uint16_t value)
{
    /* 按小端序写入：低字节在前，高字节在后 */
    destination[0] = (uint8_t)(value & 0xFFU);        /* 低 8 位 */
    destination[1] = (uint8_t)(value >> 8U);          /* 高 8 位 */
}

/**
 * @brief 将 32 位整数按小端序写入字节缓冲区。
 *
 * @param destination 目标缓冲区，至少需要 4 个字节空间。
 * @param value       待写入的 32 位整数。
 */
static void put_u32_le(uint8_t *destination, uint32_t value)
{
    /* 按小端序写入 32 位整数：从最低字节到最高字节 */
    destination[0] = (uint8_t)(value & 0xFFU);           /* 字节 0（最低位） */
    destination[1] = (uint8_t)((value >> 8U) & 0xFFU);   /* 字节 1 */
    destination[2] = (uint8_t)((value >> 16U) & 0xFFU);  /* 字节 2 */
    destination[3] = (uint8_t)((value >> 24U) & 0xFFU);  /* 字节 3（最高位） */
}

/**
 * @brief 从字节缓冲区中按小端序读取 16 位整数。
 *
 * @param source 源缓冲区，至少需要包含 2 个字节。
 * @return 从缓冲区解析出的 16 位无符号整数。
 */
static uint16_t get_u16_le(const uint8_t *source)
{
    /* 按小端序读取：低字节在前，高字节在后 */
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

/**
 * @brief 计算一次通信操作剩余的超时时间。
 *
 * @param started_at 通信操作开始时的 HAL tick 值。
 * @param timeout_ms 通信操作允许的总超时时间，单位为毫秒。
 * @return 剩余超时时间，单位为毫秒；已超时时返回 0。
 */
static uint32_t timeout_remaining(uint32_t started_at, uint32_t timeout_ms)
{
    /* 计算已经过的时间 */
    const uint32_t elapsed = HAL_GetTick() - started_at;
    /* 如果未超时，返回剩余时间；否则返回 0 */
    return (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0U;
}

/**
 * @brief 从 ESP32-S3 链路 UART 中精确接收指定数量的字节。
 *
 * 每次接收前都会根据同一个起始时间重新计算剩余时间，确保多个
 * 分段接收步骤共享整个通信事务的总超时时间，而不会重复累加超时。
 *
 * @param data        接收缓冲区。
 * @param size        需要接收的字节数。
 * @param started_at 事务开始时的 HAL tick 值。
 * @param timeout_ms 事务允许的总超时时间，单位为毫秒。
 * @return true 表示已接收完整数据；false 表示接收失败或超时。
 */
static bool receive_exact(uint8_t *data, uint16_t size,
                          uint32_t started_at, uint32_t timeout_ms)
{
    /* 逐字节接收，每次重新计算剩余超时时间，确保整个操作共享总超时 */
    for (uint16_t index = 0U; index < size; ++index) {
        /* 计算相对于事务开始时间的剩余超时 */
        const uint32_t remaining = timeout_remaining(started_at, timeout_ms);
        
        /* 如果已超时或接收失败，返回 false */
        if ((remaining == 0U) ||
            (HAL_UART_Receive(&huart2, &data[index], 1U, remaining) != HAL_OK)) {
            return false;
        }
    }
    /* 成功接收所有字节 */
    return true;
}

/**
 * @brief 等待并校验 ESP32-S3 返回的 ACK 帧。
 *
 * 函数会搜索帧头、接收 ACK 头部和负载，检查协议版本、帧类型、
 * 序列号、负载长度及 CRC，并将远端状态码转换为本地链路状态。
 *
 * @param expected_sequence 期望在 ACK 中收到的请求序列号。
 * @return ACK 处理结果，包括成功、超时、协议错误或远端拒绝。
 */
static ESP32S3_LinkStatus wait_for_ack(uint16_t expected_sequence)
{
    uint8_t byte = 0U;
    uint8_t sof_state = 0U;           /* 帧头搜索状态：0=等待0xA5, 1=等待0x5A */
    uint8_t header[LINK_HEADER_SIZE];
    uint8_t payload[LINK_MAX_RX_PAYLOAD_SIZE];
    uint8_t received_crc[2];
    const uint32_t started_at = HAL_GetTick();  /* 记录等待 ACK 的起始时间 */

    /* 在超时时间内循环搜索 ACK 帧 */
    while (timeout_remaining(started_at, ESP32S3_LINK_ACK_TIMEOUT_MS) > 0U) {
        const uint32_t remaining = timeout_remaining(started_at,
                                                      ESP32S3_LINK_ACK_TIMEOUT_MS);
        /* 接收一个字节，用于搜索帧头 */
        if (HAL_UART_Receive(&huart2, &byte, 1U, remaining) != HAL_OK) {
            return ESP32S3_LINK_ACK_TIMEOUT;
        }

        /* 帧头第一字节：0xA5 */
        if (sof_state == 0U) {
            sof_state = (byte == LINK_SOF_0) ? 1U : 0U;
            continue;
        }
        /* 帧头第二字节：0x5A */
        if (byte != LINK_SOF_1) {
            sof_state = (byte == LINK_SOF_0) ? 1U : 0U;  /* 如果不匹配，检查是否为新帧头起始 */
            continue;
        }

        /* 找到帧头，接收固定长度的头部（6 字节） */
        if (!receive_exact(header, sizeof(header), started_at,
                           ESP32S3_LINK_ACK_TIMEOUT_MS)) {
            return ESP32S3_LINK_ACK_TIMEOUT;
        }

        /* 解析负载长度，检查是否超出最大值 */
        const uint16_t payload_size = get_u16_le(&header[4]);
        if (payload_size > LINK_MAX_RX_PAYLOAD_SIZE) {
            return ESP32S3_LINK_PROTOCOL_ERROR;
        }
        
        /* 接收负载和 CRC */
        if (!receive_exact(payload, payload_size, started_at,
                           ESP32S3_LINK_ACK_TIMEOUT_MS) ||
            !receive_exact(received_crc, sizeof(received_crc), started_at,
                           ESP32S3_LINK_ACK_TIMEOUT_MS)) {
            return ESP32S3_LINK_ACK_TIMEOUT;
        }

        /* 计算 CRC：从头部到负载结束 */
        uint16_t calculated_crc = crc16_ccitt_update(0xFFFFU, header,
                                                     sizeof(header));
        calculated_crc = crc16_ccitt_update(calculated_crc, payload,
                                            payload_size);

        /* 验证帧的各个字段：版本、类型、序列号、负载长度、CRC */
        if ((header[0] != LINK_PROTOCOL_VERSION) ||
            (header[1] != LINK_FRAME_ACK) ||
            (get_u16_le(&header[2]) != expected_sequence) ||
            (payload_size != LINK_ACK_PAYLOAD_SIZE) ||
            (get_u16_le(received_crc) != calculated_crc)) {
            return ESP32S3_LINK_PROTOCOL_ERROR;
        }

        /* ACK 负载中的状态码：0=成功，非0=远端拒绝 */
        return (payload[0] == 0U) ? ESP32S3_LINK_OK
                                  : ESP32S3_LINK_REMOTE_REJECTED;
    }

    /* 超时未收到有效 ACK */
    return ESP32S3_LINK_ACK_TIMEOUT;
}

/**
 * @brief 向 ESP32-S3 发送一个设备属性并等待 ACK。
 *
 * 函数按照链路协议组装属性帧，写入帧头、序列号、属性负载和 CRC，
 * 通过 USART1 发送后，再从 USART2 等待与序列号匹配的 ACK。ACK 成功
 * 仅表示 ESP32-S3 已接受消息到本地 MQTT 队列，不代表云端发布已完成。
 *
 * @param sequence          本次属性消息的序列号。
 * @param property          属性 ID，例如灯光开关或音量。
 * @param value             属性值。
 * @param operation_time_ms STM32 记录的用户操作时间，单位为毫秒 tick。
 * @return 属性发送及 ACK 处理结果。
 */
ESP32S3_LinkStatus ESP32S3_Link_SendProperty(uint16_t sequence,
                                             ESP32S3_PropertyId property,
                                             int32_t value,
                                             uint32_t operation_time_ms)
{
    /* 分配帧缓冲区：帧头(2) + 头部(6) + 负载(9) + CRC(2) = 19 字节 */
    uint8_t frame[2U + LINK_HEADER_SIZE + LINK_PROPERTY_PAYLOAD_SIZE + 2U];
    uint8_t *header = &frame[2];                      /* 头部从第 3 字节开始 */
    uint8_t *payload = &frame[2U + LINK_HEADER_SIZE]; /* 负载从第 9 字节开始 */

    /* 构造帧头 */
    frame[0] = LINK_SOF_0;  /* 0xA5 */
    frame[1] = LINK_SOF_1;  /* 0x5A */
    
    /* 构造头部：版本、类型、序列号、负载长度 */
    header[0] = LINK_PROTOCOL_VERSION;  /* 0x01 */
    header[1] = LINK_FRAME_PROPERTY;    /* 0x01，属性帧 */
    put_u16_le(&header[2], sequence);   /* 序列号（小端） */
    put_u16_le(&header[4], LINK_PROPERTY_PAYLOAD_SIZE);  /* 负载长度 = 9 */

    /* 构造负载：属性ID(1字节) + 属性值(4字节) + 时间戳(4字节) */
    payload[0] = (uint8_t)property;                 /* 属性 ID */
    put_u32_le(&payload[1], (uint32_t)value);       /* 属性值（小端） */
    put_u32_le(&payload[5], operation_time_ms);     /* STM32 时间戳（小端） */

    /* 计算 CRC-16：从头部到负载结束 */
    uint16_t crc = crc16_ccitt_update(0xFFFFU, header, LINK_HEADER_SIZE);
    crc = crc16_ccitt_update(crc, payload, LINK_PROPERTY_PAYLOAD_SIZE);
    put_u16_le(&payload[LINK_PROPERTY_PAYLOAD_SIZE], crc);  /* CRC 附加在负载后 */

    /* 通过 USART1 发送完整帧到 ESP32-S3 */
    if (HAL_UART_Transmit(&huart2, frame, sizeof(frame),
                          ESP32S3_LINK_UART_TX_TIMEOUT_MS) != HAL_OK) {
        return ESP32S3_LINK_UART_ERROR;
    }

    /* 从 USART2 等待 ESP32-S3 返回的 ACK */
    return wait_for_ack(sequence);
}

/**
 * @brief 发送 WiFi 命令到 ESP32-S3 并等待 ACK。
 */
static ESP32S3_LinkStatus send_wifi_command(uint16_t sequence, uint8_t cmd_type,
                                            const uint8_t *cmd_data, uint16_t data_len)
{
    /* 帧缓冲区：帧头(2) + 头部(6) + 命令类型(1) + 数据 + CRC(2) */
    uint8_t frame[2U + LINK_HEADER_SIZE + 1U + LINK_MAX_WIFI_PAYLOAD + 2U];
    uint8_t *header = &frame[2];
    uint8_t *payload = &frame[2U + LINK_HEADER_SIZE];
    const uint16_t payload_len = 1U + data_len;

    /* 构造帧头 */
    frame[0] = LINK_SOF_0;
    frame[1] = LINK_SOF_1;
    
    /* 构造头部 */
    header[0] = LINK_PROTOCOL_VERSION;
    header[1] = LINK_FRAME_WIFI_CMD;
    put_u16_le(&header[2], sequence);
    put_u16_le(&header[4], payload_len);

    /* 构造负载：命令类型 + 数据 */
    payload[0] = cmd_type;
    if (data_len > 0U && cmd_data != NULL) {
        for (uint16_t i = 0U; i < data_len; ++i) {
            payload[1U + i] = cmd_data[i];
        }
    }

    /* 计算 CRC */
    uint16_t crc = crc16_ccitt_update(0xFFFFU, header, LINK_HEADER_SIZE);
    crc = crc16_ccitt_update(crc, payload, payload_len);
    put_u16_le(&payload[payload_len], crc);

    /* 发送帧 */
    const uint16_t frame_len = 2U + LINK_HEADER_SIZE + payload_len + 2U;
    if (HAL_UART_Transmit(&huart2, frame, frame_len,
                          ESP32S3_LINK_UART_TX_TIMEOUT_MS) != HAL_OK) {
        return ESP32S3_LINK_UART_ERROR;
    }

    return wait_for_ack(sequence);
}

ESP32S3_LinkStatus ESP32S3_Link_RequestWiFiScan(uint16_t sequence)
{
    return send_wifi_command(sequence, WIFI_CMD_SCAN, NULL, 0U);
}

ESP32S3_LinkStatus ESP32S3_Link_ConnectWiFi(uint16_t sequence,
                                            const char *ssid,
                                            const char *password)
{
    uint8_t data[128];
    uint16_t pos = 0U;
    
    /* SSID 长度 + SSID */
    uint8_t ssid_len = 0U;
    while (ssid[ssid_len] != '\0' && ssid_len < 32U) {
        ssid_len++;
    }
    data[pos++] = ssid_len;
    for (uint8_t i = 0U; i < ssid_len; ++i) {
        data[pos++] = (uint8_t)ssid[i];
    }
    
    /* 密码长度 + 密码 */
    uint8_t pwd_len = 0U;
    while (password[pwd_len] != '\0' && pwd_len < 63U) {
        pwd_len++;
    }
    data[pos++] = pwd_len;
    for (uint8_t i = 0U; i < pwd_len; ++i) {
        data[pos++] = (uint8_t)password[i];
    }
    
    return send_wifi_command(sequence, WIFI_CMD_CONNECT, data, pos);
}

ESP32S3_LinkStatus ESP32S3_Link_SetWiFiEnabled(uint16_t sequence, uint8_t enabled)
{
    uint8_t data[1];
    data[0] = enabled;
    return send_wifi_command(sequence, WIFI_CMD_SET_ENABLE, data, 1U);
}

ESP32S3_LinkStatus ESP32S3_Link_GetWiFiStatus(uint16_t sequence)
{
    return send_wifi_command(sequence, WIFI_CMD_GET_STATUS, NULL, 0U);
}

ESP32S3_LinkStatus ESP32S3_Link_CheckWiFiNotification(ESP32S3_WiFiApInfo *ap_list,
                                                       uint8_t max_count,
                                                       uint8_t *actual_count,
                                                       ESP32S3_WiFiStatus *wifi_status)
{
    uint8_t byte = 0U;
    uint8_t header[LINK_HEADER_SIZE];
    uint8_t payload[LINK_MAX_WIFI_PAYLOAD];
    uint8_t received_crc[2];
    
    *actual_count = 0U;
    *wifi_status = ESP32S3_WIFI_STATUS_IDLE;
    
    /* 非阻塞检查：尝试接收帧头第一字节 */
    if (HAL_UART_Receive(&huart2, &byte, 1U, 1U) != HAL_OK || byte != LINK_SOF_0) {
        return ESP32S3_LINK_OK;  /* 无数据可读 */
    }
    
    /* 接收帧头第二字节 */
    if (HAL_UART_Receive(&huart2, &byte, 1U, 50U) != HAL_OK || byte != LINK_SOF_1) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    /* 接收头部 */
    for (uint8_t i = 0U; i < LINK_HEADER_SIZE; ++i) {
        if (HAL_UART_Receive(&huart2, &header[i], 1U, 50U) != HAL_OK) {
            return ESP32S3_LINK_PROTOCOL_ERROR;
        }
    }
    
    /* 检查是否为 WiFi 通知帧 */
    if (header[0] != LINK_PROTOCOL_VERSION || header[1] != LINK_FRAME_WIFI_NOTIF) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    /* 获取负载长度 */
    const uint16_t payload_size = get_u16_le(&header[4]);
    if (payload_size > LINK_MAX_WIFI_PAYLOAD) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    /* 接收负载 */
    for (uint16_t i = 0U; i < payload_size; ++i) {
        if (HAL_UART_Receive(&huart2, &payload[i], 1U, 50U) != HAL_OK) {
            return ESP32S3_LINK_PROTOCOL_ERROR;
        }
    }
    
    /* 接收 CRC */
    if (HAL_UART_Receive(&huart2, &received_crc[0], 1U, 50U) != HAL_OK ||
        HAL_UART_Receive(&huart2, &received_crc[1], 1U, 50U) != HAL_OK) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    /* 验证 CRC */
    uint16_t calculated_crc = crc16_ccitt_update(0xFFFFU, header, LINK_HEADER_SIZE);
    calculated_crc = crc16_ccitt_update(calculated_crc, payload, payload_size);
    if (get_u16_le(received_crc) != calculated_crc) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    /* 解析负载：状态(1) + AP数量(1) + AP列表 */
    if (payload_size < 2U) {
        return ESP32S3_LINK_PROTOCOL_ERROR;
    }
    
    *wifi_status = (ESP32S3_WiFiStatus)payload[0];
    const uint8_t ap_count = payload[1];
    uint16_t pos = 2U;
    
    *actual_count = (ap_count < max_count) ? ap_count : max_count;
    
    for (uint8_t i = 0U; i < *actual_count && pos < payload_size; ++i) {
        /* SSID长度 */
        const uint8_t ssid_len = payload[pos++];
        if (pos + ssid_len + 2U > payload_size) break;
        
        /* SSID */
        for (uint8_t j = 0U; j < ssid_len && j < 32U; ++j) {
            ap_list[i].ssid[j] = (char)payload[pos++];
        }
        ap_list[i].ssid[ssid_len < 32U ? ssid_len : 32U] = '\0';
        
        /* RSSI */
        ap_list[i].rssi = (int8_t)payload[pos++];
        
        /* 加密类型 */
        ap_list[i].auth_mode = (ESP32S3_WiFiAuthMode)payload[pos++];
    }
    
    return ESP32S3_LINK_OK;
}
