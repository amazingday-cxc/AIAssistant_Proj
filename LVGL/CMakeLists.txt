# ==========================================================================
#  LVGL integration for AIAssistant_Proj (STM32F407ZG, CMake + GCC-ARM)
# --------------------------------------------------------------------------
#  Project-specific notes:
#   * The upstream "src/" tree was renamed to "LVGL_src" and the config /
#     porting / user-GUI files live in sibling folders. This module globs
#     LVGL_src recursively (LVGL v8.x has no build-breaking optional TUs;
#     every source is gated internally by lv_conf.h #if blocks), so unused
#     widgets/decoders compile to nothing and cost no flash.
#   * lv_conf.h is picked up automatically: LVGL_conf is put on the include
#     path and lv_conf_internal.h's __has_include("lv_conf.h") enables
#     LV_CONF_INCLUDE_SIMPLE for us.
#   * Built as an OBJECT library and linked against the CubeMX INTERFACE
#     target "stm32cubemx" so LVGL sees the same defines / HAL headers
#     (needed because lv_conf.h uses LV_TICK_CUSTOM -> HAL_GetTick()).
# ==========================================================================

# LVGL core / draw / font / widgets / extra ...
file(GLOB_RECURSE LVGL_SRC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_src/*.c
)

# Porting layer (display / input / filesystem glue)
file(GLOB LVGL_PORTING_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_porting/*.c
)

# User GUI screens / logic
file(GLOB_RECURSE LVGL_MYGUI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_myGUI/*.c
)

add_library(lvgl OBJECT
    ${LVGL_SRC_SOURCES}
    ${LVGL_PORTING_SOURCES}
    ${LVGL_MYGUI_SOURCES}
)

target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}            # <lvgl.h> master aggregator
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_conf  # <lv_conf.h>
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_src   # internal cross-includes
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_porting
    ${CMAKE_CURRENT_SOURCE_DIR}/LVGL_myGUI
    ${CMAKE_CURRENT_SOURCE_DIR}/../App/Inc       # device/cloud application API
    ${CMAKE_CURRENT_SOURCE_DIR}/../Firmware/inc  # BSP drivers (NT35510, etc.)
)

# Share CubeMX includes/defines (HAL headers for LV_TICK_CUSTOM, etc.)
target_link_libraries(lvgl PUBLIC stm32cubemx)
