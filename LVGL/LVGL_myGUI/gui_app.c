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
 *    internally thread-safe, so gui_app_init(), gui_app_process(), and any
 *    other lv_* calls must all run from the GUI task in freertos.c.
 *  * The GT1151 touch driver is registered through lv_port_indev_init(), so
 *    clicks, slider drags, and tile gestures are handled by LVGL's pointer
 *    input pipeline.
 */

#include "gui_app.h"

#include "bsp_nt35510.h"
#include "device_cloud.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#define GUI_APP_MAX_PROCESS_DELAY_MS 20U

/*=========================
 *  DEMO SCREEN
 *=========================*/

static lv_obj_t *tileview;
static lv_obj_t *page1;
static lv_obj_t *page2;
static lv_obj_t *page3;

/* Page1 modal */
static lv_obj_t *modal_overlay;
static lv_obj_t *volume_panel;
static lv_obj_t *volume_value_label;
static lv_obj_t *light_panel;
static lv_obj_t *light_state_btn;
static lv_obj_t *light_state_label;
static lv_obj_t *active_menu_button;
static bool light_is_on;

/* Page2 – Notepad */
static lv_obj_t *notepad_overlay;
static lv_obj_t *notepad_panel;
static lv_obj_t *notepad_textarea;
static lv_obj_t *notepad_keyboard;
static lv_obj_t *active_notepad_button;

/* Page3 – Settings / Wi-Fi */
static lv_obj_t *settings_overlay;
static lv_obj_t *wifi_settings_panel;
static lv_obj_t *active_settings_button;

/*=========================
 *  PAGE 2 – NOTEPAD
 *=========================*/

static void notepad_close_event_cb(lv_event_t *event)
{
    (void)event;

    lv_obj_add_flag(notepad_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notepad_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notepad_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    if (active_notepad_button != NULL) {
        lv_obj_clear_state(active_notepad_button, LV_STATE_DISABLED);
        active_notepad_button = NULL;
    }
}

static void notepad_menu_event_cb(lv_event_t *event)
{
    active_notepad_button = lv_event_get_target(event);
    lv_obj_add_state(active_notepad_button, LV_STATE_DISABLED);
    lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(notepad_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(notepad_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(notepad_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notepad_overlay);

    /* Focus textarea so keyboard sends characters to it */
    lv_event_send(notepad_textarea, LV_EVENT_FOCUSED, NULL);
    lv_keyboard_set_textarea(notepad_keyboard, notepad_textarea);
}

/*=========================
 *  PAGE 3 – SETTINGS / WI-FI
 *=========================*/

static void settings_close_event_cb(lv_event_t *event)
{
    (void)event;

    lv_obj_add_flag(wifi_settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    if (active_settings_button != NULL) {
        lv_obj_clear_state(active_settings_button, LV_STATE_DISABLED);
        active_settings_button = NULL;
    }
}

static void settings_menu_event_cb(lv_event_t *event)
{
    active_settings_button = lv_event_get_target(event);
    lv_obj_add_state(active_settings_button, LV_STATE_DISABLED);
    lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(settings_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings_overlay);
}

/*=========================
 *  PAGE 1 – ORIGINAL CALLBACKS
 *=========================*/

static void volume_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    lv_label_set_text_fmt(volume_value_label, "%d%%", (int)lv_slider_get_value(slider));
}

static void volume_slider_released_event_cb(lv_event_t *event)
{
    const lv_obj_t *slider = lv_event_get_target(event);
    (void)DeviceCloud_ReportVolume((uint8_t)lv_slider_get_value(slider));
}

static void open_modal_panel(lv_obj_t *panel, lv_obj_t *menu_button)
{
    lv_obj_add_flag(volume_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(light_panel, LV_OBJ_FLAG_HIDDEN);

    if (active_menu_button != NULL) {
        lv_obj_clear_state(active_menu_button, LV_STATE_DISABLED);
    }

    active_menu_button = menu_button;
    lv_obj_add_state(active_menu_button, LV_STATE_DISABLED);
    lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(modal_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal_overlay);
}

static void close_modal_event_cb(lv_event_t *event)
{
    (void)event;

    lv_obj_add_flag(volume_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(light_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);

    if (active_menu_button != NULL) {
        lv_obj_clear_state(active_menu_button, LV_STATE_DISABLED);
        active_menu_button = NULL;
    }
}

static void volume_menu_event_cb(lv_event_t *event)
{
    open_modal_panel(volume_panel, lv_event_get_target(event));
}

static void update_light_state_button(void)
{
    if (light_is_on) {
        lv_label_set_text(light_state_label, "LIGHT: ON");
        lv_obj_set_style_bg_color(light_state_btn, lv_color_hex(0x41C77A),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(light_state_label, "LIGHT: OFF");
        lv_obj_set_style_bg_color(light_state_btn, lv_color_hex(0x697386),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void light_state_event_cb(lv_event_t *event)
{
    (void)event;

    light_is_on = !light_is_on;
    update_light_state_button();
    (void)DeviceCloud_ReportLight(light_is_on);
}

static void light_menu_event_cb(lv_event_t *event)
{
    update_light_state_button();
    open_modal_panel(light_panel, lv_event_get_target(event));
}

static lv_obj_t *create_popup_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 440, 220);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 50);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x263B5F), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x5CE7AF), 0);
    lv_obj_set_style_shadow_width(panel, 24, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

static void add_popup_close_button(lv_obj_t *panel)
{
    lv_obj_t *close_btn = lv_btn_create(panel);
    lv_obj_set_size(close_btn, 52, 52);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -6, -6);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xD95757),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(close_btn, close_modal_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_24, 0);
    lv_obj_center(close_label);
}

static void gui_main_menu(void)
{
    tileview = lv_tileview_create(lv_scr_act());

    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_center(tileview);

    /* Three horizontally arranged pages; page 1 contains the main controls. */
    page1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    page2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT|LV_DIR_RIGHT);
    page3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

    lv_obj_set_style_bg_color(page1, lv_color_hex(0x1A2B4A), 0);
    lv_obj_set_style_bg_opa(page1, LV_OPA_COVER, 0);

    lv_obj_set_style_bg_color(page2, lv_color_hex(0x1A2B4A), 0);
    lv_obj_set_style_bg_opa(page2, LV_OPA_COVER, 0);

    lv_obj_set_style_bg_color(page3, lv_color_hex(0x1A2B4A), 0);
    lv_obj_set_style_bg_opa(page3, LV_OPA_COVER, 0);


    lv_obj_t *scr1_title = lv_label_create(page1);
    lv_label_set_text(scr1_title, "Smart Assistant");
    lv_obj_set_style_text_align(scr1_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr1_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(scr1_title, &lv_font_montserrat_36, 0);
    lv_obj_align(scr1_title, LV_ALIGN_CENTER, 0, -200);

    lv_obj_t *scr2_title = lv_label_create(page2);
    lv_label_set_text(scr2_title, "Smart Assistant");
    lv_obj_set_style_text_align(scr2_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr2_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(scr2_title, &lv_font_montserrat_36, 0);
    lv_obj_align(scr2_title, LV_ALIGN_CENTER, 0, -200);

    lv_obj_t *scr3_title = lv_label_create(page3);
    lv_label_set_text(scr3_title, "Smart Assistant");
    lv_obj_set_style_text_align(scr3_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr3_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(scr3_title, &lv_font_montserrat_36, 0);
    lv_obj_align(scr3_title, LV_ALIGN_CENTER, 0, -200);


    lv_obj_t *scr1_page = lv_label_create(page1);
    lv_label_set_text(scr1_page, "PAGE 1/3");
    lv_obj_set_style_text_align(scr1_page, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr1_page, lv_color_hex(0x87868E), 0);
    lv_obj_set_style_text_font(scr1_page, &lv_font_montserrat_24, 0);
    lv_obj_align(scr1_page, LV_ALIGN_CENTER, -300, -200);

    lv_obj_t *scr2_page = lv_label_create(page2);
    lv_label_set_text(scr2_page, "PAGE 2/3");
    lv_obj_set_style_text_align(scr2_page, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr2_page, lv_color_hex(0x87868E), 0);
    lv_obj_set_style_text_font(scr2_page, &lv_font_montserrat_24, 0);
    lv_obj_align(scr2_page, LV_ALIGN_CENTER, -300, -200);

    lv_obj_t *scr3_page = lv_label_create(page3);
    lv_label_set_text(scr3_page, "PAGE 3/3");
    lv_obj_set_style_text_align(scr3_page, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scr3_page, lv_color_hex(0x87868E), 0);
    lv_obj_set_style_text_font(scr3_page, &lv_font_montserrat_24, 0);
    lv_obj_align(scr3_page, LV_ALIGN_CENTER, -300, -200);

    /* ── Main control buttons ── */
    lv_obj_t *btn_volume = lv_btn_create(page1);
    lv_obj_set_size(btn_volume, 280, 160);
    lv_obj_set_style_bg_color(btn_volume, lv_color_hex(0x5CE7AF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_volume, LV_ALIGN_CENTER, -160, 120);

    lv_obj_t *btn_LED = lv_btn_create(page1);
    lv_obj_set_size(btn_LED, 280, 160);
    lv_obj_set_style_bg_color(btn_LED, lv_color_hex(0xE7C15C), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_LED, LV_ALIGN_CENTER, -160, -80);

    lv_obj_t *btn_battery = lv_btn_create(page1);
    lv_obj_set_size(btn_battery, 280, 160);
    lv_obj_set_style_bg_color(btn_battery, lv_color_hex(0xC25CE7), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_battery, LV_ALIGN_CENTER, 160, -80);

    lv_obj_t *btn_wifi = lv_btn_create(page1);
    lv_obj_set_size(btn_wifi, 280, 160);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0xD24A1F), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_wifi, LV_ALIGN_CENTER, 160, 120);


    lv_obj_t *volume_logo = lv_label_create(btn_volume);
    lv_label_set_text(volume_logo, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(volume_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(volume_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(volume_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *LED_logo = lv_label_create(btn_LED);
    lv_label_set_text(LED_logo, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(LED_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(LED_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(LED_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *battery_logo = lv_label_create(btn_battery);
    lv_label_set_text(battery_logo, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(battery_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(battery_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(battery_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *wifi_logo = lv_label_create(btn_wifi);
    lv_label_set_text(wifi_logo, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(wifi_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(wifi_logo, LV_ALIGN_CENTER, 0, -20);


    lv_obj_t *btn_battery_label = lv_label_create(btn_battery);
    lv_label_set_text(btn_battery_label, "BATTERY");
    lv_obj_set_style_text_color(btn_battery_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_battery_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_battery_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *btn_volume_label = lv_label_create(btn_volume);
    lv_label_set_text(btn_volume_label, "VOLUME");
    lv_obj_set_style_text_color(btn_volume_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_volume_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_volume_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *btn_LED_label = lv_label_create(btn_LED);
    lv_label_set_text(btn_LED_label, "LIGHT SWITCH");
    lv_obj_set_style_text_color(btn_LED_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_LED_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_LED_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *btn_wifi_label = lv_label_create(btn_wifi);
    lv_label_set_text(btn_wifi_label, "WIFI");
    lv_obj_set_style_text_color(btn_wifi_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_wifi_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_wifi_label, LV_ALIGN_CENTER, 0, 30);

    modal_overlay = lv_obj_create(page1);
    lv_obj_set_size(modal_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(modal_overlay);
    lv_obj_clear_flag(modal_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(modal_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(modal_overlay, 0, 0);
    lv_obj_set_style_border_width(modal_overlay, 0, 0);
    lv_obj_set_style_pad_all(modal_overlay, 0, 0);
    lv_obj_set_style_bg_color(modal_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_overlay, LV_OPA_10, 0);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_HIDDEN);

    volume_panel = create_popup_panel(modal_overlay);
    add_popup_close_button(volume_panel);

    lv_obj_t *volume_title = lv_label_create(volume_panel);
    lv_label_set_text(volume_title, LV_SYMBOL_AUDIO "  VOLUME");
    lv_obj_set_style_text_color(volume_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(volume_title, &lv_font_montserrat_24, 0);
    lv_obj_align(volume_title, LV_ALIGN_TOP_LEFT, 10, 10);

    volume_value_label = lv_label_create(volume_panel);
    lv_label_set_text(volume_value_label, "60%");
    lv_obj_set_style_text_color(volume_value_label, lv_color_hex(0x5CE7AF), 0);
    lv_obj_set_style_text_font(volume_value_label, &lv_font_montserrat_24, 0);
    lv_obj_align(volume_value_label, LV_ALIGN_TOP_RIGHT, -75, 10);

    lv_obj_t *volume_slider = lv_slider_create(volume_panel);
    lv_obj_set_size(volume_slider, 360, 28);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, 50, LV_ANIM_OFF);
    lv_obj_align(volume_slider, LV_ALIGN_CENTER, 0, 30);
    lv_obj_clear_flag(volume_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0x5CE7AF),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0xFFFFFF),
                              LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(volume_slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_slider_released_event_cb, LV_EVENT_RELEASED, NULL);

    light_panel = create_popup_panel(modal_overlay);
    add_popup_close_button(light_panel);
    lv_obj_set_style_border_color(light_panel, lv_color_hex(0xE7C15C), 0);

    lv_obj_t *light_title = lv_label_create(light_panel);
    lv_label_set_text(light_title, LV_SYMBOL_CHARGE "  LIGHT SWITCH");
    lv_obj_set_style_text_color(light_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(light_title, &lv_font_montserrat_24, 0);
    lv_obj_align(light_title, LV_ALIGN_TOP_MID, 0, 10);

    light_state_btn = lv_btn_create(light_panel);
    lv_obj_set_size(light_state_btn, 250, 85);
    lv_obj_align(light_state_btn, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(light_state_btn, light_state_event_cb, LV_EVENT_CLICKED, NULL);

    light_state_label = lv_label_create(light_state_btn);
    lv_obj_set_style_text_color(light_state_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(light_state_label, &lv_font_montserrat_24, 0);
    lv_obj_center(light_state_label);
    update_light_state_button();

    lv_obj_add_event_cb(btn_volume, volume_menu_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_LED, light_menu_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── Page2: Notepad button ── */
    lv_obj_t *btn_notepad = lv_btn_create(page2);
    lv_obj_set_size(btn_notepad, 280, 160);
    lv_obj_set_style_bg_color(btn_notepad, lv_color_hex(0x4A90D9), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_notepad, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *notepad_btn_logo = lv_label_create(btn_notepad);
    lv_label_set_text(notepad_btn_logo, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_color(notepad_btn_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(notepad_btn_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(notepad_btn_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *btn_notepad_label = lv_label_create(btn_notepad);
    lv_label_set_text(btn_notepad_label, "NOTEPAD");
    lv_obj_set_style_text_color(btn_notepad_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_notepad_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_notepad_label, LV_ALIGN_CENTER, 0, 30);

    /* Notepad overlay (child of page2 so it never covers page1/page3) */
    notepad_overlay = lv_obj_create(page2);
    lv_obj_set_size(notepad_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(notepad_overlay);
    lv_obj_clear_flag(notepad_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(notepad_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(notepad_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(notepad_overlay, 0, 0);
    lv_obj_set_style_border_width(notepad_overlay, 0, 0);
    lv_obj_set_style_pad_all(notepad_overlay, 0, 0);
    lv_obj_set_style_bg_color(notepad_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(notepad_overlay, LV_OPA_10, 0);
    lv_obj_add_flag(notepad_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Popup panel holding the text area */
    notepad_panel = lv_obj_create(notepad_overlay);
    lv_obj_set_size(notepad_panel, 760, 190);
    lv_obj_align(notepad_panel, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(notepad_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(notepad_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(notepad_panel, 20, 0);
    lv_obj_set_style_bg_color(notepad_panel, lv_color_hex(0x263B5F), 0);
    lv_obj_set_style_bg_opa(notepad_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notepad_panel, 2, 0);
    lv_obj_set_style_border_color(notepad_panel, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_shadow_width(notepad_panel, 24, 0);
    lv_obj_set_style_shadow_opa(notepad_panel, LV_OPA_40, 0);

    lv_obj_t *notepad_close_btn = lv_btn_create(notepad_panel);
    lv_obj_set_size(notepad_close_btn, 52, 52);
    lv_obj_align(notepad_close_btn, LV_ALIGN_TOP_RIGHT, -6, -6);
    lv_obj_clear_flag(notepad_close_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(notepad_close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(notepad_close_btn, lv_color_hex(0xD95757),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(notepad_close_btn, notepad_close_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *notepad_close_label = lv_label_create(notepad_close_btn);
    lv_label_set_text(notepad_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(notepad_close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(notepad_close_label, &lv_font_montserrat_24, 0);
    lv_obj_center(notepad_close_label);

    lv_obj_t *notepad_title = lv_label_create(notepad_panel);
    lv_label_set_text(notepad_title, LV_SYMBOL_EDIT "  NOTEPAD");
    lv_obj_set_style_text_color(notepad_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(notepad_title, &lv_font_montserrat_24, 0);
    lv_obj_align(notepad_title, LV_ALIGN_TOP_LEFT, 10, 10);

    notepad_textarea = lv_textarea_create(notepad_panel);
    lv_obj_set_size(notepad_textarea, 720, 110);
    lv_obj_align(notepad_textarea, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(notepad_textarea, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_textarea_set_placeholder_text(notepad_textarea, "Type your note here...");
    lv_textarea_set_max_length(notepad_textarea, 256);
    lv_textarea_set_one_line(notepad_textarea, false);

    /* Virtual keyboard, bound to the notepad text area */
    notepad_keyboard = lv_keyboard_create(notepad_overlay);
    lv_obj_set_size(notepad_keyboard, LV_PCT(100), 240);
    lv_obj_align(notepad_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(notepad_keyboard, notepad_textarea);
    lv_obj_add_flag(notepad_keyboard, LV_OBJ_FLAG_HIDDEN);
    /* Keyboard's own built-in close ("x") key also dismisses the notepad popup */
    lv_obj_add_event_cb(notepad_keyboard, notepad_close_event_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_event_cb(btn_notepad, notepad_menu_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── Page3: Settings button (Wi-Fi only) ── */
    lv_obj_t *btn_settings = lv_btn_create(page3);
    lv_obj_set_size(btn_settings, 280, 160);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x697386), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(btn_settings, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *settings_btn_logo = lv_label_create(btn_settings);
    lv_label_set_text(settings_btn_logo, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_btn_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(settings_btn_logo, &lv_font_montserrat_48, 0);
    lv_obj_align(settings_btn_logo, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *btn_settings_label = lv_label_create(btn_settings);
    lv_label_set_text(btn_settings_label, "SETTINGS");
    lv_obj_set_style_text_color(btn_settings_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_settings_label, &lv_font_montserrat_24, 0);
    lv_obj_align(btn_settings_label, LV_ALIGN_CENTER, 0, 30);

    /* Settings overlay (child of page3) */
    settings_overlay = lv_obj_create(page3);
    lv_obj_set_size(settings_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(settings_overlay);
    lv_obj_clear_flag(settings_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(settings_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(settings_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(settings_overlay, 0, 0);
    lv_obj_set_style_border_width(settings_overlay, 0, 0);
    lv_obj_set_style_pad_all(settings_overlay, 0, 0);
    lv_obj_set_style_bg_color(settings_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(settings_overlay, LV_OPA_10, 0);
    lv_obj_add_flag(settings_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Settings popup panel — deliberately exposes a single item: Wi-Fi */
    wifi_settings_panel = create_popup_panel(settings_overlay);
    lv_obj_set_style_border_color(wifi_settings_panel, lv_color_hex(0x4A90D9), 0);

    lv_obj_t *settings_close_btn = lv_btn_create(wifi_settings_panel);
    lv_obj_set_size(settings_close_btn, 52, 52);
    lv_obj_align(settings_close_btn, LV_ALIGN_TOP_RIGHT, -6, -6);
    lv_obj_clear_flag(settings_close_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(settings_close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settings_close_btn, lv_color_hex(0xD95757),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(settings_close_btn, settings_close_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_close_label = lv_label_create(settings_close_btn);
    lv_label_set_text(settings_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(settings_close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(settings_close_label, &lv_font_montserrat_24, 0);
    lv_obj_center(settings_close_label);

    lv_obj_t *settings_title = lv_label_create(wifi_settings_panel);
    lv_label_set_text(settings_title, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_24, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_LEFT, 10, 10);

    /* Only one row is exposed in Settings: Wi-Fi */
    lv_obj_t *wifi_row = lv_obj_create(wifi_settings_panel);
    lv_obj_set_size(wifi_row, 380, 70);
    lv_obj_align(wifi_row, LV_ALIGN_CENTER, 0, 30);
    lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(wifi_row, 12, 0);
    lv_obj_set_style_bg_color(wifi_row, lv_color_hex(0x1A2B4A), 0);
    lv_obj_set_style_bg_opa(wifi_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_row, 0, 0);

    lv_obj_t *wifi_row_icon = lv_label_create(wifi_row);
    lv_label_set_text(wifi_row_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_row_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(wifi_row_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_row_icon, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *wifi_row_label = lv_label_create(wifi_row);
    lv_label_set_text(wifi_row_label, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_row_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(wifi_row_label, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_row_label, LV_ALIGN_LEFT_MID, 56, 0);

    lv_obj_t *wifi_switch = lv_switch_create(wifi_row);
    lv_obj_align(wifi_switch, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);

    lv_obj_add_event_cb(btn_settings, settings_menu_event_cb, LV_EVENT_CLICKED, NULL);
}

/*=========================
 *  PUBLIC API
 *=========================*/

void gui_app_init(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    BSP_NT35510_SetOrientation(1);
    gui_main_menu();
}

uint32_t gui_app_process(void)
{
    uint32_t idle = lv_timer_handler();

    if (idle > GUI_APP_MAX_PROCESS_DELAY_MS) {
        idle = GUI_APP_MAX_PROCESS_DELAY_MS;
    }

    return (idle > 0U) ? idle : 1U;
}
