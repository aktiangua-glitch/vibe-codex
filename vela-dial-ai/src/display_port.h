#ifndef JC3636K718_DISPLAY_PORT_H
#define JC3636K718_DISPLAY_PORT_H

#include <stdint.h>

void screen_switch(bool on);
void set_brightness(uint8_t brightness);
// Flushes the complete initial LVGL screen while the panel and backlight are
// still hidden, then reveals the already-rendered frame.
bool display_present_first_frame(uint8_t brightness);
// Waits for the final asynchronous QSPI DMA transfer to complete. This is
// used while the backlight is off so a newly rendered full frame is never
// revealed halfway through a transfer.
bool display_wait_for_flush(uint32_t timeout_ms);

#endif
