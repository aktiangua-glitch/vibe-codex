#ifndef VELA_DIAL_AI_APP_UI_H
#define VELA_DIAL_AI_APP_UI_H

// Creates the native 360 x 360 LVGL interface. Call from the same task that
// owns lv_timer_handler().
void app_ui_begin();

// Advances the rotary dwell state machine and reads hardware snapshots.
// No worker task is allowed to touch LVGL objects directly.
void app_ui_poll();

#endif
