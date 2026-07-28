#ifndef JC3636K718_APP_UI_H
#define JC3636K718_APP_UI_H

// Call after display/input, board hardware, and media initialization. A boot
// overlay may remain on the active LVGL screen and fade out after this returns.
void app_ui_begin();

// Call from the same task that runs lv_timer_handler(). The function only reads
// hardware snapshots; no worker task or hardware callback touches LVGL.
void app_ui_poll();

#endif
