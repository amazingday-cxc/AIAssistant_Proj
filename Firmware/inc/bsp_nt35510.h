#ifndef BSP_NT35510_H
#define BSP_NT35510_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NT35510 LCD controller driver for the board's 16-bit FSMC interface.
 *
 * Call MX_FSMC_Init() before BSP_NT35510_Init(). Coordinates use a top-left
 * origin. Rectangle APIs take width and height, clip at the display boundary,
 * and transfer pixels in RGB565 format. Calls must be serialized by the
 * application when used from multiple tasks.
 */

#define BSP_NT35510_DEVICE_ID         UINT16_C(0x5510)
#define BSP_NT35510_PORTRAIT_WIDTH    UINT16_C(480)
#define BSP_NT35510_PORTRAIT_HEIGHT   UINT16_C(800)
#define BSP_NT35510_LANDSCAPE_WIDTH   BSP_NT35510_PORTRAIT_HEIGHT
#define BSP_NT35510_LANDSCAPE_HEIGHT  BSP_NT35510_PORTRAIT_WIDTH

#define BSP_NT35510_COLOR_WHITE       UINT16_C(0xFFFF)
#define BSP_NT35510_COLOR_BLACK       UINT16_C(0x0000)
#define BSP_NT35510_COLOR_BLUE        UINT16_C(0x001F)
#define BSP_NT35510_COLOR_RED         UINT16_C(0xF800)
#define BSP_NT35510_COLOR_MAGENTA     UINT16_C(0xF81F)
#define BSP_NT35510_COLOR_GREEN       UINT16_C(0x07E0)
#define BSP_NT35510_COLOR_CYAN        UINT16_C(0x07FF)
#define BSP_NT35510_COLOR_YELLOW      UINT16_C(0xFFE0)
#define BSP_NT35510_COLOR_BROWN       UINT16_C(0xBC40)
#define BSP_NT35510_COLOR_DARK_BLUE   UINT16_C(0x01CF)

#define BSP_NT35510_DATA_REG  ((uint32_t)0x6C000080UL)

typedef enum
{
    BSP_NT35510_STATUS_OK = 0,
    BSP_NT35510_STATUS_BUS_ERROR,
    BSP_NT35510_STATUS_ID_MISMATCH
} BSP_NT35510_Status;

typedef enum
{
    BSP_NT35510_ORIENTATION_PORTRAIT = 0,
    BSP_NT35510_ORIENTATION_LANDSCAPE
} BSP_NT35510_Orientation;

typedef enum
{
    BSP_NT35510_FONT_12 = 12,
    BSP_NT35510_FONT_16 = 16,
    BSP_NT35510_FONT_24 = 24
} BSP_NT35510_Font;

BSP_NT35510_Status BSP_NT35510_Init(void);
void BSP_NT35510_Deinit(void);

bool BSP_NT35510_IsReady(void);
/* Logical controller code and unmodified C5 register signature. */
uint16_t BSP_NT35510_GetDeviceId(void);
uint16_t BSP_NT35510_GetRawDeviceId(void);
uint16_t BSP_NT35510_GetWidth(void);
uint16_t BSP_NT35510_GetHeight(void);
BSP_NT35510_Orientation BSP_NT35510_GetOrientation(void);

bool BSP_NT35510_SetOrientation(BSP_NT35510_Orientation orientation);
void BSP_NT35510_SetBacklight(bool enabled);
void BSP_NT35510_DisplayOn(void);
void BSP_NT35510_DisplayOff(void);

void BSP_NT35510_Clear(uint16_t color);
void BSP_NT35510_FillRect(uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height, uint16_t color);
bool BSP_NT35510_PreparePixelWrite(uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height);
bool BSP_NT35510_WritePixels(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             const uint16_t *pixels);
void BSP_NT35510_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
bool BSP_NT35510_ReadPixel(uint16_t x, uint16_t y, uint16_t *color);
void BSP_NT35510_DrawLine(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1, uint16_t color);
void BSP_NT35510_DrawRect(uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height, uint16_t color);
void BSP_NT35510_DrawCircle(uint16_t center_x, uint16_t center_y,
                            uint16_t radius, uint16_t color);

void BSP_NT35510_DrawChar(uint16_t x, uint16_t y, char character,
                          BSP_NT35510_Font font, uint16_t foreground,
                          uint16_t background, bool transparent);
void BSP_NT35510_DrawString(uint16_t x, uint16_t y,
                            uint16_t region_width, uint16_t region_height,
                            const char *text, BSP_NT35510_Font font,
                            uint16_t foreground, uint16_t background,
                            bool transparent);
void BSP_NT35510_DrawUInt(uint16_t x, uint16_t y, uint32_t value,
                          uint8_t digits, BSP_NT35510_Font font,
                          uint16_t foreground, uint16_t background,
                          bool leading_zero);

#ifdef __cplusplus
}
#endif

#endif /* BSP_NT35510_H */
