#ifndef __BSP_GT1151_H
#define __BSP_GT1151_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file    bsp_gt1151.h
 * @brief   GT1151 电容触摸屏驱动（软件 I2C 实现）。
 *
 * @note 关于接口命名的说明：
 *       GT1151 本身是一颗 Goodix 出品的 **I2C** 电容触摸控制器。
 *       但很多开发板（如正点原子探索者/阿波罗系列）把电容触摸与电阻触摸
 *       共用同一根排线，排线丝印沿用了电阻触摸（XPT2046/SPI）的名字：
 *
 *         排线丝印   |  电阻触摸(SPI)含义  |  GT1151(电容/I2C) 复用含义
 *         ---------- | ------------------- | --------------------------
 *         T_SCK      |  SPI 时钟           |  I2C 时钟  SCL
 *         T_MOSI     |  SPI 主出从入       |  I2C 数据  SDA
 *         T_MISO     |  SPI 主入从出       |  未使用（保留）
 *         T_PEN      |  笔中断             |  数据就绪中断 INT
 *         T_CS       |  片选               |  复位       RST
 *
 *       因此本驱动使用 **软件 I2C**（GPIO 模拟）与 GT1151 通信，
 *       只需要 SCL / SDA / INT / RST 四根线，MISO 在电容触摸模式下不使用。
 *
 * @note 使用步骤：
 *       1. 根据你的原理图修改下方“引脚配置”宏；
 *       2. 调用 BSP_GT1151_Init() 完成复位与握手，并读取芯片型号；
 *       3. 周期性调用 BSP_GT1151_Scan() 获取触摸点坐标。
 */

/* ======================= 引脚配置（按原理图修改） ======================= *
 * 默认值对应正点原子 STM32F407 探索者开发板的触摸排线映射，
 * 若你的板子不同，只需修改这一段宏即可，无需改动 .c 文件。
 * ---------------------------------------------------------------------- */

/* I2C 时钟线 SCL —— 排线丝印 T_SCK */
#define GT1151_SCL_PORT           GPIOB
#define GT1151_SCL_PIN            GPIO_PIN_0

/* I2C 数据线 SDA —— 排线丝印 T_MOSI */
#define GT1151_SDA_PORT           GPIOF
#define GT1151_SDA_PIN            GPIO_PIN_11

/* 数据就绪中断 INT —— 排线丝印 T_PEN */
#define GT1151_INT_PORT           GPIOB
#define GT1151_INT_PIN            GPIO_PIN_1

/* 复位 RST —— 排线丝印 T_CS */
#define GT1151_RST_PORT           GPIOC
#define GT1151_RST_PIN            GPIO_PIN_13

/* 使能上面四个引脚所在 GPIO 端口的时钟（按需增删）。 */
#define GT1151_GPIO_CLK_ENABLE()                     \
    do {                                             \
        __HAL_RCC_GPIOB_CLK_ENABLE();                \
        __HAL_RCC_GPIOC_CLK_ENABLE();                \
        __HAL_RCC_GPIOF_CLK_ENABLE();                \
    } while (0)

/* ======================= I2C 从机地址 ======================= *
 * GT1151 的 7bit 地址由复位期间 INT 引脚电平决定：
 *   INT = 0 -> 0x14（8bit 写 0x28 / 读 0x29）  <= 本驱动默认
 *   INT = 1 -> 0x5D（8bit 写 0xBA / 读 0xBB）
 * ---------------------------------------------------------- */
#define GT1151_ADDR_WRITE         0x28U
#define GT1151_ADDR_READ          0x29U

/* ======================= GT1151 寄存器地址（16bit） ======================= */
#define GT1151_REG_CTRL           0x8040U  /* 命令寄存器（软复位等）        */
#define GT1151_REG_CFG            0x8050U  /* 配置参数起始地址              */
#define GT1151_REG_CHECK          0x813CU  /* 配置校验和                    */
#define GT1151_REG_PID            0x8140U  /* 产品 ID：4 字节 ASCII 字符串  */
#define GT1151_REG_STATUS         0x814EU  /* 触摸状态（bit7 就绪, bit0-3 点数）*/
#define GT1151_REG_POINT1         0x814FU  /* 第 1 个触摸点数据起始地址      */

/* 每个触摸点占 8 字节，最多支持 5 点触控 */
#define GT1151_POINT_SIZE         8U
#define GT1151_MAX_TOUCH          5U

/* ======================= 数据类型 ======================= */

/** 驱动返回状态 */
typedef enum
{
    GT1151_OK = 0,        /* 操作成功                       */
    GT1151_ERROR,         /* I2C 通信失败 / 无应答          */
    GT1151_ID_MISMATCH    /* 读到 ID，但与期望型号不一致    */
} GT1151_Status;

/** 单个触摸点 */
typedef struct
{
    uint8_t  track_id;    /* 触摸点跟踪 ID（区分不同手指）  */
    uint16_t x;           /* X 坐标（原始，未做方向变换）   */
    uint16_t y;           /* Y 坐标（原始，未做方向变换）   */
    uint16_t size;        /* 触摸面积/压力                  */
} GT1151_Point;

/** 一次扫描得到的全部触摸点 */
typedef struct
{
    uint8_t      count;                     /* 有效触摸点个数（0~GT1151_MAX_TOUCH） */
    GT1151_Point points[GT1151_MAX_TOUCH];  /* 触摸点数组                            */
} GT1151_TouchData;

/* ======================= 对外 API ======================= */

/**
 * @brief  初始化 GT1151：配置 GPIO、执行硬件复位时序、读取并校验产品 ID。
 * @return GT1151_OK       初始化成功且型号匹配；
 *         GT1151_ID_MISMATCH 通信正常但读到的型号不是 "1151"（仍可继续使用）；
 *         GT1151_ERROR     I2C 通信失败。
 */
GT1151_Status BSP_GT1151_Init(void);

/**
 * @brief  读取芯片产品 ID 字符串（用于检测触摸芯片型号）。
 * @param  id  输出缓冲区，至少 5 字节，返回以 '\0' 结尾的字符串，如 "1151"。
 * @return GT1151_OK 读取成功；GT1151_ERROR 通信失败。
 */
GT1151_Status BSP_GT1151_ReadID(char id[5]);

/**
 * @brief  扫描一次触摸屏，读取当前所有触摸点。
 * @param  data  输出的触摸数据（不能为 NULL）。
 * @return 本次有效触摸点个数（0 表示无触摸或无新数据）。
 */
uint8_t BSP_GT1151_Scan(GT1151_TouchData *data);

/**
 * @brief  通过 INT 引脚快速判断当前是否有手指按下（轮询用）。
 * @return true 有触摸；false 无触摸。
 */
bool BSP_GT1151_IsPressed(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_GT1151_H */
