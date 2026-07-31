#pragma once

#include <stdint.h>

// Creates a lightweight, self-contained LVGL boot overlay. Call after the
// display driver is registered and before revealing the backlight.
bool startup_screen_begin();

// Updates the visible initialization stage. LVGL remains owned by the caller's
// task; no background task touches UI objects.
void startup_screen_set_status(const char *status, uint8_t progress);

// Runs the LVGL handler for a bounded interval so the spinner and progress
// animation can advance between synchronous hardware initialization stages.
void startup_screen_pump(uint32_t duration_ms);

// Settles the moving boot artwork into a static READY frame before the
// application UI is built behind it. This makes the short LVGL object-build
// interval intentional rather than a visibly frozen spinner.
void startup_screen_prepare_transition();

// Keeps the overlay above the newly built application UI, switches frames
// while the PWM backlight is briefly dark, then restores the requested
// brightness and frees every boot-screen object.
void startup_screen_finish(uint8_t restore_brightness);
