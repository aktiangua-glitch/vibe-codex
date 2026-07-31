#include "startup_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "display_port.h"

namespace {

constexpr int kScreenSize = 360;
constexpr uint32_t kHandlerStepMs = 5;
constexpr uint32_t kBacklightStepMs = 12;

lv_obj_t *s_root = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_progress = nullptr;
lv_obj_t *s_spinner = nullptr;
uint32_t s_visible_since_ms = 0;

void set_plain_circle(lv_obj_t *object, lv_color_t color)
{
    lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    return label;
}

}  // namespace

bool startup_screen_begin()
{
    if (s_root != nullptr) {
        return true;
    }

    lv_obj_t *screen = lv_scr_act();
    if (screen == nullptr) {
        return false;
    }

    const lv_color_t background = lv_color_hex(0x040811);
    const lv_color_t panel = lv_color_hex(0x0B1522);
    const lv_color_t border = lv_color_hex(0x193247);
    const lv_color_t cyan = lv_color_hex(0x39E0D7);
    const lv_color_t purple = lv_color_hex(0x8D7CFF);
    const lv_color_t text = lv_color_hex(0xF4F8FC);
    const lv_color_t muted = lv_color_hex(0x8297AA);

    lv_obj_set_style_bg_color(screen, background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_root = lv_obj_create(screen);
    lv_obj_set_size(s_root, kScreenSize, kScreenSize);
    lv_obj_center(s_root);
    set_plain_circle(s_root, background);

    lv_obj_t *halo = lv_obj_create(s_root);
    lv_obj_set_size(halo, 284, 284);
    lv_obj_center(halo);
    set_plain_circle(halo, background);
    lv_obj_set_style_border_width(halo, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(halo, border, LV_PART_MAIN);
    lv_obj_set_style_border_opa(halo, LV_OPA_70, LV_PART_MAIN);

    s_spinner = lv_spinner_create(s_root, 900, 82);
    lv_obj_set_size(s_spinner, 238, 238);
    lv_obj_center(s_spinner);
    lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, border, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_spinner, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, cyan, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_spinner, true, LV_PART_INDICATOR);
    lv_obj_remove_style(s_spinner, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *badge = lv_obj_create(s_root);
    lv_obj_set_size(badge, 150, 150);
    lv_obj_center(badge);
    set_plain_circle(badge, panel);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, purple, LV_PART_MAIN);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, LV_PART_MAIN);

    lv_obj_t *mark =
        make_label(badge, "V", &lv_font_montserrat_32, cyan);
    lv_obj_align(mark, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *model =
        make_label(badge, "DIAL AI", &lv_font_montserrat_16, text);
    lv_obj_align(model, LV_ALIGN_CENTER, 0, 16);

    lv_obj_t *caption =
        make_label(badge, "NATIVE LVGL", &lv_font_montserrat_14, muted);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 45);

    s_status =
        make_label(s_root, "DISPLAY READY", &lv_font_montserrat_14, muted);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -43);

    s_progress = lv_bar_create(s_root);
    lv_obj_set_size(s_progress, 132, 4);
    lv_obj_align(s_progress, LV_ALIGN_BOTTOM_MID, 0, -27);
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 18, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, border, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress, cyan, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_INDICATOR);

    s_visible_since_ms = millis();
    return true;
}

void startup_screen_set_status(const char *status, uint8_t progress)
{
    if (s_root == nullptr) {
        return;
    }
    if (s_status != nullptr && status != nullptr) {
        lv_label_set_text(s_status, status);
        lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -43);
    }
    if (s_progress != nullptr) {
        lv_bar_set_value(
            s_progress,
            progress > 100 ? 100 : progress,
            LV_ANIM_ON);
    }

    // The peripheral work now runs on Core 0. The main LVGL loop will refresh
    // only the invalidated label and progress bar on its next 5 ms pass;
    // forcing the 360x360 root here caused a measured 136-138 ms spinner gap.
}

void startup_screen_pump(uint32_t duration_ms)
{
    const uint32_t started_ms = millis();
    do {
        lv_timer_handler();
        delay(kHandlerStepMs);
    } while (millis() - started_ms < duration_ms);
}

void startup_screen_prepare_transition()
{
    if (s_root == nullptr) {
        return;
    }

    // Whole-object alpha fading requires a 32-bit transparent LVGL screen.
    // This project deliberately uses RGB565, so stop the moving element and
    // present one explicit static frame before the short synchronous UI build.
    if (s_spinner != nullptr) {
        lv_anim_del(s_spinner, nullptr);
    }
    if (s_status != nullptr) {
        lv_label_set_text(s_status, "READY");
        lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -43);
    }
    if (s_progress != nullptr) {
        lv_bar_set_value(s_progress, 100, LV_ANIM_OFF);
    }
    lv_obj_invalidate(s_root);
    lv_refr_now(nullptr);
    (void)display_wait_for_flush(500);
}

void startup_screen_finish(uint8_t restore_brightness)
{
    if (s_root == nullptr) {
        return;
    }

    // app_ui_begin() creates its root after the splash. Restore the splash to
    // the foreground before the first handler call so the application cannot
    // flash on screen and then disappear again.
    lv_obj_move_foreground(s_root);

    restore_brightness =
        restore_brightness > 100 ? 100 : restore_brightness;
    const uint8_t down[] = {
        static_cast<uint8_t>((restore_brightness * 2U) / 3U),
        static_cast<uint8_t>(restore_brightness / 3U),
        0,
    };
    for (uint8_t level : down) {
        set_brightness(level);
        startup_screen_pump(kBacklightStepMs);
    }

    lv_obj_del(s_root);
    s_root = nullptr;
    s_status = nullptr;
    s_progress = nullptr;
    s_spinner = nullptr;

    // Render the entire application frame while the backlight is dark. Wait
    // for the final asynchronous DMA flush before revealing it.
    lv_obj_t *screen = lv_scr_act();
    if (screen != nullptr) {
        lv_obj_invalidate(screen);
    }
    const uint32_t frame_started_ms = millis();
    lv_refr_now(nullptr);
    if (!display_wait_for_flush(500)) {
        Serial.println("[BOOT] Application-frame flush timed out");
    }
    Serial.printf(
        "[BOOT] Hidden application-frame render: %lu ms\n",
        static_cast<unsigned long>(millis() - frame_started_ms));

    const uint8_t up[] = {
        static_cast<uint8_t>(restore_brightness / 3U),
        static_cast<uint8_t>((restore_brightness * 2U) / 3U),
        restore_brightness,
    };
    for (uint8_t level : up) {
        set_brightness(level);
        delay(kBacklightStepMs);
    }

    Serial.printf(
        "[BOOT] Splash visible for %lu ms\n",
        static_cast<unsigned long>(millis() - s_visible_since_ms));
}
