/**
 * @file device_cloud.c
 * @brief Asynchronous GUI-operation queue and ESP32-S3 reporting worker.
 */

#include "device_cloud.h"

#include <string.h>

#include "cmsis_os.h"
#include "device_cloud_config.h"
#include "main.h"

typedef struct {
    ESP32S3_PropertyId property;
    int32_t value;
    uint32_t operation_time_ms;
} DeviceCloudEvent;

static osMessageQueueId_t event_queue;
static DeviceCloudStats cloud_stats;

/**
 * @brief 将毫秒转换为 RTOS 时钟节拍数。
 *
 * @param milliseconds 毫秒数。
 * @return 对应的 RTOS 时钟节拍数，至少返回 1。
 */
static uint32_t milliseconds_to_ticks(uint32_t milliseconds)
{
    /* 获取 RTOS 内核的时钟频率（每秒节拍数） */
    const uint32_t tick_frequency = osKernelGetTickFreq();
    /* 转换为节拍数，向上取整（+999 实现向上取整） */
    const uint64_t ticks = ((uint64_t)milliseconds * tick_frequency + 999U) / 1000U;
    /* 确保至少返回 1 个节拍，避免 0 导致的立即超时 */
    return (ticks > 0U) ? (uint32_t)ticks : 1U;
}

/**
 * @brief 将设备属性操作加入事件队列。
 *
 * @param property 属性 ID（灯光开关或音量）。
 * @param value    属性值。
 * @return true 表示成功加入队列；false 表示队列已满或未初始化，操作被丢弃。
 */
static bool queue_property(ESP32S3_PropertyId property, int32_t value)
{
    /* 构造事件结构体，记录属性、值和 STM32 的操作时间戳 */
    const DeviceCloudEvent event = {
        .property = property,
        .value = value,
        .operation_time_ms = HAL_GetTick()  /* 获取当前系统 tick，用于云端时序分析 */
    };

    /* 尝试将事件放入队列（不阻塞，超时为 0） */
    if ((event_queue != NULL) &&
        (osMessageQueuePut(event_queue, &event, 0U, 0U) == osOK)) {
        cloud_stats.queued_count++;  /* 统计：成功入队计数 */
        return true;
    }

    /* 队列满或未初始化，记录丢弃计数 */
    cloud_stats.dropped_count++;
    return false;
}

void DeviceCloud_Init(void)
{
    /* 队列属性：设置名称便于调试 */
    static const osMessageQueueAttr_t queue_attributes = {
        .name = "cloudEventQueue"
    };

    /* 初始化统计数据结构体 */
    memset(&cloud_stats, 0, sizeof(cloud_stats));
    cloud_stats.last_link_status = ESP32S3_LINK_OK;
    
    /* 创建 FreeRTOS 消息队列，用于存储设备操作事件 */
    event_queue = osMessageQueueNew(DEVICE_CLOUD_EVENT_QUEUE_DEPTH,
                                    sizeof(DeviceCloudEvent),
                                    &queue_attributes);
}

bool DeviceCloud_ReportLight(bool is_on)
{
    /* 将布尔值转换为整数（1=开，0=关），加入属性队列 */
    return queue_property(ESP32S3_PROPERTY_LIGHT_SWITCH, is_on ? 1 : 0);
}

bool DeviceCloud_ReportVolume(uint8_t volume_percent)
{
    /* 限制音量范围在 0-100 之间 */
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    /* 将音量值加入属性队列 */
    return queue_property(ESP32S3_PROPERTY_VOLUME, volume_percent);
}

void DeviceCloud_Task(void *argument)
{
    DeviceCloudEvent event;
    uint16_t sequence = 0U;  /* 每个属性消息的序列号，用于匹配 ACK */
    (void)argument;

    for (;;) {
        /* 检查队列是否已初始化 */
        if (event_queue == NULL) {
            osDelay(milliseconds_to_ticks(1000U));
            continue;
        }

        /* 从队列中取出一个事件，永久阻塞直到有数据 */
        if (osMessageQueueGet(event_queue, &event, NULL, osWaitForever) != osOK) {
            continue;
        }

        /* 递增序列号，跳过 0（从 1 开始编号） */
        sequence++;
        if (sequence == 0U) {
            sequence = 1U;
        }

        /* 尝试发送属性到 ESP32-S3，支持重试机制 */
        ESP32S3_LinkStatus status = ESP32S3_LINK_PROTOCOL_ERROR;
        for (uint32_t attempt = 0U;
             attempt < DEVICE_CLOUD_SEND_RETRY_COUNT;
             ++attempt) {
            /* 通过 UART 协议发送属性帧并等待 ACK */
            status = ESP32S3_Link_SendProperty(sequence, event.property,
                                               event.value,
                                               event.operation_time_ms);
            cloud_stats.last_link_status = status;
            
            /* 如果发送成功，退出重试循环 */
            if (status == ESP32S3_LINK_OK) {
                break;
            }
            
            /* 如果还有重试次数，延迟后再试 */
            if ((attempt + 1U) < DEVICE_CLOUD_SEND_RETRY_COUNT) {
                osDelay(milliseconds_to_ticks(DEVICE_CLOUD_RETRY_DELAY_MS));
            }
        }

        /* 更新统计计数器 */
        if (status == ESP32S3_LINK_OK) {
            cloud_stats.delivered_count++;  /* 成功送达 ESP32-S3 本地队列 */
        } else {
            cloud_stats.failed_count++;     /* 重试耗尽仍失败 */
        }
    }
}

void DeviceCloud_GetStats(DeviceCloudStats *stats)
{
    /* 将当前统计数据复制到调用者提供的缓冲区 */
    if (stats != NULL) {
        *stats = cloud_stats;
    }
}
