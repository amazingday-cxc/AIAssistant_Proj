/**
 * @file gui_app.h
 * @brief Application-level LVGL entry points for AIAssistant_Proj.
 */

#ifndef GUI_APP_H
#define GUI_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise LVGL and the display porting layer, then build the first screen.
 * Call once, from the GUI task, before the lv_timer_handler loop.
 */
void gui_app_init(void);

/**
 * Service LVGL timers and input/display processing once.
 * Call periodically and only from the GUI task after gui_app_init().
 *
 * @return Recommended delay in milliseconds before the next call. The return
 *         value is bounded so the GUI task continues to service LVGL often.
 */
uint32_t gui_app_process(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_APP_H */
