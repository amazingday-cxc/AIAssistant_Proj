#ifndef _BSP_ST7789_H_
#define _BSP_ST7789_H_

#include "main.h"

//-----------------LCD Configuation----------------
#define USE_HORIZONTAL 0    // 设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏
#define USE_HARDWARE_SPI 1

#define LCD_FILL_BUF_SIZE 1536U

//-----------------LCD Function Ctrl---------------
#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
#define LCD_W 240
#define LCD_H 320

#define EXT_SRAM __attribute__((section(".extsram"), aligned(4)))

#else
#define LCD_W 320
#define LCD_H 240
#endif

//-----------------LCD Port Define-----------------

#define LCD_GPIO_SET(port, pin) ((port)->BSRR = (uint32_t)(pin))
#define LCD_GPIO_CLR(port, pin) ((port)->BSRR = ((uint32_t)(pin) << 16U))

#if USE_HARDWARE_SPI == 0
#define LCD_SCLK_Clr() LCD_GPIO_CLR(SPI1_SCK_GPIO_Port, SPI1_SCK_Pin)
#define LCD_SCLK_Set() LCD_GPIO_SET(SPI1_SCK_GPIO_Port, SPI1_SCK_Pin)

#define LCD_MOSI_Clr() LCD_GPIO_CLR(SPI1_MOSI_GPIO_Port, SPI1_MOSI_Pin)
#define LCD_MOSI_Set() LCD_GPIO_SET(SPI1_MOSI_GPIO_Port, SPI1_MOSI_Pin)
#endif

#define LCD_RES_Clr() LCD_GPIO_CLR(LCD_RES_GPIO_Port, LCD_RES_Pin)
#define LCD_RES_Set() LCD_GPIO_SET(LCD_RES_GPIO_Port, LCD_RES_Pin)

#define LCD_DC_Clr() LCD_GPIO_CLR(LCD_DC_GPIO_Port, LCD_DC_Pin)
#define LCD_DC_Set() LCD_GPIO_SET(LCD_DC_GPIO_Port, LCD_DC_Pin)

#define LCD_CS_Clr() LCD_GPIO_CLR(LCD_CS_GPIO_Port, LCD_CS_Pin)
#define LCD_CS_Set() LCD_GPIO_SET(LCD_CS_GPIO_Port, LCD_CS_Pin)

#define LCD_BLK_Clr() LCD_GPIO_CLR(LCD_BLK_GPIO_Port, LCD_BLK_Pin)
#define LCD_BLK_Set() LCD_GPIO_SET(LCD_BLK_GPIO_Port, LCD_BLK_Pin)

// Color Define
#define WHITE 0xFFFF
#define BLACK 0x0000
#define BLUE 0x001F
#define BRED 0XF81F
#define GRED 0XFFE0
#define GBLUE 0X07FF
#define RED 0xF800
#define MAGENTA 0xF81F
#define GREEN 0x07E0
#define CYAN 0x7FFF
#define YELLOW 0xFFE0
#define BROWN 0XBC40      // 棕色
#define BRRED 0XFC07      // 棕红色
#define GRAY 0X8430       // 灰色
#define DARKBLUE 0X01CF   // 深蓝色
#define LIGHTBLUE 0X7D7C  // 浅蓝色
#define GRAYBLUE 0X5458   // 灰蓝色
#define LIGHTGREEN 0X841F // 浅绿色
#define LGRAY 0XC618      // 浅灰色(PANNEL),窗体背景色
#define LGRAYBLUE 0XA651  // 浅灰蓝色(中间层颜色)
#define LBBLUE 0X2B12     // 浅棕蓝色(选择条目的反色)

void LCD_GPIO_Init(void);                             // 初始化GPIO
void LCD_Writ_Bus(u8 dat);                            // 模拟SPI时序
void LCD_WR_DATA8(u8 dat);                            // 写入一个字节
void LCD_WR_DATA(u16 dat);                            // 写入两个字节
void LCD_WR_REG(u8 dat);                              // 写入一个指令
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2); // 设置坐标函数
void LCD_Init(void);                                  // LCD初始化

void LCD_Fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color);  // 指定区域填充颜色
void LCD_WriteColorBuffer(u16 xsta, u16 ysta, u16 xend, u16 yend, const u16 *color);
void LCD_DrawPoint(u16 x, u16 y, u16 color);                       // 在指定位置画一个点
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2, u16 color);      // 在指定位置画一条线
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2, u16 color); // 在指定位置画一个矩形
void Draw_Circle(u16 x0, u16 y0, u8 r, u16 color);                 // 在指定位置画一个圆

void LCD_ShowChinese(u16 x, u16 y, u8 *s, u16 fc, u16 bc, u8 sizey, u8 mode); // 显示汉字串
void LCD_ShowChinese12x12(u16 x, u16 y, u8 *s, u16 fc, u16 bc, u8 sizey,
                          u8 mode); // 显示单个12x12汉字
void LCD_ShowChinese16x16(u16 x, u16 y, u8 *s, u16 fc, u16 bc, u8 sizey,
                          u8 mode); // 显示单个16x16汉字
void LCD_ShowChinese24x24(u16 x, u16 y, u8 *s, u16 fc, u16 bc, u8 sizey,
                          u8 mode); // 显示单个24x24汉字
void LCD_ShowChinese32x32(u16 x, u16 y, u8 *s, u16 fc, u16 bc, u8 sizey,
                          u8 mode);                                         // 显示单个32x32汉字
void LCD_ShowChar(u16 x, u16 y, u8 num, u16 fc, u16 bc, u8 sizey, u8 mode); // 显示一个字符
void LCD_ShowString(u16 x, u16 y, const u8 *p, u16 fc, u16 bc, u8 sizey, u8 mode); // 显示字符串
u32 mypow(u8 m, u8 n);                                                             // 求幂
void LCD_ShowIntNum(u16 x, u16 y, u16 num, u8 len, u16 fc, u16 bc, u8 sizey);      // 显示整数变量
void LCD_ShowFloatNum1(u16 x, u16 y, float num, u8 len, u16 fc, u16 bc,
                       u8 sizey);                                          // 显示两位小数变量
void LCD_ShowPicture(u16 x, u16 y, u16 length, u16 width, const u8 pic[]); // 显示图片

#endif
