#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "main.h"
#include "gpio.h"

/* 1: FreeRTOS task + queue, 0: bare-metal callback */
#define BSP_KEY_USE_RTOS           1

#define KEY_NUM                    4
#define KEY_SCAN_PERIOD_MS         1
#define KEY_DEBOUNCE_TICKS         20
#define KEY_LONG_TICKS             1000
#define KEY_ACTIVE_LEVEL           GPIO_PIN_RESET

/* Change this table to match your board. */
#define KEY_GPIO_TABLE             \
{                                  \
    {GPIOE, GPIO_PIN_0},           \
    {GPIOE, GPIO_PIN_1},           \
    {GPIOE, GPIO_PIN_2},           \
    {GPIOE, GPIO_PIN_3},           \
}

#define KEY_GPIO_CLK_ENABLE()      do { __HAL_RCC_GPIOE_CLK_ENABLE(); } while (0)
#define KEY_GPIO_PULL              GPIO_PULLUP

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} KeyIo_t;

typedef void (*KeyIoInitFunc_t)(const KeyIo_t *io);
typedef GPIO_PinState (*KeyIoReadFunc_t)(const KeyIo_t *io);

typedef enum
{
    KEY_IDLE = 0,
    KEY_DEBOUNCE,
    KEY_PRESSED,
    KEY_LONG_HOLD,
} KeyState_t;

typedef enum
{
    KEY_EVENT_SHORT = 1,
    KEY_EVENT_LONG,
} KeyEvent_t;

typedef struct
{
    KeyState_t state;
    uint16_t cnt;
} KeyCtrl_t;

typedef struct
{
    uint8_t key_id;
    KeyEvent_t event;
} KeyMsg_t;

void BSP_Key_Init(void);
void BSP_Key_Scan(void);
void BSP_Key_RegisterIo(KeyIoInitFunc_t init, KeyIoReadFunc_t read);
uint8_t Key_Read(uint8_t id);
void BSP_Key_EventCallback(KeyMsg_t *msg);

#if BSP_KEY_USE_RTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define KEY_QUEUE_LEN              8
extern QueueHandle_t KeyQueueHandle;
void Task_KeyScan(void *argument);
#endif

#endif
