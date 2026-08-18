/**
 * @file device_cloud.h
 * @brief Non-blocking application API for reporting device operations.
 */

#ifndef DEVICE_CLOUD_H
#define DEVICE_CLOUD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp32s3_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t queued_count;
    uint32_t dropped_count;
    uint32_t delivered_count;
    uint32_t failed_count;
    ESP32S3_LinkStatus last_link_status;
} DeviceCloudStats;

/**
 * @brief 初始化设备云服务模块。
 *
 * 在 RTOS 调度器启动前调用，用于创建内部操作队列和初始化统计数据。
 */
void DeviceCloud_Init(void);

/**
 * @brief 将灯光开关操作加入队列，不阻塞 GUI 任务。
 *
 * @param is_on true 表示开灯，false 表示关灯。
 * @return true 表示操作已成功加入队列；false 表示队列已满，操作被丢弃。
 */
bool DeviceCloud_ReportLight(bool is_on);

/**
 * @brief 将音量调节操作加入队列，不阻塞 GUI 任务。
 *
 * @param volume_percent 音量百分比（0-100），超过 100 将自动截断为 100。
 * @return true 表示操作已成功加入队列；false 表示队列已满，操作被丢弃。
 */
bool DeviceCloud_ReportVolume(uint8_t volume_percent);

/**
 * @brief 设备云服务后台任务入口函数。
 *
 * 此函数作为 FreeRTOS/CMSIS-RTOS 任务运行，从操作队列中取出事件，
 * 通过 ESP32-S3 链路发送到云端，支持自动重试。此函数永不返回。
 *
 * @param argument FreeRTOS 任务参数（未使用）。
 */
void DeviceCloud_Task(void *argument);

/**
 * @brief 获取设备云服务的诊断统计信息。
 *
 * 适用于调试器监视窗口查看或运行时状态监控。
 *
 * @param stats 指向 DeviceCloudStats 结构体的指针，用于接收统计数据。
 */
void DeviceCloud_GetStats(DeviceCloudStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_CLOUD_H */
