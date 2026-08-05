/**
 * @file    bsp_gt1151.c
 * @brief   GT1151 电容触摸屏驱动实现（GPIO 软件模拟 I2C）。
 *
 * 设计要点：
 *   - 使用 GPIO 位翻转（bit-banging）模拟 I2C，因此不依赖硬件 I2C 外设，
 *     便于在共用电阻触摸排线（SCK/MOSI 复用为 SCL/SDA）的开发板上使用。
 *   - GT1151 采用 16bit 寄存器地址，读写时序遵循标准 I2C：
 *       写：START -> 从机写地址 -> 寄存器高字节 -> 寄存器低字节 -> data... -> STOP
 *       读：START -> 从机写地址 -> 寄存器高字节 -> 寄存器低字节
 *            -> repeated START -> 从机读地址 -> data... -> STOP
 *   - 所有引脚、地址、寄存器均在 bsp_gt1151.h 中以宏定义，移植只改头文件。
 */

#include "bsp_gt1151.h"

#include <string.h>

/* ======================= 底层 GPIO 抽象宏 ======================= *
 * 将“操作某根线”封装成宏，主体逻辑读起来更接近时序图，便于维护。
 * -------------------------------------------------------------- */

/* SCL 时钟线：始终是推挽输出 */
#define GT1151_SCL_HIGH()   HAL_GPIO_WritePin(GT1151_SCL_PORT, GT1151_SCL_PIN, GPIO_PIN_SET)
#define GT1151_SCL_LOW()    HAL_GPIO_WritePin(GT1151_SCL_PORT, GT1151_SCL_PIN, GPIO_PIN_RESET)

/* SDA 数据线：需要在“输出”与“输入”之间切换（读 ACK / 读数据时切为输入） */
#define GT1151_SDA_HIGH()   HAL_GPIO_WritePin(GT1151_SDA_PORT, GT1151_SDA_PIN, GPIO_PIN_SET)
#define GT1151_SDA_LOW()    HAL_GPIO_WritePin(GT1151_SDA_PORT, GT1151_SDA_PIN, GPIO_PIN_RESET)
#define GT1151_SDA_READ()   HAL_GPIO_ReadPin(GT1151_SDA_PORT, GT1151_SDA_PIN)

/* INT / RST 控制 */
#define GT1151_RST_HIGH()   HAL_GPIO_WritePin(GT1151_RST_PORT, GT1151_RST_PIN, GPIO_PIN_SET)
#define GT1151_RST_LOW()    HAL_GPIO_WritePin(GT1151_RST_PORT, GT1151_RST_PIN, GPIO_PIN_RESET)
#define GT1151_INT_HIGH()   HAL_GPIO_WritePin(GT1151_INT_PORT, GT1151_INT_PIN, GPIO_PIN_SET)
#define GT1151_INT_LOW()    HAL_GPIO_WritePin(GT1151_INT_PORT, GT1151_INT_PIN, GPIO_PIN_RESET)
#define GT1151_INT_READ()   HAL_GPIO_ReadPin(GT1151_INT_PORT, GT1151_INT_PIN)

/* GT1151 期望的产品 ID 字符串 */
static const char GT1151_EXPECTED_ID[] = "1151";

/* ======================= 微秒级软件延时 ======================= *
 * 软件 I2C 需要在每个时钟沿之间插入短暂延时以满足建立/保持时间。
 * 这里用简单的 NOP 循环，粗略保证 SCL 频率不超过 GT1151 上限（400kHz）。
 * 若你的主频与此不同，可微调 GT1151_DELAY_LOOP。
 * ------------------------------------------------------------ */
#define GT1151_DELAY_LOOP   30U

static void GT1151_Delay(void)
{
    volatile uint32_t i = GT1151_DELAY_LOOP;
    while (i-- > 0U)
    {
        __NOP();
    }
}

/* ======================= SDA 方向切换 ======================= */

/**
 * @brief 将 SDA 配置为开漏输出（主机驱动数据线）。
 * @note  I2C 为线与逻辑，标准做法是开漏 + 外部上拉。
 */
static void GT1151_SDA_OutMode(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GT1151_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏输出 */
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GT1151_SDA_PORT, &gpio);
}

/**
 * @brief 将 SDA 配置为输入（读取从机 ACK 或数据）。
 */
static void GT1151_SDA_InMode(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GT1151_SDA_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GT1151_SDA_PORT, &gpio);
}

/* ======================= I2C 基本时序 ======================= */

/** @brief 产生 START 条件：SCL 高电平期间，SDA 由高变低。 */
static void GT1151_I2C_Start(void)
{
    GT1151_SDA_OutMode();
    GT1151_SDA_HIGH();
    GT1151_SCL_HIGH();
    GT1151_Delay();
    GT1151_SDA_LOW();   /* SCL 高时 SDA 下降沿 = START */
    GT1151_Delay();
    GT1151_SCL_LOW();   /* 钳住时钟，准备发送数据 */
    GT1151_Delay();
}

/** @brief 产生 STOP 条件：SCL 高电平期间，SDA 由低变高。 */
static void GT1151_I2C_Stop(void)
{
    GT1151_SDA_OutMode();
    GT1151_SCL_LOW();
    GT1151_SDA_LOW();
    GT1151_Delay();
    GT1151_SCL_HIGH();
    GT1151_Delay();
    GT1151_SDA_HIGH();  /* SCL 高时 SDA 上升沿 = STOP */
    GT1151_Delay();
}

/**
 * @brief  发送一个字节并读取从机应答位。
 * @param  byte 待发送字节。
 * @return true 收到 ACK；false 收到 NACK（无应答）。
 */
static bool GT1151_I2C_WriteByte(uint8_t byte)
{
    uint8_t i;
    bool ack;

    GT1151_SDA_OutMode();

    /* 高位在前，逐位发送 8 个数据位 */
    for (i = 0; i < 8U; i++)
    {
        GT1151_SCL_LOW();
        GT1151_Delay();
        if (byte & 0x80U)
        {
            GT1151_SDA_HIGH();
        }
        else
        {
            GT1151_SDA_LOW();
        }
        byte <<= 1;
        GT1151_Delay();
        GT1151_SCL_HIGH();  /* 拉高时钟，从机采样 */
        GT1151_Delay();
    }

    /* 第 9 个时钟：释放 SDA，读取从机 ACK */
    GT1151_SCL_LOW();
    GT1151_SDA_InMode();
    GT1151_Delay();
    GT1151_SCL_HIGH();
    GT1151_Delay();
    ack = (GT1151_SDA_READ() == GPIO_PIN_RESET);  /* 低电平表示 ACK */
    GT1151_SCL_LOW();
    GT1151_Delay();

    return ack;
}

/**
 * @brief  读取一个字节。
 * @param  ack true -> 读完发送 ACK（还要继续读）；false -> 发送 NACK（最后一个字节）。
 * @return 读到的字节。
 */
static uint8_t GT1151_I2C_ReadByte(bool ack)
{
    uint8_t i;
    uint8_t byte = 0;

    GT1151_SDA_InMode();

    for (i = 0; i < 8U; i++)
    {
        GT1151_SCL_LOW();
        GT1151_Delay();
        GT1151_SCL_HIGH();  /* 时钟拉高，采样 SDA */
        GT1151_Delay();
        byte <<= 1;
        if (GT1151_SDA_READ() == GPIO_PIN_SET)
        {
            byte |= 0x01U;
        }
    }

    /* 主机产生第 9 个时钟，发送 ACK / NACK 给从机 */
    GT1151_SCL_LOW();
    GT1151_SDA_OutMode();
    if (ack)
    {
        GT1151_SDA_LOW();   /* ACK */
    }
    else
    {
        GT1151_SDA_HIGH();  /* NACK */
    }
    GT1151_Delay();
    GT1151_SCL_HIGH();
    GT1151_Delay();
    GT1151_SCL_LOW();
    GT1151_Delay();

    return byte;
}

/* ======================= GT1151 寄存器读写 ======================= */

/**
 * @brief  向 GT1151 指定寄存器写入若干字节。
 * @param  reg  16bit 寄存器地址。
 * @param  buf  数据缓冲区（可为 NULL 且 len 为 0，仅用于设置读地址）。
 * @param  len  写入长度。
 * @return GT1151_OK 成功；GT1151_ERROR 从机无应答。
 */
static GT1151_Status GT1151_WriteReg(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    GT1151_I2C_Start();

    /* 从机地址（写） + 16bit 寄存器地址（高字节在前） */
    if (!GT1151_I2C_WriteByte(GT1151_ADDR_WRITE) ||
        !GT1151_I2C_WriteByte((uint8_t)(reg >> 8)) ||
        !GT1151_I2C_WriteByte((uint8_t)(reg & 0xFFU)))
    {
        GT1151_I2C_Stop();
        return GT1151_ERROR;
    }

    for (i = 0; i < len; i++)
    {
        if (!GT1151_I2C_WriteByte(buf[i]))
        {
            GT1151_I2C_Stop();
            return GT1151_ERROR;
        }
    }

    GT1151_I2C_Stop();
    return GT1151_OK;
}

/**
 * @brief  从 GT1151 指定寄存器读取若干字节。
 * @param  reg  16bit 寄存器地址。
 * @param  buf  输出缓冲区。
 * @param  len  读取长度。
 * @return GT1151_OK 成功；GT1151_ERROR 从机无应答。
 */
static GT1151_Status GT1151_ReadReg(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    /* 第一阶段：写寄存器地址（不发数据） */
    GT1151_I2C_Start();
    if (!GT1151_I2C_WriteByte(GT1151_ADDR_WRITE) ||
        !GT1151_I2C_WriteByte((uint8_t)(reg >> 8)) ||
        !GT1151_I2C_WriteByte((uint8_t)(reg & 0xFFU)))
    {
        GT1151_I2C_Stop();
        return GT1151_ERROR;
    }

    /* 第二阶段：重复起始，切换到读方向，连续读数据 */
    GT1151_I2C_Start();
    if (!GT1151_I2C_WriteByte(GT1151_ADDR_READ))
    {
        GT1151_I2C_Stop();
        return GT1151_ERROR;
    }

    for (i = 0; i < len; i++)
    {
        /* 最后一个字节回 NACK，其余回 ACK */
        buf[i] = GT1151_I2C_ReadByte(i < (uint16_t)(len - 1U));
    }

    GT1151_I2C_Stop();
    return GT1151_OK;
}

/* ======================= 硬件复位时序 ======================= *
 * GT1151 上电复位时，通过 INT 引脚电平选择 I2C 从机地址：
 *   1. 拉低 RST，同时把 INT 拉低 -> 选择 0x14 地址；
 *   2. 保持 >10ms 后释放 RST；
 *   3. 再保持 INT 状态 >50ms，然后把 INT 切换为输入（作为数据就绪中断）。
 * ---------------------------------------------------------- */
static void GT1151_Reset(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* RST、INT 先配置为推挽输出 */
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = GT1151_RST_PIN;
    HAL_GPIO_Init(GT1151_RST_PORT, &gpio);
    gpio.Pin = GT1151_INT_PIN;
    HAL_GPIO_Init(GT1151_INT_PORT, &gpio);

    /* 进入复位：RST=0，INT=0（选择 0x14 从机地址） */
    GT1151_RST_LOW();
    GT1151_INT_LOW();
    HAL_Delay(20);

    /* 释放复位：RST=1，INT 仍保持一段时间 */
    GT1151_RST_HIGH();
    HAL_Delay(10);

    /* 结束地址选择，把 INT 切换为输入（数据就绪信号） */
    gpio.Pin  = GT1151_INT_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GT1151_INT_PORT, &gpio);
    HAL_Delay(50);
}

/* ======================= 对外 API 实现 ======================= */

GT1151_Status BSP_GT1151_ReadID(char id[5])
{
    uint8_t raw[4] = {0};

    if (GT1151_ReadReg(GT1151_REG_PID, raw, sizeof(raw)) != GT1151_OK)
    {
        return GT1151_ERROR;
    }

    /* 产品 ID 是 4 字节 ASCII，如 '1','1','5','1' */
    id[0] = (char)raw[0];
    id[1] = (char)raw[1];
    id[2] = (char)raw[2];
    id[3] = (char)raw[3];
    id[4] = '\0';

    return GT1151_OK;
}

GT1151_Status BSP_GT1151_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    char id[5] = {0};

    /* 1. 使能相关 GPIO 时钟 */
    GT1151_GPIO_CLK_ENABLE();

    /* 2. SCL 配置为开漏输出（I2C 时钟） */
    gpio.Pin   = GT1151_SCL_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GT1151_SCL_PORT, &gpio);

    /* SDA 默认配置为开漏输出，具体读写时会动态切换方向 */
    GT1151_SDA_OutMode();
    GT1151_SCL_HIGH();
    GT1151_SDA_HIGH();

    /* 3. 执行硬件复位并完成 I2C 地址选择 */
    GT1151_Reset();

    /* 4. 读取并校验产品 ID，判断触摸芯片型号 */
    if (BSP_GT1151_ReadID(id) != GT1151_OK)
    {
        return GT1151_ERROR;
    }

    if (strncmp(id, GT1151_EXPECTED_ID, 4) != 0)
    {
        /* 通信正常但型号不符（可能是 GT9147/GT917S 等其它 Goodix 芯片） */
        return GT1151_ID_MISMATCH;
    }

    return GT1151_OK;
}

bool BSP_GT1151_IsPressed(void)
{
    /* INT 引脚在有触摸数据就绪时会产生脉冲；这里作为简单轮询指示。 */
    return (GT1151_INT_READ() == GPIO_PIN_SET);
}

uint8_t BSP_GT1151_Scan(GT1151_TouchData *data)
{
    uint8_t status = 0;
    uint8_t touch_num;
    uint8_t clear = 0;   /* 用于清零状态寄存器，通知 GT1151 数据已读取 */
    uint8_t i;

    if (data == NULL)
    {
        return 0;
    }

    data->count = 0;

    /* 1. 读取状态寄存器 0x814E */
    if (GT1151_ReadReg(GT1151_REG_STATUS, &status, 1) != GT1151_OK)
    {
        return 0;
    }

    /* bit7 = buffer status（1 表示坐标就绪），bit0~3 = 触摸点个数 */
    if ((status & 0x80U) == 0U)
    {
        return 0;   /* 数据未就绪 */
    }

    touch_num = status & 0x0FU;
    if (touch_num > GT1151_MAX_TOUCH)
    {
        touch_num = GT1151_MAX_TOUCH;
    }

    /* 2. 逐点读取触摸坐标数据 */
    for (i = 0; i < touch_num; i++)
    {
        uint8_t buf[GT1151_POINT_SIZE] = {0};
        uint16_t reg = GT1151_REG_POINT1 + (uint16_t)(i * GT1151_POINT_SIZE);

        if (GT1151_ReadReg(reg, buf, GT1151_POINT_SIZE) != GT1151_OK)
        {
            break;
        }

        /* 单点数据格式：
         *   buf[0]      = track id
         *   buf[1..2]   = X 坐标（小端）
         *   buf[3..4]   = Y 坐标（小端）
         *   buf[5..6]   = 触摸面积
         *   buf[7]      = 保留
         */
        data->points[i].track_id = buf[0];
        data->points[i].x        = (uint16_t)(buf[1] | ((uint16_t)buf[2] << 8));
        data->points[i].y        = (uint16_t)(buf[3] | ((uint16_t)buf[4] << 8));
        data->points[i].size     = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
        data->count++;
    }

    /* 3. 必须把状态寄存器写 0，否则 GT1151 不会更新下一帧坐标 */
    (void)GT1151_WriteReg(GT1151_REG_STATUS, &clear, 1);

    return data->count;
}
