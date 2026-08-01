#include "bsp_key.h"

static const KeyIo_t key_gpio[KEY_NUM] = KEY_GPIO_TABLE;
static KeyCtrl_t key_ctrl[KEY_NUM];
static KeyIoInitFunc_t key_io_init;
static KeyIoReadFunc_t key_io_read;

#if BSP_KEY_USE_RTOS
QueueHandle_t KeyQueueHandle;
#endif

static void Key_DefaultIoInit(const KeyIo_t *io)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = io->pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = KEY_GPIO_PULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(io->port, &GPIO_InitStruct);
}

static GPIO_PinState Key_DefaultIoRead(const KeyIo_t *io)
{
    return HAL_GPIO_ReadPin(io->port, io->pin);
}

static void Key_SendMsg(KeyMsg_t *msg)
{
#if BSP_KEY_USE_RTOS
    if (KeyQueueHandle != NULL)
    {
        (void)xQueueSend(KeyQueueHandle, msg, 0);
    }
#else
    BSP_Key_EventCallback(msg);
#endif
}

__weak void BSP_Key_EventCallback(KeyMsg_t *msg)
{
    (void)msg;
}

void BSP_Key_RegisterIo(KeyIoInitFunc_t init, KeyIoReadFunc_t read)
{
    key_io_init = init;
    key_io_read = read;
}

void BSP_Key_Init(void)
{
    uint8_t i;

    if (key_io_init == NULL)
    {
        key_io_init = Key_DefaultIoInit;
    }

    if (key_io_read == NULL)
    {
        key_io_read = Key_DefaultIoRead;
    }

    KEY_GPIO_CLK_ENABLE();

    for (i = 0; i < KEY_NUM; i++)
    {
        key_io_init(&key_gpio[i]);

        key_ctrl[i].state = KEY_IDLE;
        key_ctrl[i].cnt = 0;
    }

#if BSP_KEY_USE_RTOS
    if (KeyQueueHandle == NULL)
    {
        KeyQueueHandle = xQueueCreate(KEY_QUEUE_LEN, sizeof(KeyMsg_t));
    }
#endif
}

uint8_t Key_Read(uint8_t id)
{
    if (id >= KEY_NUM)
    {
        return 1;
    }

    if (key_io_read == NULL)
    {
        key_io_read = Key_DefaultIoRead;
    }

    return (key_io_read(&key_gpio[id]) == KEY_ACTIVE_LEVEL) ? 0 : 1;
}

void BSP_Key_Scan(void)
{
    KeyMsg_t msg;
    uint8_t i;
    uint8_t level;

    for (i = 0; i < KEY_NUM; i++)
    {
        level = Key_Read(i);

        switch (key_ctrl[i].state)
        {
            case KEY_IDLE:
                if (level == 0)
                {
                    key_ctrl[i].cnt = 0;
                    key_ctrl[i].state = KEY_DEBOUNCE;
                }
                break;

            case KEY_DEBOUNCE:
                if (level == 0)
                {
                    key_ctrl[i].cnt++;
                    if (key_ctrl[i].cnt >= KEY_DEBOUNCE_TICKS)
                    {
                        key_ctrl[i].cnt = 0;
                        key_ctrl[i].state = KEY_PRESSED;
                    }
                }
                else
                {
                    key_ctrl[i].state = KEY_IDLE;
                }
                break;

            case KEY_PRESSED:
                if (level == 0)
                {
                    key_ctrl[i].cnt++;
                    if (key_ctrl[i].cnt >= KEY_LONG_TICKS)
                    {
                        msg.key_id = i;
                        msg.event = KEY_EVENT_LONG;
                        Key_SendMsg(&msg);
                        key_ctrl[i].state = KEY_LONG_HOLD;
                    }
                }
                else
                {
                    msg.key_id = i;
                    msg.event = KEY_EVENT_SHORT;
                    Key_SendMsg(&msg);
                    key_ctrl[i].state = KEY_IDLE;
                }
                break;

            case KEY_LONG_HOLD:
                if (level != 0)
                {
                    key_ctrl[i].state = KEY_IDLE;
                }
                break;

            default:
                key_ctrl[i].state = KEY_IDLE;
                break;
        }
    }
}

#if BSP_KEY_USE_RTOS
void Task_KeyScan(void *argument)
{
    (void)argument;

    BSP_Key_Init();

    for (;;)
    {
        BSP_Key_Scan();
        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));
    }
}
#endif
