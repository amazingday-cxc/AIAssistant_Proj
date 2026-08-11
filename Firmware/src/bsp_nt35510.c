#include "bsp_nt35510.h"

#include "../inc/bsp_nt35510_font.h"
#include "fsmc.h"

#include <stddef.h>

/*
 * Board-specific defaults. They may be overridden with target compile
 * definitions without changing the controller driver.
 */
#ifndef BSP_NT35510_COMMAND_ADDRESS
#define BSP_NT35510_COMMAND_ADDRESS  ((uintptr_t)0x6C00007EUL)
#endif

#ifndef BSP_NT35510_DATA_ADDRESS
#define BSP_NT35510_DATA_ADDRESS     ((uintptr_t)0x6C000080UL)
#endif

#ifndef BSP_NT35510_FSMC_HANDLE
#define BSP_NT35510_FSMC_HANDLE      hsram2
#endif

#ifndef BSP_NT35510_BACKLIGHT_PORT
#define BSP_NT35510_BACKLIGHT_PORT   GPIOB
#endif

#ifndef BSP_NT35510_BACKLIGHT_PIN
#define BSP_NT35510_BACKLIGHT_PIN    GPIO_PIN_15
#endif

#ifndef BSP_NT35510_BACKLIGHT_CLOCK_ENABLE
#define BSP_NT35510_BACKLIGHT_CLOCK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

#ifndef BSP_NT35510_BACKLIGHT_ACTIVE_LEVEL
#define BSP_NT35510_BACKLIGHT_ACTIVE_LEVEL GPIO_PIN_SET
#endif

#define NT35510_ARRAY_SIZE(array) \
    (sizeof(array) / sizeof((array)[0]))

#define NT35510_RAW_ID_SIGNATURE       UINT16_C(0x8000)
#define NT35510_CMD_COLUMN_ADDRESS     UINT16_C(0x2A00)
#define NT35510_CMD_PAGE_ADDRESS       UINT16_C(0x2B00)
#define NT35510_CMD_MEMORY_WRITE       UINT16_C(0x2C00)
#define NT35510_CMD_MEMORY_READ        UINT16_C(0x2E00)
#define NT35510_CMD_DISPLAY_OFF        UINT16_C(0x2800)
#define NT35510_CMD_DISPLAY_ON         UINT16_C(0x2900)
#define NT35510_CMD_SLEEP_OUT          UINT16_C(0x1100)
#define NT35510_REG_MEMORY_ACCESS      UINT16_C(0x3600)

#define NT35510_MADCTL_PORTRAIT        UINT8_C(0x00)
#define NT35510_MADCTL_LANDSCAPE       UINT8_C(0xA0)

typedef struct
{
    uint16_t address;
    uint8_t value;
} NT35510_RegisterValue;

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t device_id;
    uint16_t raw_device_id;
    BSP_NT35510_Orientation orientation;
    bool ready;
} NT35510_State;

static NT35510_State nt35510_state = {
    .width = BSP_NT35510_PORTRAIT_WIDTH,
    .height = BSP_NT35510_PORTRAIT_HEIGHT,
    .device_id = 0U,
    .raw_device_id = 0U,
    .orientation = BSP_NT35510_ORIENTATION_PORTRAIT,
    .ready = false,
};

static const NT35510_RegisterValue nt35510_power_sequence[] = {
    {0xB000U, 0x0DU}, {0xB001U, 0x0DU}, {0xB002U, 0x0DU},
    {0xB600U, 0x34U}, {0xB601U, 0x34U}, {0xB602U, 0x34U},
    {0xB100U, 0x0DU}, {0xB101U, 0x0DU}, {0xB102U, 0x0DU},
    {0xB700U, 0x34U}, {0xB701U, 0x34U}, {0xB702U, 0x34U},
    {0xB200U, 0x00U}, {0xB201U, 0x00U}, {0xB202U, 0x00U},
    {0xB800U, 0x24U}, {0xB801U, 0x24U}, {0xB802U, 0x24U},
    {0xBF00U, 0x01U},
    {0xB300U, 0x0FU}, {0xB301U, 0x0FU}, {0xB302U, 0x0FU},
    {0xB900U, 0x34U}, {0xB901U, 0x34U}, {0xB902U, 0x34U},
    {0xB500U, 0x08U}, {0xB501U, 0x08U}, {0xB502U, 0x08U},
    {0xC200U, 0x03U},
    {0xBA00U, 0x24U}, {0xBA01U, 0x24U}, {0xBA02U, 0x24U},
    {0xBC00U, 0x00U}, {0xBC01U, 0x78U}, {0xBC02U, 0x00U},
    {0xBD00U, 0x00U}, {0xBD01U, 0x78U}, {0xBD02U, 0x00U},
    {0xBE00U, 0x00U}, {0xBE01U, 0x64U},
};

static const uint8_t nt35510_gamma_curve[] = {
    0x00U, 0x33U, 0x00U, 0x34U, 0x00U, 0x3AU, 0x00U, 0x4AU,
    0x00U, 0x5CU, 0x00U, 0x81U, 0x00U, 0xA6U, 0x00U, 0xE5U,
    0x01U, 0x13U, 0x01U, 0x54U, 0x01U, 0x82U, 0x01U, 0xCAU,
    0x02U, 0x00U, 0x02U, 0x01U, 0x02U, 0x34U, 0x02U, 0x67U,
    0x02U, 0x84U, 0x02U, 0xA4U, 0x02U, 0xB7U, 0x02U, 0xCFU,
    0x02U, 0xDEU, 0x02U, 0xF2U, 0x02U, 0xFEU, 0x03U, 0x10U,
    0x03U, 0x33U, 0x03U, 0x6DU,
};

static const NT35510_RegisterValue nt35510_display_sequence[] = {
    {0xB100U, 0xCCU}, {0xB101U, 0x00U},
    {0xB600U, 0x05U},
    {0xB700U, 0x70U}, {0xB701U, 0x70U},
    {0xB800U, 0x01U}, {0xB801U, 0x03U},
    {0xB802U, 0x03U}, {0xB803U, 0x03U},
    {0xBC00U, 0x02U}, {0xBC01U, 0x00U}, {0xBC02U, 0x00U},
    {0xC900U, 0xD0U}, {0xC901U, 0x02U}, {0xC902U, 0x50U},
    {0xC903U, 0x50U}, {0xC904U, 0x50U},
    {0x3500U, 0x00U},
    {0x3A00U, 0x55U},
};

/**
 * @brief  Write a 16-bit command word to the controller command register.
 * @param  command  NT35510 command/register index to latch on the bus.
 * @note   Issues an FSMC write to BSP_NT35510_COMMAND_ADDRESS (RS/A0 low).
 * @return None.
 */
static inline void nt35510_write_command(uint16_t command)
{
    *(volatile uint16_t *)BSP_NT35510_COMMAND_ADDRESS = command;
}

/**
 * @brief  Write a 16-bit data word to the controller data register.
 * @param  data  Value to place on the data bus (RGB565 pixel or parameter).
 * @note   Issues an FSMC write to BSP_NT35510_DATA_ADDRESS (RS/A0 high).
 * @return None.
 */
static inline void nt35510_write_data(uint16_t data)
{
    *(volatile uint16_t *)BSP_NT35510_DATA_ADDRESS = data;
}

/**
 * @brief  Read a 16-bit data word from the controller data register.
 * @return The value currently driven on the FSMC data bus.
 */
static inline uint16_t nt35510_read_data(void)
{
    return *(volatile uint16_t *)BSP_NT35510_DATA_ADDRESS;
}

/**
 * @brief  Write a single value into a controller register.
 * @param  address  Register index to select before writing.
 * @param  value    16-bit value written to the selected register.
 * @note   Convenience wrapper that emits a command then a data word.
 * @return None.
 */
static void nt35510_write_register(uint16_t address, uint16_t value)
{
    nt35510_write_command(address);
    nt35510_write_data(value);
}

/**
 * @brief  Read the value of a controller register.
 * @param  address  Register index to select before reading.
 * @return The 16-bit value returned by the controller.
 */
static uint16_t nt35510_read_register(uint16_t address)
{
    nt35510_write_command(address);
    return nt35510_read_data();
}

/**
 * @brief  Write an array of address/value register pairs in order.
 * @param  sequence  Pointer to the first register/value pair.
 * @param  count     Number of pairs to program.
 * @return None.
 */
static void nt35510_write_sequence(const NT35510_RegisterValue *sequence,
                                    size_t count)
{
    size_t index;

    for (index = 0U; index < count; ++index)
    {
        nt35510_write_register(sequence[index].address,
                               sequence[index].value);
    }
}

/**
 * @brief  Unlock and select an NT35510 command page (manufacturer command set).
 * @param  page  Command page number to activate (e.g. 0 or 1).
 * @note   Emits the fixed 0x55/0xAA/0x52/0x08 unlock key before the page byte.
 * @return None.
 */
static void nt35510_select_command_page(uint8_t page)
{
    nt35510_write_register(0xF000U, 0x55U);
    nt35510_write_register(0xF001U, 0xAAU);
    nt35510_write_register(0xF002U, 0x52U);
    nt35510_write_register(0xF003U, 0x08U);
    nt35510_write_register(0xF004U, page);
}

/**
 * @brief  Program the gamma correction tables for all six gamma banks.
 * @note   Writes nt35510_gamma_curve into register banks 0xD1xx..0xD6xx.
 *         Command page 1 must already be selected.
 * @return None.
 */
static void nt35510_program_gamma(void)
{
    uint16_t page;
    size_t offset;

    for (page = 1U; page <= 6U; ++page)
    {
        const uint16_t base_address =
            (uint16_t)(0xD000U | (uint16_t)(page << 8U));

        for (offset = 0U;
             offset < NT35510_ARRAY_SIZE(nt35510_gamma_curve);
             ++offset)
        {
            nt35510_write_register(
                (uint16_t)(base_address + (uint16_t)offset),
                nt35510_gamma_curve[offset]);
        }
    }
}

/**
 * @brief  Run the full controller initialization command sequence.
 * @note   Applies the power sequence and gamma tables on command page 1,
 *         then the display sequence on command page 0.
 * @return None.
 */
static void nt35510_program_controller(void)
{
    nt35510_select_command_page(1U);
    nt35510_write_sequence(nt35510_power_sequence,
                           NT35510_ARRAY_SIZE(nt35510_power_sequence));
    nt35510_program_gamma();

    nt35510_select_command_page(0U);
    nt35510_write_sequence(nt35510_display_sequence,
                           NT35510_ARRAY_SIZE(nt35510_display_sequence));
}

/**
 * @brief  Read the controller identification code from the C5 registers.
 * @note   Selects command page 1 and combines registers 0xC500/0xC501.
 * @return 16-bit device identifier reported by the controller.
 */
static uint16_t nt35510_read_device_id(void)
{
    uint16_t high_byte;
    uint16_t low_byte;

    nt35510_select_command_page(1U);
    high_byte = (uint16_t)(nt35510_read_register(0xC500U) & 0x00FFU);
    low_byte = (uint16_t)(nt35510_read_register(0xC501U) & 0x00FFU);

    return (uint16_t)((uint16_t)(high_byte << 8U) | low_byte);
}

/**
 * @brief  Test whether a raw ID belongs to a supported NT35510 controller.
 * @param  raw_id  Identifier read back from the controller.
 * @return true if the ID matches the NT35510 (0x5510 or the 0x8000 wiring
 *         signature); false otherwise.
 */
static bool nt35510_device_id_matches(uint16_t raw_id)
{
    /*
     * This board's 16-bit FSMC wiring can expose the NT35510 C5 signature as
     * 0x8000. Both values identify the same controller; the public ID remains
     * the controller code 0x5510.
     */
    return raw_id == BSP_NT35510_DEVICE_ID ||
           raw_id == NT35510_RAW_ID_SIGNATURE;
}

/**
 * @brief  Configure the FSMC bank timings used to talk to the panel.
 * @note   Validates that the SRAM handle targets bank 4 with a 16-bit bus,
 *         then applies separate read and write timings in extended mode.
 * @return HAL_OK on success, HAL_ERROR if the FSMC handle is not usable.
 */
static HAL_StatusTypeDef nt35510_configure_bus(void)
{
    SRAM_HandleTypeDef *const handle = &BSP_NT35510_FSMC_HANDLE;
    FSMC_NORSRAM_TimingTypeDef read_timing = {0};
    FSMC_NORSRAM_TimingTypeDef write_timing = {0};

    if (handle->Instance == NULL ||
        handle->Init.NSBank != FSMC_NORSRAM_BANK4 ||
        handle->Init.MemoryDataWidth != FSMC_NORSRAM_MEM_BUS_WIDTH_16)
    {
        return HAL_ERROR;
    }

    read_timing.AddressSetupTime = 15U;
    read_timing.AddressHoldTime = 0U;
    read_timing.DataSetupTime = 60U;
    read_timing.BusTurnAroundDuration = 0U;
    read_timing.CLKDivision = 2U;
    read_timing.DataLatency = 2U;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;

    write_timing.AddressSetupTime = 3U;
    write_timing.AddressHoldTime = 0U;
    write_timing.DataSetupTime = 2U;
    write_timing.BusTurnAroundDuration = 0U;
    write_timing.CLKDivision = 2U;
    write_timing.DataLatency = 2U;
    write_timing.AccessMode = FSMC_ACCESS_MODE_A;

    handle->Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    return HAL_SRAM_Init(handle, &read_timing, &write_timing);
}

/**
 * @brief  Enable the backlight GPIO clock and configure the control pin.
 * @note   Leaves the backlight switched off until BSP_NT35510_SetBacklight()
 *         is called.
 * @return None.
 */
static void nt35510_init_backlight(void)
{
    GPIO_InitTypeDef gpio = {0};

    BSP_NT35510_BACKLIGHT_CLOCK_ENABLE();
    BSP_NT35510_SetBacklight(false);

    gpio.Pin = BSP_NT35510_BACKLIGHT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_NT35510_BACKLIGHT_PORT, &gpio);
}

/**
 * @brief  Write a 16-bit coordinate into a two-register address field.
 * @param  base_address  Index of the high-byte register; the low byte goes
 *                        to base_address + 1.
 * @param  value         16-bit coordinate to split across the two registers.
 * @return None.
 */
static void nt35510_write_address(uint16_t base_address, uint16_t value)
{
    nt35510_write_register(base_address, (uint8_t)(value >> 8U));
    nt35510_write_register((uint16_t)(base_address + 1U),
                           (uint8_t)(value & 0x00FFU));
}

/**
 * @brief  Set the GRAM read/write window (column and page address ranges).
 * @param  x       Left edge of the window in pixels.
 * @param  y       Top edge of the window in pixels.
 * @param  width   Window width in pixels.
 * @param  height  Window height in pixels.
 * @note   Callers must ensure the rectangle lies within the panel bounds.
 * @return None.
 */
static void nt35510_set_address_window(uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height)
{
    const uint16_t end_x = (uint16_t)(x + width - 1U);
    const uint16_t end_y = (uint16_t)(y + height - 1U);

    nt35510_write_address(NT35510_CMD_COLUMN_ADDRESS, x);
    nt35510_write_address((uint16_t)(NT35510_CMD_COLUMN_ADDRESS + 2U),
                          end_x);
    nt35510_write_address(NT35510_CMD_PAGE_ADDRESS, y);
    nt35510_write_address((uint16_t)(NT35510_CMD_PAGE_ADDRESS + 2U),
                          end_y);
}

/**
 * @brief  Issue the memory-write command to start streaming pixel data.
 * @note   Following nt35510_write_data() calls fill the active window.
 * @return None.
 */
static void nt35510_begin_memory_write(void)
{
    nt35510_write_command(NT35510_CMD_MEMORY_WRITE);
}

/**
 * @brief  Stream one color repeatedly into the active GRAM window.
 * @param  color  RGB565 color written to every pixel.
 * @param  count  Number of pixels to write.
 * @note   The address window and memory-write command must be set beforehand;
 *         the loop is unrolled by eight for throughput.
 * @return None.
 */
static void nt35510_write_solid_pixels(uint16_t color, uint32_t count)
{
    while (count >= 8U)
    {
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        nt35510_write_data(color);
        count -= 8U;
    }

    while (count != 0U)
    {
        nt35510_write_data(color);
        --count;
    }
}

/**
 * @brief  Apply a display orientation and update the cached geometry.
 * @param  orientation  Portrait or landscape orientation to program.
 * @note   Updates the MADCTL register plus the tracked width/height and
 *         resets the address window to the full screen.
 * @return true if the orientation was valid and applied; false otherwise.
 */
static bool nt35510_apply_orientation(BSP_NT35510_Orientation orientation)
{
    uint8_t memory_access;

    switch (orientation)
    {
        case BSP_NT35510_ORIENTATION_PORTRAIT:
            nt35510_state.width = BSP_NT35510_PORTRAIT_WIDTH;
            nt35510_state.height = BSP_NT35510_PORTRAIT_HEIGHT;
            memory_access = NT35510_MADCTL_PORTRAIT;
            break;

        case BSP_NT35510_ORIENTATION_LANDSCAPE:
            nt35510_state.width = BSP_NT35510_LANDSCAPE_WIDTH;
            nt35510_state.height = BSP_NT35510_LANDSCAPE_HEIGHT;
            memory_access = NT35510_MADCTL_LANDSCAPE;
            break;

        default:
            return false;
    }

    nt35510_state.orientation = orientation;
    nt35510_write_register(NT35510_REG_MEMORY_ACCESS, memory_access);
    nt35510_set_address_window(0U, 0U, nt35510_state.width,
                               nt35510_state.height);
    return true;
}

/**
 * @brief  Clip a rectangle against the current screen bounds.
 * @param  x       Left edge of the rectangle.
 * @param  y       Top edge of the rectangle.
 * @param  width   In/out: requested width, shrunk to fit the screen.
 * @param  height  In/out: requested height, shrunk to fit the screen.
 * @return true if the rectangle has a visible area after clipping; false if
 *         it is empty, out of bounds, or the pointers are NULL.
 */
static bool nt35510_clip_rectangle(uint16_t x, uint16_t y,
                                    uint16_t *width, uint16_t *height)
{
    uint16_t maximum_width;
    uint16_t maximum_height;

    if (width == NULL || height == NULL ||
        *width == 0U || *height == 0U ||
        x >= nt35510_state.width || y >= nt35510_state.height)
    {
        return false;
    }

    maximum_width = (uint16_t)(nt35510_state.width - x);
    maximum_height = (uint16_t)(nt35510_state.height - y);

    if (*width > maximum_width)
    {
        *width = maximum_width;
    }
    if (*height > maximum_height)
    {
        *height = maximum_height;
    }

    return true;
}

/**
 * @brief  Draw a single pixel without bounds checking.
 * @param  x      Pixel X coordinate (assumed in range).
 * @param  y      Pixel Y coordinate (assumed in range).
 * @param  color  RGB565 color to write.
 * @return None.
 */
static void nt35510_draw_pixel_unchecked(uint16_t x, uint16_t y,
                                          uint16_t color)
{
    nt35510_set_address_window(x, y, 1U, 1U);
    nt35510_begin_memory_write();
    nt35510_write_data(color);
}

/**
 * @brief  Draw a pixel from signed coordinates, skipping off-screen points.
 * @param  x      Signed X coordinate; ignored if outside the screen.
 * @param  y      Signed Y coordinate; ignored if outside the screen.
 * @param  color  RGB565 color to write.
 * @return None.
 */
static void nt35510_draw_pixel_signed(int32_t x, int32_t y,
                                       uint16_t color)
{
    if (x >= 0 && y >= 0 &&
        x < (int32_t)nt35510_state.width &&
        y < (int32_t)nt35510_state.height)
    {
        nt35510_draw_pixel_unchecked((uint16_t)x, (uint16_t)y, color);
    }
}

enum
{
    NT35510_LINE_LEFT = 1U << 0,
    NT35510_LINE_RIGHT = 1U << 1,
    NT35510_LINE_TOP = 1U << 2,
    NT35510_LINE_BOTTOM = 1U << 3
};

/**
 * @brief  Compute the Cohen-Sutherland region code for a point.
 * @param  x  Point X coordinate.
 * @param  y  Point Y coordinate.
 * @return Bitmask of NT35510_LINE_* flags describing which edges the point
 *         lies outside of (0 when inside the screen).
 */
static uint8_t nt35510_line_out_code(int32_t x, int32_t y)
{
    uint8_t code = 0U;

    if (x < 0)
    {
        code |= NT35510_LINE_LEFT;
    }
    else if (x >= (int32_t)nt35510_state.width)
    {
        code |= NT35510_LINE_RIGHT;
    }

    if (y < 0)
    {
        code |= NT35510_LINE_TOP;
    }
    else if (y >= (int32_t)nt35510_state.height)
    {
        code |= NT35510_LINE_BOTTOM;
    }

    return code;
}

/**
 * @brief  Clip a line segment to the screen using Cohen-Sutherland.
 * @param  x0  In/out: X of the first endpoint, updated to the clipped point.
 * @param  y0  In/out: Y of the first endpoint, updated to the clipped point.
 * @param  x1  In/out: X of the second endpoint, updated to the clipped point.
 * @param  y1  In/out: Y of the second endpoint, updated to the clipped point.
 * @return true if any part of the segment is visible; false if fully clipped.
 */
static bool nt35510_clip_line(int32_t *x0, int32_t *y0,
                               int32_t *x1, int32_t *y1)
{
    uint8_t code0 = nt35510_line_out_code(*x0, *y0);
    uint8_t code1 = nt35510_line_out_code(*x1, *y1);

    for (;;)
    {
        uint8_t outside;
        int32_t x;
        int32_t y;

        if ((code0 | code1) == 0U)
        {
            return true;
        }
        if ((code0 & code1) != 0U)
        {
            return false;
        }

        outside = code0 != 0U ? code0 : code1;

        if ((outside & NT35510_LINE_TOP) != 0U)
        {
            y = 0;
            x = *x0 + (int32_t)(((int64_t)(*x1 - *x0) *
                                 (int64_t)(y - *y0)) /
                                (int64_t)(*y1 - *y0));
        }
        else if ((outside & NT35510_LINE_BOTTOM) != 0U)
        {
            y = (int32_t)nt35510_state.height - 1;
            x = *x0 + (int32_t)(((int64_t)(*x1 - *x0) *
                                 (int64_t)(y - *y0)) /
                                (int64_t)(*y1 - *y0));
        }
        else if ((outside & NT35510_LINE_RIGHT) != 0U)
        {
            x = (int32_t)nt35510_state.width - 1;
            y = *y0 + (int32_t)(((int64_t)(*y1 - *y0) *
                                 (int64_t)(x - *x0)) /
                                (int64_t)(*x1 - *x0));
        }
        else
        {
            x = 0;
            y = *y0 + (int32_t)(((int64_t)(*y1 - *y0) *
                                 (int64_t)(x - *x0)) /
                                (int64_t)(*x1 - *x0));
        }

        if (outside == code0)
        {
            *x0 = x;
            *y0 = y;
            code0 = nt35510_line_out_code(*x0, *y0);
        }
        else
        {
            *x1 = x;
            *y1 = y;
            code1 = nt35510_line_out_code(*x1, *y1);
        }
    }
}

/**
 * @brief  Resolve the bitmap and metrics for a printable ASCII character.
 * @param  character  Character to look up (must be in the range ' '..'~').
 * @param  font       Font size selector.
 * @param  glyph      Out: pointer to the glyph bitmap data.
 * @param  width      Out: glyph width in pixels.
 * @param  height     Out: glyph height in pixels.
 * @return true if the character and font are supported; false otherwise.
 */
static bool nt35510_get_glyph(char character, BSP_NT35510_Font font,
                               const uint8_t **glyph, uint8_t *width,
                               uint8_t *height)
{
    const uint8_t code = (uint8_t)character;
    const uint8_t glyph_index = (uint8_t)(code - (uint8_t)' ');

    if (glyph == NULL || width == NULL || height == NULL ||
        code < (uint8_t)' ' || code > (uint8_t)'~')
    {
        return false;
    }

    *height = (uint8_t)font;
    *width = (uint8_t)(*height / 2U);

    switch (font)
    {
        case BSP_NT35510_FONT_12:
            *glyph = nt35510_font_12x6[glyph_index];
            return true;

        case BSP_NT35510_FONT_16:
            *glyph = nt35510_font_16x8[glyph_index];
            return true;

        case BSP_NT35510_FONT_24:
            *glyph = nt35510_font_24x12[glyph_index];
            return true;

        default:
            return false;
    }
}

/**
 * @brief  Test whether a specific pixel of a glyph bitmap is set.
 * @param  glyph   Glyph bitmap data (column-major, MSB-first per byte).
 * @param  height  Glyph height in pixels (determines bytes per column).
 * @param  column  Column index within the glyph.
 * @param  row     Row index within the glyph.
 * @return true if the addressed pixel is part of the glyph; false otherwise.
 */
static bool nt35510_glyph_pixel(const uint8_t *glyph, uint8_t height,
                                 uint8_t column, uint8_t row)
{
    const uint8_t bytes_per_column =
        (uint8_t)((height + 7U) / 8U);
    const size_t byte_index =
        (size_t)column * bytes_per_column + (size_t)(row / 8U);
    const uint8_t bit = (uint8_t)(0x80U >> (row % 8U));

    return (glyph[byte_index] & bit) != 0U;
}

/**
 * @brief  Initialize the NT35510 panel and prepare it for drawing.
 * @note   Configures the FSMC bus, verifies the controller ID, programs the
 *         controller, selects portrait orientation, clears to white, turns the
 *         display on and enables the backlight. Call MX_FSMC_Init() first.
 * @retval BSP_NT35510_STATUS_OK          Panel initialized successfully.
 * @retval BSP_NT35510_STATUS_BUS_ERROR   FSMC bus configuration failed.
 * @retval BSP_NT35510_STATUS_ID_MISMATCH Controller ID was not recognized.
 */
BSP_NT35510_Status BSP_NT35510_Init(void)
{
    nt35510_state.ready = false;
    nt35510_state.width = BSP_NT35510_PORTRAIT_WIDTH;
    nt35510_state.height = BSP_NT35510_PORTRAIT_HEIGHT;
    nt35510_state.device_id = 0U;
    nt35510_state.raw_device_id = 0U;
    nt35510_state.orientation = BSP_NT35510_ORIENTATION_PORTRAIT;
    nt35510_init_backlight();

    if (nt35510_configure_bus() != HAL_OK)
    {
        return BSP_NT35510_STATUS_BUS_ERROR;
    }

    HAL_Delay(50U);
    nt35510_state.raw_device_id = nt35510_read_device_id();

    if (!nt35510_device_id_matches(nt35510_state.raw_device_id))
    {
        return BSP_NT35510_STATUS_ID_MISMATCH;
    }

    nt35510_state.device_id = BSP_NT35510_DEVICE_ID;
    nt35510_program_controller();
    nt35510_write_command(NT35510_CMD_SLEEP_OUT);
    HAL_Delay(120U);
    (void)nt35510_apply_orientation(
        BSP_NT35510_ORIENTATION_PORTRAIT);

    nt35510_state.ready = true;
    BSP_NT35510_Clear(BSP_NT35510_COLOR_WHITE);
    nt35510_write_command(NT35510_CMD_DISPLAY_ON);
    BSP_NT35510_SetBacklight(true);

    return BSP_NT35510_STATUS_OK;
}

/**
 * @brief  Turn the display and backlight off and mark the driver not ready.
 * @note   Safe to call whether or not the panel was initialized.
 * @return None.
 */
void BSP_NT35510_Deinit(void)
{
    BSP_NT35510_SetBacklight(false);
    if (nt35510_state.ready)
    {
        nt35510_write_command(NT35510_CMD_DISPLAY_OFF);
    }
    nt35510_state.ready = false;
}

/**
 * @brief  Query whether the panel has been successfully initialized.
 * @return true if the driver is ready for drawing; false otherwise.
 */
bool BSP_NT35510_IsReady(void)
{
    return nt35510_state.ready;
}

/**
 * @brief  Get the logical controller identifier.
 * @return BSP_NT35510_DEVICE_ID (0x5510) once initialized, 0 otherwise.
 */
uint16_t BSP_NT35510_GetDeviceId(void)
{
    return nt35510_state.device_id;
}

/**
 * @brief  Get the unmodified controller signature read from the C5 registers.
 * @return Raw device ID (0x5510 or the board's 0x8000 wiring signature).
 */
uint16_t BSP_NT35510_GetRawDeviceId(void)
{
    return nt35510_state.raw_device_id;
}

/**
 * @brief  Get the current active display width.
 * @return Width in pixels for the current orientation.
 */
uint16_t BSP_NT35510_GetWidth(void)
{
    return nt35510_state.width;
}

/**
 * @brief  Get the current active display height.
 * @return Height in pixels for the current orientation.
 */
uint16_t BSP_NT35510_GetHeight(void)
{
    return nt35510_state.height;
}

/**
 * @brief  Get the currently configured display orientation.
 * @return BSP_NT35510_ORIENTATION_PORTRAIT or _LANDSCAPE.
 */
BSP_NT35510_Orientation BSP_NT35510_GetOrientation(void)
{
    return nt35510_state.orientation;
}

/**
 * @brief  Change the display orientation at runtime.
 * @param  orientation  Portrait or landscape orientation to apply.
 * @note   Updates the reported width/height; existing screen content is not
 *         re-rendered.
 * @return true on success; false if the driver is not ready or the value is
 *         invalid.
 */
bool BSP_NT35510_SetOrientation(BSP_NT35510_Orientation orientation)
{
    if (!nt35510_state.ready)
    {
        return false;
    }

    return nt35510_apply_orientation(orientation);
}

/**
 * @brief  Switch the panel backlight on or off.
 * @param  enabled  true to turn the backlight on, false to turn it off.
 * @note   Honors the configured active level (BSP_NT35510_BACKLIGHT_ACTIVE_LEVEL).
 * @return None.
 */
void BSP_NT35510_SetBacklight(bool enabled)
{
    const GPIO_PinState active = BSP_NT35510_BACKLIGHT_ACTIVE_LEVEL;
    const GPIO_PinState inactive =
        active == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET;

    HAL_GPIO_WritePin(BSP_NT35510_BACKLIGHT_PORT,
                      BSP_NT35510_BACKLIGHT_PIN,
                      enabled ? active : inactive);
}

/**
 * @brief  Turn the display output on (does not touch the backlight).
 * @note   No effect if the driver is not ready.
 * @return None.
 */
void BSP_NT35510_DisplayOn(void)
{
    if (nt35510_state.ready)
    {
        nt35510_write_command(NT35510_CMD_DISPLAY_ON);
    }
}

/**
 * @brief  Turn the display output off (does not touch the backlight).
 * @note   No effect if the driver is not ready.
 * @return None.
 */
void BSP_NT35510_DisplayOff(void)
{
    if (nt35510_state.ready)
    {
        nt35510_write_command(NT35510_CMD_DISPLAY_OFF);
    }
}

/**
 * @brief  Fill the entire screen with a single color.
 * @param  color  RGB565 color to fill.
 * @note   No effect if the driver is not ready.
 * @return None.
 */
void BSP_NT35510_Clear(uint16_t color)
{
    if (!nt35510_state.ready)
    {
        return;
    }

    nt35510_set_address_window(0U, 0U, nt35510_state.width,
                               nt35510_state.height);
    nt35510_begin_memory_write();
    nt35510_write_solid_pixels(
        color, (uint32_t)nt35510_state.width * nt35510_state.height);
}

/**
 * @brief  Fill a rectangular area with a single color.
 * @param  x       Left edge in pixels.
 * @param  y       Top edge in pixels.
 * @param  width   Rectangle width in pixels.
 * @param  height  Rectangle height in pixels.
 * @param  color   RGB565 fill color.
 * @note   The rectangle is clipped to the screen; off-screen or empty
 *         rectangles are ignored.
 * @return None.
 */
void BSP_NT35510_FillRect(uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height, uint16_t color)
{
    if (!nt35510_state.ready ||
        !nt35510_clip_rectangle(x, y, &width, &height))
    {
        return;
    }

    nt35510_set_address_window(x, y, width, height);
    nt35510_begin_memory_write();
    nt35510_write_solid_pixels(color, (uint32_t)width * height);
}

bool BSP_NT35510_PreparePixelWrite(uint16_t x, uint16_t y,
                                    uint16_t width, uint16_t height)
{
    if (!nt35510_state.ready || width == 0U || height == 0U || x >= nt35510_state.width || y >= nt35510_state.height)
    {
        return false;
    }
    nt35510_set_address_window(x, y, width, height);
    nt35510_begin_memory_write();
    return true;
}

/**
 * @brief  Blit an in-bounds RGB565 pixel buffer without clipping.
 * @param  x       Left edge in pixels (assumed in range).
 * @param  y       Top edge in pixels (assumed in range).
 * @param  width   Region width in pixels.
 * @param  height  Region height in pixels.
 * @param  pixels  Pointer to width*height RGB565 pixels in row-major order.
 * @note   Intended for LVGL flush buffers whose area is already clipped. The
 *         transfer loop writes eight pixels per iteration to the fixed FSMC
 *         data address for maximum throughput.
 * @return true if the region was drawn; false on invalid arguments.
 */
bool BSP_NT35510_WritePixelsFast(uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  const uint16_t *pixels)
{
    volatile uint16_t *const lcd_data =
        (volatile uint16_t *)BSP_NT35510_DATA_ADDRESS;
    uint32_t pixel_count = (uint32_t)width * height;

    if (!nt35510_state.ready || pixels == NULL || pixel_count == 0U)
    {
        return false;
    }

    nt35510_set_address_window(x, y, width, height);
    nt35510_begin_memory_write();

    while (pixel_count >= 8U)
    {
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        *lcd_data = *pixels++;
        pixel_count -= 8U;
    }

    while (pixel_count != 0U)
    {
        *lcd_data = *pixels++;
        --pixel_count;
    }

    return true;
}

/**
 * @brief  Blit a RGB565 pixel buffer into a rectangular screen region.
 * @param  x       Left edge in pixels.
 * @param  y       Top edge in pixels.
 * @param  width   Source/region width in pixels (used as the source stride).
 * @param  height  Source/region height in pixels.
 * @param  pixels  Pointer to width*height RGB565 pixels in row-major order.
 * @note   The destination is clipped to the screen; the source stride stays
 *         equal to the original width so partially clipped images stay aligned.
 * @return true if the region was drawn; false on invalid arguments or when
 *         fully clipped.
 */
bool BSP_NT35510_WritePixels(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             const uint16_t *pixels)
{
    const uint16_t source_stride = width;
    uint16_t row;

    if (!nt35510_state.ready || pixels == NULL ||
        !nt35510_clip_rectangle(x, y, &width, &height))
    {
        return false;
    }

    nt35510_set_address_window(x, y, width, height);
    nt35510_begin_memory_write();

    for (row = 0U; row < height; ++row)
    {
        uint16_t column;
        const uint16_t *const source_row =
            pixels + (uint32_t)row * source_stride;

        for (column = 0U; column < width; ++column)
        {
            nt35510_write_data(source_row[column]);
        }
    }

    return true;
}

/**
 * @brief  Draw a single pixel with bounds checking.
 * @param  x      Pixel X coordinate.
 * @param  y      Pixel Y coordinate.
 * @param  color  RGB565 color.
 * @note   Off-screen coordinates and an uninitialized driver are ignored.
 * @return None.
 */
void BSP_NT35510_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!nt35510_state.ready ||
        x >= nt35510_state.width || y >= nt35510_state.height)
    {
        return;
    }

    nt35510_draw_pixel_unchecked(x, y, color);
}

/**
 * @brief  Read the color of a single pixel from GRAM.
 * @param  x      Pixel X coordinate.
 * @param  y      Pixel Y coordinate.
 * @param  color  Out: RGB565 color of the pixel.
 * @note   Repacks the controller's read-back format (18-bit RGB) into RGB565.
 * @return true on success; false on invalid arguments or off-screen access.
 */
bool BSP_NT35510_ReadPixel(uint16_t x, uint16_t y, uint16_t *color)
{
    uint16_t red_green;
    uint16_t blue;

    if (!nt35510_state.ready || color == NULL ||
        x >= nt35510_state.width || y >= nt35510_state.height)
    {
        return false;
    }

    nt35510_set_address_window(x, y, 1U, 1U);
    nt35510_write_command(NT35510_CMD_MEMORY_READ);
    (void)nt35510_read_data();
    red_green = nt35510_read_data();
    blue = nt35510_read_data();

    *color = (uint16_t)(
        (((red_green >> 11U) & 0x001FU) << 11U) |
        (((red_green & 0x00FFU) >> 2U) << 5U) |
        ((blue >> 11U) & 0x001FU));
    return true;
}

/**
 * @brief  Draw a straight line between two points.
 * @param  x0     X of the first endpoint.
 * @param  y0     Y of the first endpoint.
 * @param  x1     X of the second endpoint.
 * @param  y1     Y of the second endpoint.
 * @param  color  RGB565 line color.
 * @note   The segment is clipped to the screen; horizontal and vertical lines
 *         use fast fills, others use Bresenham's algorithm.
 * @return None.
 */
void BSP_NT35510_DrawLine(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1, uint16_t color)
{
    int32_t current_x = x0;
    int32_t current_y = y0;
    int32_t end_x = x1;
    int32_t end_y = y1;
    int32_t delta_x;
    int32_t delta_y;
    int32_t step_x;
    int32_t step_y;
    int32_t error;

    if (!nt35510_state.ready ||
        !nt35510_clip_line(&current_x, &current_y, &end_x, &end_y))
    {
        return;
    }

    if (current_y == end_y)
    {
        const uint16_t start =
            (uint16_t)(current_x < end_x ? current_x : end_x);
        const uint16_t width =
            (uint16_t)((current_x < end_x ? end_x - current_x :
                        current_x - end_x) + 1);
        BSP_NT35510_FillRect(start, (uint16_t)current_y,
                             width, 1U, color);
        return;
    }

    if (current_x == end_x)
    {
        const uint16_t start =
            (uint16_t)(current_y < end_y ? current_y : end_y);
        const uint16_t height =
            (uint16_t)((current_y < end_y ? end_y - current_y :
                        current_y - end_y) + 1);
        BSP_NT35510_FillRect((uint16_t)current_x, start,
                             1U, height, color);
        return;
    }

    delta_x = end_x >= current_x ? end_x - current_x :
                                      current_x - end_x;
    step_x = current_x < end_x ? 1 : -1;
    delta_y = end_y >= current_y ? current_y - end_y :
                                      end_y - current_y;
    step_y = current_y < end_y ? 1 : -1;
    error = delta_x + delta_y;

    for (;;)
    {
        const int32_t doubled_error = error * 2;

        nt35510_draw_pixel_unchecked((uint16_t)current_x,
                                      (uint16_t)current_y, color);
        if (current_x == end_x && current_y == end_y)
        {
            break;
        }
        if (doubled_error >= delta_y)
        {
            error += delta_y;
            current_x += step_x;
        }
        if (doubled_error <= delta_x)
        {
            error += delta_x;
            current_y += step_y;
        }
    }
}

/**
 * @brief  Draw the outline of a rectangle (unfilled border).
 * @param  x       Left edge in pixels.
 * @param  y       Top edge in pixels.
 * @param  width   Rectangle width in pixels.
 * @param  height  Rectangle height in pixels.
 * @param  color   RGB565 border color.
 * @note   The rectangle is clipped to the screen before drawing.
 * @return None.
 */
void BSP_NT35510_DrawRect(uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height, uint16_t color)
{
    if (!nt35510_state.ready ||
        !nt35510_clip_rectangle(x, y, &width, &height))
    {
        return;
    }

    BSP_NT35510_FillRect(x, y, width, 1U, color);
    if (height > 1U)
    {
        BSP_NT35510_FillRect(x, (uint16_t)(y + height - 1U),
                             width, 1U, color);
    }
    if (height > 2U)
    {
        BSP_NT35510_FillRect(x, (uint16_t)(y + 1U),
                             1U, (uint16_t)(height - 2U), color);
        if (width > 1U)
        {
            BSP_NT35510_FillRect((uint16_t)(x + width - 1U),
                                 (uint16_t)(y + 1U), 1U,
                                 (uint16_t)(height - 2U), color);
        }
    }
}

/**
 * @brief  Draw the outline of a circle using the midpoint algorithm.
 * @param  center_x  X coordinate of the circle center.
 * @param  center_y  Y coordinate of the circle center.
 * @param  radius    Circle radius in pixels.
 * @param  color     RGB565 color.
 * @note   Points falling outside the screen are skipped; the center must be
 *         on-screen for anything to be drawn.
 * @return None.
 */
void BSP_NT35510_DrawCircle(uint16_t center_x, uint16_t center_y,
                            uint16_t radius, uint16_t color)
{
    int32_t x = radius;
    int32_t y = 0;
    int32_t error = 1 - x;

    if (!nt35510_state.ready ||
        center_x >= nt35510_state.width ||
        center_y >= nt35510_state.height)
    {
        return;
    }

    while (x >= y)
    {
        nt35510_draw_pixel_signed((int32_t)center_x + x,
                                   (int32_t)center_y + y, color);
        nt35510_draw_pixel_signed((int32_t)center_x + y,
                                   (int32_t)center_y + x, color);
        nt35510_draw_pixel_signed((int32_t)center_x - y,
                                   (int32_t)center_y + x, color);
        nt35510_draw_pixel_signed((int32_t)center_x - x,
                                   (int32_t)center_y + y, color);
        nt35510_draw_pixel_signed((int32_t)center_x - x,
                                   (int32_t)center_y - y, color);
        nt35510_draw_pixel_signed((int32_t)center_x - y,
                                   (int32_t)center_y - x, color);
        nt35510_draw_pixel_signed((int32_t)center_x + y,
                                   (int32_t)center_y - x, color);
        nt35510_draw_pixel_signed((int32_t)center_x + x,
                                   (int32_t)center_y - y, color);

        ++y;
        if (error < 0)
        {
            error += 2 * y + 1;
        }
        else
        {
            --x;
            error += 2 * (y - x + 1);
        }
    }
}

/**
 * @brief  Draw a single printable ASCII character.
 * @param  x           Left edge of the character cell in pixels.
 * @param  y           Top edge of the character cell in pixels.
 * @param  character   ASCII character to render (' '..'~').
 * @param  font        Font size selector (12/16/24).
 * @param  foreground  RGB565 color for set glyph pixels.
 * @param  background  RGB565 color for background pixels (opaque mode only).
 * @param  transparent true to draw only foreground pixels; false to also fill
 *                      the background.
 * @note   Ignored if the character or font is unsupported or the cell would
 *         fall off-screen.
 * @return None.
 */
void BSP_NT35510_DrawChar(uint16_t x, uint16_t y, char character,
                          BSP_NT35510_Font font, uint16_t foreground,
                          uint16_t background, bool transparent)
{
    const uint8_t *glyph;
    uint8_t width;
    uint8_t height;
    uint8_t column;
    uint8_t row;

    if (!nt35510_state.ready ||
        !nt35510_get_glyph(character, font, &glyph, &width, &height) ||
        x > (uint16_t)(nt35510_state.width - width) ||
        y > (uint16_t)(nt35510_state.height - height))
    {
        return;
    }

    if (transparent)
    {
        for (column = 0U; column < width; ++column)
        {
            for (row = 0U; row < height; ++row)
            {
                if (nt35510_glyph_pixel(glyph, height, column, row))
                {
                    nt35510_draw_pixel_unchecked(
                        (uint16_t)(x + column), (uint16_t)(y + row),
                        foreground);
                }
            }
        }
        return;
    }

    nt35510_set_address_window(x, y, width, height);
    nt35510_begin_memory_write();
    for (row = 0U; row < height; ++row)
    {
        for (column = 0U; column < width; ++column)
        {
            nt35510_write_data(
                nt35510_glyph_pixel(glyph, height, column, row) ?
                foreground : background);
        }
    }
}

/**
 * @brief  Draw a text string within a bounded region, with word wrapping.
 * @param  x             Left edge of the text region in pixels.
 * @param  y             Top edge of the text region in pixels.
 * @param  region_width  Width of the text region in pixels.
 * @param  region_height Height of the text region in pixels.
 * @param  text          Null-terminated ASCII string to draw.
 * @param  font          Font size selector (12/16/24).
 * @param  foreground    RGB565 color for glyph pixels.
 * @param  background    RGB565 color for the background (opaque mode only).
 * @param  transparent   true to skip drawing the background.
 * @note   '\n' starts a new line; characters wrap at the region's right edge
 *         and rendering stops once the region's bottom is exceeded.
 *         Unsupported characters are drawn as '?'.
 * @return None.
 */
void BSP_NT35510_DrawString(uint16_t x, uint16_t y,
                            uint16_t region_width, uint16_t region_height,
                            const char *text, BSP_NT35510_Font font,
                            uint16_t foreground, uint16_t background,
                            bool transparent)
{
    const uint8_t font_height = (uint8_t)font;
    const uint8_t font_width = (uint8_t)(font_height / 2U);
    const uint32_t requested_right = (uint32_t)x + region_width;
    const uint32_t requested_bottom = (uint32_t)y + region_height;
    const uint16_t right =
        (uint16_t)(requested_right < nt35510_state.width ?
                   requested_right : nt35510_state.width);
    const uint16_t bottom =
        (uint16_t)(requested_bottom < nt35510_state.height ?
                   requested_bottom : nt35510_state.height);
    const uint32_t origin_x = x;
    uint32_t cursor_x = x;
    uint32_t cursor_y = y;

    if (!nt35510_state.ready || text == NULL ||
        region_width == 0U || region_height == 0U ||
        (font != BSP_NT35510_FONT_12 &&
         font != BSP_NT35510_FONT_16 &&
         font != BSP_NT35510_FONT_24) ||
        x >= nt35510_state.width || y >= nt35510_state.height)
    {
        return;
    }

    while (*text != '\0')
    {
        char character = *text++;

        if (character == '\n')
        {
            cursor_x = origin_x;
            cursor_y += font_height;
            continue;
        }
        if ((uint8_t)character < (uint8_t)' ' ||
            (uint8_t)character > (uint8_t)'~')
        {
            character = '?';
        }
        if ((uint32_t)cursor_x + font_width > right)
        {
            cursor_x = origin_x;
            cursor_y += font_height;
        }
        if ((uint32_t)cursor_y + font_height > bottom)
        {
            break;
        }

        BSP_NT35510_DrawChar((uint16_t)cursor_x, (uint16_t)cursor_y,
                             character, font,
                             foreground, background, transparent);
        cursor_x += font_width;
    }
}

/**
 * @brief  Draw an unsigned integer as a fixed-width decimal string.
 * @param  x            Left edge of the number in pixels.
 * @param  y            Top edge of the number in pixels.
 * @param  value        Unsigned value to render.
 * @param  digits       Number of digit cells to draw (1..10).
 * @param  font         Font size selector (12/16/24).
 * @param  foreground   RGB565 color for glyph pixels.
 * @param  background   RGB565 color for the background (drawn opaque).
 * @param  leading_zero true to pad with '0', false to pad with spaces.
 * @note   Rendering stops early if the next digit would fall off-screen.
 * @return None.
 */
void BSP_NT35510_DrawUInt(uint16_t x, uint16_t y, uint32_t value,
                          uint8_t digits, BSP_NT35510_Font font,
                          uint16_t foreground, uint16_t background,
                          bool leading_zero)
{
    const uint8_t font_width = (uint8_t)((uint8_t)font / 2U);
    uint32_t divisor = 1U;
    bool started = false;
    uint8_t index;

    if (!nt35510_state.ready || digits == 0U || digits > 10U ||
        (font != BSP_NT35510_FONT_12 &&
         font != BSP_NT35510_FONT_16 &&
         font != BSP_NT35510_FONT_24))
    {
        return;
    }

    for (index = 1U; index < digits; ++index)
    {
        divisor *= 10U;
    }

    for (index = 0U; index < digits; ++index)
    {
        const uint8_t digit = (uint8_t)((value / divisor) % 10U);
        const uint32_t character_x =
            (uint32_t)x + (uint32_t)index * font_width;
        char character;

        if (digit != 0U || index == (uint8_t)(digits - 1U))
        {
            started = true;
        }

        character = (started || leading_zero) ?
                    (char)('0' + digit) : ' ';
        if (character_x + font_width > nt35510_state.width)
        {
            break;
        }

        BSP_NT35510_DrawChar(
            (uint16_t)character_x, y,
            character, font, foreground, background, false);

        if (divisor > 1U)
        {
            divisor /= 10U;
        }
    }
}
