/**
 * @file gui_app.h
 * @brief Application-level LVGL entry points for AIAssistant_Proj.
 */

#ifndef GUI_APP_H
#define GUI_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise LVGL and the display porting layer, then build the first screen.
 * Call once, from the GUI task, before the lv_timer_handler loop.
 */
void gui_app_init(void);

/**
 * FreeRTOS task body: runs gui_app_init() then periodically services
 * lv_timer_handler(). Hand this straight to osThreadNew().
 * @param argument unused (CMSIS-RTOS v2 signature).
 */
void gui_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* GUI_APP_H */
