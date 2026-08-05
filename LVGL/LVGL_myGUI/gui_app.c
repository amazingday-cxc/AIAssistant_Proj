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

/* Apply a consistent drop-shadow to a button object. */
static void apply_btn_shadow(lv_obj_t *btn)
{
    lv_obj_set_style_shadow_width(btn,  20,                  0);
    lv_obj_set_style_shadow_spread(btn,  3,                  0);
    lv_obj_set_style_shadow_ofs_x(btn,   5,                  0);
    lv_obj_set_style_shadow_ofs_y(btn,   8,                  0);
    lv_obj_set_style_shadow_color(btn,  lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn,    LV_OPA_50,           0);
}

static void gui_main_menu(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* ── Background: ink blue (墨蓝) ── */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A2B4A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);


    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "LVGL+MQTT Demo ");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -200);


    /* ── Three buttons with drop-shadow ── */
    lv_obj_t *btn_volumn = lv_btn_create(scr);
    lv_obj_set_size(btn_volumn, 280, 160);
    lv_obj_set_style_bg_color(btn_volumn, lv_color_hex(0x5CE7AF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_volumn, LV_ALIGN_CENTER, -160, 120);
    apply_btn_shadow(btn_volumn);

    lv_obj_t *btn_LED = lv_btn_create(scr);
    lv_obj_set_size(btn_LED, 280, 160);
    lv_obj_set_style_bg_color(btn_LED, lv_color_hex(0xE7C15C), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_LED, LV_ALIGN_CENTER, -160, -80);
    apply_btn_shadow(btn_LED);

    lv_obj_t *btn_battery = lv_btn_create(scr);
    lv_obj_set_size(btn_battery, 280, 360);
    lv_obj_set_style_bg_color(btn_battery, lv_color_hex(0xC25CE7), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_battery, LV_ALIGN_CENTER, 160, 20);
    apply_btn_shadow(btn_battery);


    lv_obj_t *volumn_logo = lv_label_create(btn_volumn);
    lv_label_set_text(volumn_logo, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(volumn_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(volumn_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(volumn_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *battery_logo = lv_label_create(btn_battery);
    lv_label_set_text(battery_logo, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(battery_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(battery_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(battery_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *LED_logo = lv_label_create(btn_LED);
    lv_label_set_text(LED_logo, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(LED_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(LED_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(LED_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *btn_battery_label = lv_label_create(btn_battery);
    lv_label_set_text(btn_battery_label, "BATTERY");
    lv_obj_set_style_text_color(btn_battery_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_battery_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_battery_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *btn_volumn_label = lv_label_create(btn_volumn);
    lv_label_set_text(btn_volumn_label, "VOLUMN");
    lv_obj_set_style_text_color(btn_volumn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_volumn_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_volumn_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *btn_LED_label = lv_label_create(btn_LED);
    lv_label_set_text(btn_LED_label, "LIGHT SWITCH");
    lv_obj_set_style_text_color(btn_LED_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_LED_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_LED_label, LV_ALIGN_CENTER, 0, 30);
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
    gui_main_menu();
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
