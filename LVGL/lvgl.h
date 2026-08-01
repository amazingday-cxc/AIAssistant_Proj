/**
 * @file lvgl.h
 * Master aggregator header for this project.
 *
 * WHY THIS FILE EXISTS (project-specific adjustment)
 * --------------------------------------------------
 * The LVGL source tree in this project has been reorganised into
 *   LVGL/LVGL_conf   (lv_conf.h + original aggregator)
 *   LVGL/LVGL_src    (the upstream "src/" tree: core, draw, font, ...)
 *   LVGL/LVGL_porting
 *   LVGL/LVGL_myGUI
 *
 * Every LVGL .c/.h file includes the master header with a *hard coded*
 * relative path such as `#include "../../lvgl.h"` (from LVGL_src/core)
 * or `#include "../../../lvgl.h"` (from LVGL_src/extra subtrees). Those paths
 * always resolve to <LVGL/lvgl.h>, so a master header MUST live here at the
 * root of the LVGL folder. The original aggregator that ships in
 * LVGL_conf/lvgl.h pointed at a `src/` directory, but the sources were
 * renamed to `LVGL_src`, so the include paths below are adjusted to match.
 */

#ifndef LVGL_H
#define LVGL_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************
 * CURRENT VERSION OF LVGL
 ***************************/
#define LVGL_VERSION_MAJOR 8
#define LVGL_VERSION_MINOR 3
#define LVGL_VERSION_PATCH 11
#define LVGL_VERSION_INFO ""

/*********************
 *      INCLUDES
 *********************/

#include "LVGL_src/misc/lv_log.h"
#include "LVGL_src/misc/lv_timer.h"
#include "LVGL_src/misc/lv_math.h"
#include "LVGL_src/misc/lv_mem.h"
#include "LVGL_src/misc/lv_async.h"
#include "LVGL_src/misc/lv_anim_timeline.h"
#include "LVGL_src/misc/lv_printf.h"

#include "LVGL_src/hal/lv_hal.h"

#include "LVGL_src/core/lv_obj.h"
#include "LVGL_src/core/lv_group.h"
#include "LVGL_src/core/lv_indev.h"
#include "LVGL_src/core/lv_refr.h"
#include "LVGL_src/core/lv_disp.h"
#include "LVGL_src/core/lv_theme.h"

#include "LVGL_src/font/lv_font.h"
#include "LVGL_src/font/lv_font_loader.h"
#include "LVGL_src/font/lv_font_fmt_txt.h"

#include "LVGL_src/widgets/lv_arc.h"
#include "LVGL_src/widgets/lv_btn.h"
#include "LVGL_src/widgets/lv_img.h"
#include "LVGL_src/widgets/lv_label.h"
#include "LVGL_src/widgets/lv_line.h"
#include "LVGL_src/widgets/lv_table.h"
#include "LVGL_src/widgets/lv_checkbox.h"
#include "LVGL_src/widgets/lv_bar.h"
#include "LVGL_src/widgets/lv_slider.h"
#include "LVGL_src/widgets/lv_btnmatrix.h"
#include "LVGL_src/widgets/lv_dropdown.h"
#include "LVGL_src/widgets/lv_roller.h"
#include "LVGL_src/widgets/lv_textarea.h"
#include "LVGL_src/widgets/lv_canvas.h"
#include "LVGL_src/widgets/lv_switch.h"

#include "LVGL_src/draw/lv_draw.h"

#include "LVGL_src/lv_api_map.h"

/*-----------------
 * EXTRAS
 *----------------*/
#include "LVGL_src/extra/lv_extra.h"

/**********************
 *      MACROS
 **********************/

/** Gives 1 if the x.y.z version is supported in the current version */
#define LV_VERSION_CHECK(x,y,z) (x == LVGL_VERSION_MAJOR && (y < LVGL_VERSION_MINOR || (y == LVGL_VERSION_MINOR && z <= LVGL_VERSION_PATCH)))

static inline int lv_version_major(void)
{
    return LVGL_VERSION_MAJOR;
}

static inline int lv_version_minor(void)
{
    return LVGL_VERSION_MINOR;
}

static inline int lv_version_patch(void)
{
    return LVGL_VERSION_PATCH;
}

static inline const char *lv_version_info(void)
{
    return LVGL_VERSION_INFO;
}

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_H*/
