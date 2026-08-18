# ESP32-S3 阿里云 MQTT 桥接固件

本目录是独立的 **ESP-IDF 5.x** 工程。STM32F407 通过 USART2 将灯光、音量操作发送给 ESP32-S3；ESP32-S3 负责 Wi-Fi、阿里云 MQTT 鉴权、断线重连和物模型属性上报。

## 1. 配置

编辑 `main/app_config.h`：

- `APP_WIFI_SSID` / `APP_WIFI_PASSWORD`
- `ALIYUN_PRODUCT_KEY`
- `ALIYUN_DEVICE_NAME`
- `ALIYUN_DEVICE_SECRET`
- `ALIYUN_REGION_ID`
- MQTT Broker URI、属性上报 Topic（通常保持空字符串自动生成；私有接入点时可覆盖）
- UART TX/RX GPIO（默认 ESP32-S3 GPIO17/GPIO18）
- 阿里云物模型属性标识符（默认 `LightSwitch`、`Volume`）

阿里云产品物模型应包含：

| 属性标识符 | 类型 | 范围 |
| --- | --- | --- |
| `LightSwitch` | bool/int | 0 或 1 |
| `Volume` | int | 0~100 |

若物模型中的标识符不同，只修改配置宏，不需要修改业务代码。

## 2. 接线

| STM32F407 | ESP32-S3 |
| --- | --- |
| PA2 / USART2_TX | RX GPIO（默认 GPIO18） |
| PA3 / USART2_RX | TX GPIO（默认 GPIO17） |
| GND | GND |

通信参数为 115200、8N1、3.3 V TTL。

## 3. 构建与烧录

```bash
cd ESP32S3_Aliyun_MQTT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 4. 协议与可靠性

自定义帧格式：

```text
A5 5A | version | type | sequence(u16 LE) | length(u16 LE) | payload | CRC16
```

- CRC：CRC-16/CCITT-FALSE，覆盖 `version` 到 `payload`。
- STM32 仅在 ESP32-S3 本地 MQTT 队列接收成功后收到 ACK。
- STM32 超时会使用相同序号和完整负载重试；ESP32-S3 会比较完整消息识别相邻重试帧，避免重复入队，也避免 STM32 单独重启后仅因序号复用而误丢首条操作。
- ESP32-S3 串口接收先于网络连接启动，因此 Wi-Fi 尚未连接时也可暂存操作。
- MQTT 断线或发布接口暂时失败时，当前消息由发布任务保留，其余消息继续保存在 ESP32-S3 RAM 队列中，并按 `ALIYUN_MQTT_PUBLISH_RETRY_MS` 配置重试；连接恢复后按顺序继续发送。
- STM32 收到 ACK 表示消息已经被 ESP32-S3 本地队列接收，不代表阿里云已确认；MQTT QoS 1 的 PUBACK 由 ESP-MQTT 协议栈异步处理。
- 若要求掉电不丢数据，可在 ESP32-S3 侧将 `s_property_queue` 替换为 NVS/Flash 持久队列。
