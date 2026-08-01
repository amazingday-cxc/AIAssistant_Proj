/**
 * @file gui_app.c
 * @brief LVGL bring-up and a small demo screen for AIAssistant_Proj.
 *
 * INTEGRATION NOTES (project-specific)
 * ------------------------------------
 *  * lv_conf.h sets LV_TICK_CUSTOM = 1 with LV_TICK_CUSTOM_SYS_TIME_EXPR =
 *    HAL_GetTick(). That means LVGL reads time straight from the HAL SysTick,
 *    so we do NOT need to call lv_tick_inc() ourselves - one less thing to
 *    wire into an interrupt.
 *  * This project already runs FreeRTOS (CMSIS-RTOS v2). LVGL v8 is not
 *    internally thread-safe, so all LVGL calls must happen from a single
 *    task. gui_task() below is that task; keep every lv_* call in here.
 *  * The input-device porting file (lv_port_indev.c) is currently disabled
 *    (its top guard is `#if 0`), so we only bring up the display here. Add
 *    lv_port_indev_init() once a touch/keypad driver exists.
 */

#include "gui_app.h"

#include "bsp_nt35510.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "cmsis_os.h"

/*=========================
 *  DEMO SCREEN
 *=========================*/

static void gui_build_demo(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "LVGL on STM32F407\nAIAssistant_Proj");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

    /* A button to prove widgets + theme are alive. */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 140, 44);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Ready");
    lv_obj_center(btn_label);
}

/*=========================
 *  PUBLIC API
 *=========================*/

void gui_app_init(void)
{
    lv_init();
    lv_port_disp_init();
    /* lv_port_indev_init(); // enable once an input driver is available */
    BSP_NT35510_SetOrientation(1);
    gui_build_demo();
}

void guiTask_Func(void *argument)
{
    (void)argument;

    gui_app_init();

    for (;;) {
        /* Returns the time until the next timer is due; cap it so the task
         * still yields regularly to the rest of the system. */
        uint32_t idle = lv_timer_handler();
        if (idle > 20U) {
            idle = 20U;
        }
        osDelay(idle ? idle : 1U);
    }
}
