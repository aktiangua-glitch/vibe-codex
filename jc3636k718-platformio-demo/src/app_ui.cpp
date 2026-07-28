#include "app_ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board_hardware.h"
#include "knob.h"
#include "media_engine.h"

namespace {

constexpr int kScreenSize = 360;
constexpr int kContentX = 26;
constexpr int kContentY = 55;
constexpr int kContentWidth = 308;
constexpr int kContentHeight = 238;
constexpr uint32_t kUiRefreshMs = 90;

const lv_color_t kBg = lv_color_hex(0x070B13);
const lv_color_t kPanel = lv_color_hex(0x111B29);
const lv_color_t kPanelRaised = lv_color_hex(0x172538);
const lv_color_t kBorder = lv_color_hex(0x26384D);
const lv_color_t kText = lv_color_hex(0xF4F8FC);
const lv_color_t kMuted = lv_color_hex(0x8DA2B7);
const lv_color_t kCyan = lv_color_hex(0x38DDD8);
const lv_color_t kGreen = lv_color_hex(0x54DEA5);
const lv_color_t kAmber = lv_color_hex(0xFFBE5C);
const lv_color_t kRed = lv_color_hex(0xFF6F7D);
const lv_color_t kPurple = lv_color_hex(0x907DFF);

enum class Page : uint8_t {
    Overview,
    Spectrum,
    Media,
    Actuators,
    Storage,
    Wireless,
    System,
    Count,
};

enum class KnobMode : uint8_t {
    Page,
    Gain,
    Brightness,
    Volume,
};

enum class Action : intptr_t {
    PreviousPage = 1,
    NextPage,
    ToggleKnobMode,
    GainDown,
    GainUp,
    RecordStart,
    RecordStop,
    Haptic,
    Rgb,
    Speaker,
    SdRescan,
    PlayPhoto,
    PlayVideo,
    MediaStop,
    OverlayExit,
    UsbDiskToggle,
    WifiScan,
    BleScan,
    HidToggle,
};

struct PageRefs {
    lv_obj_t *overview_power;
    lv_obj_t *overview_flash;
    lv_obj_t *overview_psram;
    lv_obj_t *overview_sd;
    lv_obj_t *overview_mic;
    lv_obj_t *overview_input;

    lv_obj_t *spectrum_rms;
    lv_obj_t *spectrum_frequency;
    lv_obj_t *spectrum_gain;
    lv_obj_t *spectrum_chart;
    lv_chart_series_t *spectrum_series;
    lv_obj_t *record_button;
    lv_obj_t *record_button_label;
    lv_obj_t *record_status;
    lv_obj_t *record_path;

    lv_obj_t *media_state;
    lv_obj_t *media_detail;
    lv_obj_t *media_metrics;
    lv_obj_t *media_path;

    lv_obj_t *actuator_haptic;
    lv_obj_t *actuator_rgb;
    lv_obj_t *actuator_speaker;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value;

    lv_obj_t *storage_state;
    lv_obj_t *storage_capacity;
    lv_obj_t *storage_used;
    lv_obj_t *storage_bar;
    lv_obj_t *storage_usb_button;
    lv_obj_t *storage_usb_label;
    lv_obj_t *storage_safety;

    lv_obj_t *wireless_state;
    lv_obj_t *wifi_rows[BOARD_DEMO_WIRELESS_ROWS];
    lv_obj_t *ble_rows[BOARD_DEMO_WIRELESS_ROWS];

    lv_obj_t *system_uptime;
    lv_obj_t *system_flash;
    lv_obj_t *system_psram;
    lv_obj_t *system_usb;
    lv_obj_t *system_input;
    lv_obj_t *system_hid;
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_content = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_page_index = nullptr;
lv_obj_t *s_mode_button = nullptr;
lv_obj_t *s_mode_label = nullptr;
lv_obj_t *s_toast = nullptr;
lv_obj_t *s_toast_label = nullptr;
lv_obj_t *s_media_overlay = nullptr;
lv_obj_t *s_media_image = nullptr;
lv_obj_t *s_media_overlay_status = nullptr;
lv_obj_t *s_media_overlay_path = nullptr;

lv_style_t s_card_style;
lv_style_t s_button_style;
lv_style_t s_nav_style;
bool s_styles_ready = false;
bool s_syncing_slider = false;

PageRefs s_refs = {};
Page s_page = Page::Overview;
KnobMode s_knob_mode = KnobMode::Page;
uint8_t s_haptic_pattern = 0;
uint32_t s_last_refresh_ms = 0;
uint32_t s_toast_until_ms = 0;
uint32_t s_media_sequence_floor = 0;
uint32_t s_media_scan_after_ms = 0;
uint32_t s_media_rescan_started_ms = 0;
size_t s_next_photo_index = 0;
size_t s_next_video_index = 0;
bool s_media_rescan_pending = false;
bool s_msc_enable_pending = false;
uint32_t s_msc_transition_started_ms = 0;
uint32_t s_msc_transition_next_ms = 0;
bool s_record_start_pending = false;
bool s_record_start_command_sent = false;
uint32_t s_record_transition_started_ms = 0;
uint32_t s_record_transition_next_ms = 0;
MediaKind s_overlay_kind = MediaKind::Jpeg;
lv_img_dsc_t s_media_image_descriptor = {};

const char *const kPageNames[] = {
    "OVERVIEW",
    "SPECTRUM",
    "MEDIA",
    "ACTUATORS",
    "STORAGE",
    "WIRELESS",
    "SYSTEM",
};

void render_page();
void update_current_page();
void navigate_pages(int delta);
void close_media_overlay(bool stop_engine);
void play_next_media(MediaKind kind);
void request_media_rescan();
void toggle_usb_disk();
void request_recording_start();
void request_recording_stop();

void set_plain_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t text_color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN);
    return label;
}

void set_label_color(lv_obj_t *label, lv_color_t text_color)
{
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    }
}

lv_obj_t *make_card(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    lv_color_t accent = kCyan)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &s_card_style, LV_PART_MAIN);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_pos(bar, 0, 10);
    lv_obj_set_size(bar, 3, height - 20);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *make_metric(
    lv_obj_t *parent,
    int x,
    int y,
    const char *name,
    const char *initial_value,
    lv_color_t accent)
{
    lv_obj_t *card = make_card(parent, x, y, 146, 57, accent);

    lv_obj_t *name_label =
        make_label(card, name, &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(name_label, 11, 6);

    lv_obj_t *value =
        make_label(card, initial_value, &lv_font_montserrat_16, kText);
    lv_obj_set_pos(value, 11, 29);
    lv_obj_set_width(value, 124);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    return value;
}

lv_obj_t *make_button(
    lv_obj_t *parent,
    const char *text,
    int x,
    int y,
    int width,
    int height,
    Action action,
    lv_color_t accent = kCyan)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_add_style(button, &s_button_style, LV_PART_MAIN);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_border_color(button, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, accent, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        button,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
                return;
            }

            board_mark_touch();
            const Action selected = static_cast<Action>(
                reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));

            auto show_toast = [](const char *message) {
                if (s_toast == nullptr) {
                    return;
                }
                lv_label_set_text(s_toast_label, message);
                lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_toast);
                s_toast_until_ms = millis() + 1500;
            };

            switch (selected) {
                case Action::PreviousPage:
                    navigate_pages(-1);
                    break;
                case Action::NextPage:
                    navigate_pages(1);
                    break;
                case Action::ToggleKnobMode:
                    if (s_knob_mode != KnobMode::Page) {
                        s_knob_mode = KnobMode::Page;
                    } else if (s_page == Page::Spectrum) {
                        s_knob_mode = KnobMode::Gain;
                    } else if (s_page == Page::Actuators) {
                        s_knob_mode = KnobMode::Brightness;
                    } else if (
                        s_page == Page::System && board_hid_available()) {
                        s_knob_mode = KnobMode::Volume;
                    }
                    break;
                case Action::GainDown:
                    board_adjust_spectrum_gain(-2);
                    s_knob_mode = KnobMode::Gain;
                    break;
                case Action::GainUp:
                    board_adjust_spectrum_gain(2);
                    s_knob_mode = KnobMode::Gain;
                    break;
                case Action::RecordStart:
                    request_recording_start();
                    break;
                case Action::RecordStop:
                    request_recording_stop();
                    break;
                case Action::Haptic:
                    show_toast(
                        board_play_haptic(s_haptic_pattern++)
                            ? "Haptic pattern played"
                            : "Haptic unavailable");
                    break;
                case Action::Rgb:
                    show_toast(
                        board_rgb_toggle_chase()
                            ? "RGB chase started"
                            : "RGB chase stopped");
                    break;
                case Action::Speaker:
                    show_toast(
                        board_play_speaker_test()
                            ? "Speaker test started"
                            : "Speaker busy / unavailable");
                    break;
                case Action::SdRescan:
                    if (board_get_status().msc_active ||
                        s_msc_enable_pending) {
                        show_toast("Disable USB disk before rescan");
                    } else {
                        request_media_rescan();
                        show_toast("TF card rescan requested");
                    }
                    break;
                case Action::PlayPhoto:
                    play_next_media(MediaKind::Jpeg);
                    break;
                case Action::PlayVideo:
                    play_next_media(MediaKind::Mjpeg);
                    break;
                case Action::MediaStop:
                case Action::OverlayExit:
                    close_media_overlay(true);
                    show_toast("Media playback stopped");
                    break;
                case Action::UsbDiskToggle:
                    toggle_usb_disk();
                    break;
                case Action::WifiScan:
                    board_request_wifi_scan();
                    show_toast("Wi-Fi scan started");
                    break;
                case Action::BleScan:
                    board_request_ble_scan();
                    show_toast("BLE scan started");
                    break;
                case Action::HidToggle: {
                    const bool enabled = !board_hid_volume_enabled();
                    board_set_hid_volume_enabled(enabled);
                    show_toast(enabled ? "HID volume enabled"
                                       : "HID volume disabled");
                    break;
                }
            }

            const char *mode_text = "PAGE";
            lv_color_t mode_color = kMuted;
            switch (s_knob_mode) {
                case KnobMode::Gain:
                    mode_text = "GAIN";
                    mode_color = kCyan;
                    break;
                case KnobMode::Brightness:
                    mode_text = "BRI";
                    mode_color = kAmber;
                    break;
                case KnobMode::Volume:
                    mode_text = "VOL";
                    mode_color = kPurple;
                    break;
                case KnobMode::Page:
                    break;
            }
            lv_label_set_text(s_mode_label, mode_text);
            lv_obj_set_style_border_color(
                s_mode_button, mode_color, LV_PART_MAIN);
            update_current_page();
        },
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(action)));

    lv_obj_t *label =
        make_label(button, text, &lv_font_montserrat_14, kText);
    lv_obj_set_width(label, width - 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

void show_toast(const char *message)
{
    if (s_toast == nullptr) {
        return;
    }
    lv_label_set_text(s_toast_label, message);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    s_toast_until_ms = millis() + 1500;
}

void open_media_overlay(MediaKind kind, const char *path)
{
    s_overlay_kind = kind;

    if (s_media_overlay == nullptr) {
        s_media_overlay = lv_obj_create(s_root);
        lv_obj_set_pos(s_media_overlay, 0, 0);
        lv_obj_set_size(s_media_overlay, kScreenSize, kScreenSize);
        lv_obj_set_style_bg_color(s_media_overlay, kBg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_media_overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_media_overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(
            s_media_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_media_overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_media_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            s_media_overlay,
            [](lv_event_t *event) {
                if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
                    board_mark_touch();
                }
            },
            LV_EVENT_PRESSED,
            nullptr);

        s_media_image = lv_img_create(s_media_overlay);
        lv_obj_set_pos(s_media_image, 0, 0);
        lv_obj_add_flag(s_media_image, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *hud = lv_obj_create(s_media_overlay);
        lv_obj_set_pos(hud, 78, 9);
        lv_obj_set_size(hud, 204, 54);
        lv_obj_set_style_bg_color(hud, kBg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hud, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_color(hud, kBorder, LV_PART_MAIN);
        lv_obj_set_style_border_width(hud, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(hud, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(hud, 0, LV_PART_MAIN);
        lv_obj_clear_flag(hud, LV_OBJ_FLAG_SCROLLABLE);

        s_media_overlay_status =
            make_label(hud, "LOADING...", &lv_font_montserrat_16, kText);
        lv_obj_align(s_media_overlay_status, LV_ALIGN_TOP_MID, 0, 6);
        s_media_overlay_path =
            make_label(hud, "", &lv_font_montserrat_14, kMuted);
        lv_obj_set_width(s_media_overlay_path, 176);
        lv_label_set_long_mode(
            s_media_overlay_path, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(
            s_media_overlay_path, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(s_media_overlay_path, LV_ALIGN_BOTTOM_MID, 0, -5);

        make_button(
            s_media_overlay,
            "STOP / EXIT",
            122,
            301,
            116,
            43,
            Action::OverlayExit,
            kRed);
    }

    lv_obj_add_flag(s_media_image, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(
        s_media_overlay_status,
        kind == MediaKind::Jpeg ? "PHOTO  LOADING" : "VIDEO  LOADING");
    lv_label_set_text(
        s_media_overlay_path,
        path != nullptr ? path : "");
    lv_obj_move_foreground(s_media_overlay);
}

void close_media_overlay(bool stop_engine)
{
    if (stop_engine) {
        media_engine_stop();
    }
    if (s_media_overlay == nullptr) {
        return;
    }

    // This can be called from a button event, so defer destruction until LVGL
    // has finished dispatching the current event.
    lv_obj_del_async(s_media_overlay);
    s_media_overlay = nullptr;
    s_media_image = nullptr;
    s_media_overlay_status = nullptr;
    s_media_overlay_path = nullptr;
}

void navigate_pages(int delta)
{
    if (delta == 0) {
        return;
    }
    const int page_count = static_cast<int>(Page::Count);
    int next = static_cast<int>(s_page) + delta;
    next %= page_count;
    if (next < 0) {
        next += page_count;
    }

    if (s_page == Page::Media &&
        next != static_cast<int>(Page::Media)) {
        close_media_overlay(true);
    }
    s_page = static_cast<Page>(next);
    s_knob_mode = KnobMode::Page;
    render_page();
}

bool recording_uses_tf(const RecordingStatus &recording)
{
    return s_record_start_pending ||
           recording.starting ||
           recording.recording ||
           recording.stopping;
}

void request_recording_start()
{
    const BoardStatus board = board_get_status();
    const RecordingStatus recording =
        board_get_recording_status();
    if (!recording.available) {
        show_toast(
            recording.error[0] != '\0'
                ? recording.error
                : "Recording is unavailable");
        return;
    }
    if (board.msc_active || s_msc_enable_pending) {
        show_toast("Close USB disk before recording");
        return;
    }
    if (recording_uses_tf(recording)) {
        show_toast(
            recording.recording ? "Recording is already active"
                                : "Recording transition in progress");
        return;
    }

    close_media_overlay(true);
    media_engine_stop();
    s_media_rescan_pending = false;
    s_record_start_pending = true;
    s_record_start_command_sent = false;
    s_record_transition_started_ms = millis();
    s_record_transition_next_ms =
        s_record_transition_started_ms + 60U;
    show_toast("Preparing TF recording");
}

void request_recording_stop()
{
    s_record_start_pending = false;
    s_record_start_command_sent = false;
    const RecordingStatus recording =
        board_get_recording_status();
    if (!recording.starting &&
        !recording.recording &&
        !recording.stopping) {
        show_toast("Recorder is already stopped");
        return;
    }
    board_recording_stop();
    show_toast("Stopping and closing WAV file");
}

void play_next_media(MediaKind kind)
{
    const BoardStatus board = board_get_status();
    if (board.msc_active || s_msc_enable_pending) {
        show_toast("USB disk owns the TF card");
        return;
    }

    const RecordingStatus recording =
        board_get_recording_status();
    if (recording_uses_tf(recording)) {
        show_toast("Stop recording before media playback");
        return;
    }

    const MediaStatus before = media_engine_get_status();
    if (!before.initialized || !before.decoder_available) {
        show_toast(
            before.last_error[0] != '\0'
                ? before.last_error
                : "Media decoder unavailable");
        return;
    }

    const size_t count = media_engine_file_count(kind);
    if (count == 0) {
        show_toast(
            kind == MediaKind::Jpeg
                ? "No JPG in /pic"
                : "No MJPEG in /mjpeg");
        return;
    }

    size_t *next_index =
        kind == MediaKind::Jpeg
            ? &s_next_photo_index
            : &s_next_video_index;
    const size_t index = *next_index % count;
    *next_index = (index + 1U) % count;

    MediaFileInfo file = {};
    if (!media_engine_file_info(kind, index, &file)) {
        show_toast("Cannot read media file entry");
        return;
    }

    s_media_sequence_floor = before.sequence;
    s_media_rescan_pending = false;
    const bool accepted = media_engine_play_index(
        kind,
        index,
        20,
        kind == MediaKind::Mjpeg);
    if (!accepted) {
        show_toast("Media play request rejected");
        return;
    }

    open_media_overlay(kind, file.path);
}

void request_media_rescan()
{
    const BoardStatus board = board_get_status();
    if (board.msc_active || s_msc_enable_pending) {
        show_toast("Disable USB disk before rescan");
        return;
    }
    if (recording_uses_tf(board_get_recording_status())) {
        show_toast("Stop recording before rescan");
        return;
    }

    close_media_overlay(true);
    board_request_sd_rescan();
    s_media_rescan_pending = true;
    s_media_rescan_started_ms = millis();
    s_media_scan_after_ms = s_media_rescan_started_ms + 450U;
}

void toggle_usb_disk()
{
    const BoardStatus board = board_get_status();
    if (!board.msc_compiled) {
        show_toast("USB disk is not in this build");
        return;
    }
    if (s_msc_enable_pending) {
        show_toast("Waiting for TF access to stop");
        return;
    }

    if (board.msc_active) {
        if (!board.msc_ejected) {
            show_toast("Eject disk on computer first");
            return;
        }
        if (!board_usb_msc_set_active(false)) {
            show_toast("Cannot close USB disk");
            return;
        }

        board_request_sd_rescan();
        s_media_rescan_pending = true;
        s_media_rescan_started_ms = millis();
        s_media_scan_after_ms = s_media_rescan_started_ms + 450U;
        show_toast("USB disk closed; rescanning TF");
        return;
    }

    close_media_overlay(true);
    media_engine_stop();
    s_record_start_pending = false;
    s_record_start_command_sent = false;
    const RecordingStatus recording =
        board_get_recording_status();
    if (recording.starting ||
        recording.recording ||
        recording.stopping) {
        board_recording_stop();
    }
    s_media_rescan_pending = false;
    s_msc_enable_pending = true;
    s_msc_transition_started_ms = millis();
    s_msc_transition_next_ms = s_msc_transition_started_ms + 60U;
    show_toast("Stopping media for USB disk");
}

void update_mode_button()
{
    if (s_mode_label == nullptr) {
        return;
    }

    const char *text = "PAGE";
    lv_color_t outline = kMuted;
    switch (s_knob_mode) {
        case KnobMode::Gain:
            text = "GAIN";
            outline = kCyan;
            break;
        case KnobMode::Brightness:
            text = "BRI";
            outline = kAmber;
            break;
        case KnobMode::Volume:
            text = "VOL";
            outline = kPurple;
            break;
        case KnobMode::Page:
            break;
    }
    lv_label_set_text(s_mode_label, text);
    lv_obj_set_style_border_color(
        s_mode_button, outline, LV_PART_MAIN);
}

void reset_page_refs()
{
    memset(&s_refs, 0, sizeof(s_refs));
}

void create_overview_page()
{
    s_refs.overview_power =
        make_metric(s_content, 0, 0, "POWER", "-- V", kGreen);
    s_refs.overview_flash =
        make_metric(s_content, 162, 0, "FLASH", "--", kCyan);
    s_refs.overview_psram =
        make_metric(s_content, 0, 64, "PSRAM", "--", kPurple);
    s_refs.overview_sd =
        make_metric(s_content, 162, 64, "TF CARD", "--", kAmber);
    s_refs.overview_mic =
        make_metric(s_content, 0, 128, "MIC / AUDIO", "--", kCyan);
    s_refs.overview_input =
        make_metric(s_content, 162, 128, "TOUCH / KNOB", "--", kGreen);

    lv_obj_t *hint =
        make_label(s_content, "Turn knob to explore pages", &lv_font_montserrat_14, kMuted);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void create_spectrum_page()
{
    s_refs.spectrum_rms =
        make_label(s_content, "--.- dB", &lv_font_montserrat_32, kText);
    lv_obj_set_pos(s_refs.spectrum_rms, 2, -4);

    s_refs.spectrum_frequency =
        make_label(s_content, "--- Hz", &lv_font_montserrat_22, kCyan);
    lv_obj_align(s_refs.spectrum_frequency, LV_ALIGN_TOP_RIGHT, -2, 3);
    s_refs.spectrum_gain =
        make_label(s_content, "GAIN +-- dB", &lv_font_montserrat_14, kMuted);
    lv_obj_align(s_refs.spectrum_gain, LV_ALIGN_TOP_RIGHT, -2, 28);

    s_refs.spectrum_chart = lv_chart_create(s_content);
    lv_obj_set_pos(s_refs.spectrum_chart, 0, 42);
    lv_obj_set_size(s_refs.spectrum_chart, 308, 102);
    lv_obj_add_style(
        s_refs.spectrum_chart, &s_card_style, LV_PART_MAIN);
    lv_obj_set_style_pad_all(
        s_refs.spectrum_chart, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(
        s_refs.spectrum_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_width(
        s_refs.spectrum_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(
        s_refs.spectrum_chart, 3, LV_PART_ITEMS);
    lv_chart_set_type(
        s_refs.spectrum_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(
        s_refs.spectrum_chart, BOARD_DEMO_SPECTRUM_BANDS);
    lv_chart_set_range(
        s_refs.spectrum_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_refs.spectrum_chart, 0, 0);
    s_refs.spectrum_series = lv_chart_add_series(
        s_refs.spectrum_chart, kCyan, LV_CHART_AXIS_PRIMARY_Y);
    for (size_t i = 0; i < BOARD_DEMO_SPECTRUM_BANDS; ++i) {
        lv_chart_set_value_by_id(
            s_refs.spectrum_chart, s_refs.spectrum_series, i, 2);
    }

    s_refs.record_button = make_button(
        s_content, "RECORD", 0, 151, 104, 38, Action::RecordStart, kRed);
    s_refs.record_button_label =
        lv_obj_get_child(s_refs.record_button, 0);
    make_button(
        s_content, "STOP", 112, 151, 72, 38, Action::RecordStop, kAmber);
    make_button(
        s_content, "-", 192, 151, 52, 38, Action::GainDown, kPurple);
    make_button(
        s_content, "+", 252, 151, 52, 38, Action::GainUp, kCyan);

    s_refs.record_status = make_label(
        s_content,
        "REC IDLE | 00:00 | 0 KB | DROP 0",
        &lv_font_montserrat_14,
        kMuted);
    lv_obj_set_pos(s_refs.record_status, 2, 196);
    lv_obj_set_width(s_refs.record_status, 304);
    lv_label_set_long_mode(
        s_refs.record_status, LV_LABEL_LONG_DOT);

    s_refs.record_path = make_label(
        s_content,
        "/recordings",
        &lv_font_montserrat_14,
        kMuted);
    lv_obj_set_pos(s_refs.record_path, 2, 218);
    lv_obj_set_width(s_refs.record_path, 304);
    lv_label_set_long_mode(
        s_refs.record_path, LV_LABEL_LONG_DOT);
}

void create_media_page()
{
    lv_obj_t *hero = make_card(s_content, 0, 0, 308, 66, kPurple);
    lv_obj_t *name =
        make_label(hero, "TF MEDIA", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(name, 12, 7);
    s_refs.media_state =
        make_label(hero, "CHECKING...", &lv_font_montserrat_22, kText);
    lv_obj_set_pos(s_refs.media_state, 12, 29);
    s_refs.media_detail =
        make_label(hero, "JPG -- | MJPEG --", &lv_font_montserrat_14, kMuted);
    lv_obj_align(s_refs.media_detail, LV_ALIGN_RIGHT_MID, -12, 12);

    lv_obj_t *metrics =
        make_card(s_content, 0, 74, 308, 64, kCyan);
    s_refs.media_metrics =
        make_label(metrics, "FPS -- | DECODE -- ms", &lv_font_montserrat_16, kText);
    lv_obj_set_pos(s_refs.media_metrics, 12, 8);
    s_refs.media_path =
        make_label(metrics, "/pic + /mjpeg", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(s_refs.media_path, 12, 37);
    lv_obj_set_width(s_refs.media_path, 282);
    lv_label_set_long_mode(s_refs.media_path, LV_LABEL_LONG_DOT);

    make_button(
        s_content, "PLAY PHOTO", 0, 147, 148, 39, Action::PlayPhoto, kPurple);
    make_button(
        s_content, "PLAY VIDEO", 160, 147, 148, 39, Action::PlayVideo, kCyan);
    make_button(
        s_content, "STOP", 0, 194, 148, 39, Action::MediaStop, kRed);
    make_button(
        s_content, "RESCAN", 160, 194, 148, 39, Action::SdRescan, kAmber);
}

void brightness_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        board_mark_touch();
        s_knob_mode = KnobMode::Brightness;
        update_mode_button();
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED ||
        s_syncing_slider ||
        s_refs.brightness_slider == nullptr) {
        return;
    }

    const int value =
        lv_slider_get_value(s_refs.brightness_slider);
    board_set_brightness(static_cast<uint8_t>(value));
    if (s_refs.brightness_value != nullptr) {
        lv_label_set_text_fmt(
            s_refs.brightness_value, "%d%%", value);
    }
}

void create_actuators_page()
{
    make_button(
        s_content, "VIBRATE", 0, 0, 94, 72, Action::Haptic, kPurple);
    make_button(
        s_content, "CHASE", 107, 0, 94, 72, Action::Rgb, kCyan);
    make_button(
        s_content, "SPEAKER", 214, 0, 94, 72, Action::Speaker, kAmber);

    s_refs.actuator_haptic =
        make_label(s_content, "--", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(s_refs.actuator_haptic, 17, 76);
    s_refs.actuator_rgb =
        make_label(s_content, "--", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(s_refs.actuator_rgb, 127, 76);
    s_refs.actuator_speaker =
        make_label(s_content, "--", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(s_refs.actuator_speaker, 232, 76);

    lv_obj_t *brightness =
        make_card(s_content, 0, 105, 308, 90, kGreen);
    lv_obj_t *title =
        make_label(brightness, "DISPLAY BRIGHTNESS", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(title, 12, 8);
    s_refs.brightness_value =
        make_label(brightness, "--%", &lv_font_montserrat_16, kText);
    lv_obj_align(s_refs.brightness_value, LV_ALIGN_TOP_RIGHT, -12, 7);

    s_refs.brightness_slider = lv_slider_create(brightness);
    lv_obj_set_pos(s_refs.brightness_slider, 13, 48);
    lv_obj_set_size(s_refs.brightness_slider, 278, 12);
    lv_slider_set_range(s_refs.brightness_slider, 12, 100);
    lv_obj_set_style_bg_color(
        s_refs.brightness_slider, kPanelRaised, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        s_refs.brightness_slider, kGreen, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        s_refs.brightness_slider, kText, LV_PART_KNOB);
    lv_obj_set_style_pad_all(
        s_refs.brightness_slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(
        s_refs.brightness_slider, brightness_event, LV_EVENT_ALL, nullptr);

    lv_obj_t *hint = make_label(
        s_content, "Tap PAGE, then turn knob for brightness", &lv_font_montserrat_14, kMuted);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void create_storage_page()
{
    lv_obj_t *capacity = make_card(s_content, 0, 0, 308, 82, kAmber);
    lv_obj_t *title =
        make_label(capacity, "TF CARD", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(title, 12, 5);
    s_refs.storage_state =
        make_label(capacity, "CHECKING...", &lv_font_montserrat_22, kText);
    lv_obj_set_pos(s_refs.storage_state, 12, 25);
    s_refs.storage_capacity =
        make_label(capacity, "-- GB", &lv_font_montserrat_16, kAmber);
    lv_obj_align(s_refs.storage_capacity, LV_ALIGN_TOP_RIGHT, -12, 27);
    s_refs.storage_used =
        make_label(capacity, "Used --", &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(s_refs.storage_used, 12, 57);

    s_refs.storage_bar = lv_bar_create(s_content);
    lv_obj_set_pos(s_refs.storage_bar, 5, 92);
    lv_obj_set_size(s_refs.storage_bar, 298, 12);
    lv_bar_set_range(s_refs.storage_bar, 0, 100);
    lv_obj_set_style_bg_color(
        s_refs.storage_bar, kPanelRaised, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(
        s_refs.storage_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        s_refs.storage_bar, kAmber, LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        s_refs.storage_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(
        s_refs.storage_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    make_button(
        s_content, "RESCAN TF", 0, 115, 148, 40, Action::SdRescan, kAmber);
    s_refs.storage_usb_button = make_button(
        s_content,
        "USB DISK",
        160,
        115,
        148,
        40,
        Action::UsbDiskToggle,
        kCyan);
    s_refs.storage_usb_label =
        lv_obj_get_child(s_refs.storage_usb_button, 0);

    lv_obj_t *safety =
        make_card(s_content, 0, 165, 308, 68, kRed);
    s_refs.storage_safety = make_label(
        safety,
        "USB disk shares the TF card.\nEject it on the computer before closing.",
        &lv_font_montserrat_14,
        kMuted);
    lv_obj_set_pos(s_refs.storage_safety, 12, 10);
    lv_obj_set_width(s_refs.storage_safety, 284);
    lv_label_set_long_mode(
        s_refs.storage_safety, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(
        s_refs.storage_safety, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(
        s_refs.storage_safety, 5, LV_PART_MAIN);
}

void create_wireless_page()
{
    make_button(
        s_content, "SCAN WI-FI", 0, 0, 147, 42, Action::WifiScan, kCyan);
    make_button(
        s_content, "SCAN BLE", 161, 0, 147, 42, Action::BleScan, kPurple);

    s_refs.wireless_state =
        make_label(s_content, "Tap a scan button", &lv_font_montserrat_14, kMuted);
    lv_obj_align(s_refs.wireless_state, LV_ALIGN_TOP_MID, 0, 49);

    lv_obj_t *wifi_card =
        make_card(s_content, 0, 75, 148, 148, kCyan);
    lv_obj_t *wifi_title =
        make_label(wifi_card, "WI-FI", &lv_font_montserrat_14, kCyan);
    lv_obj_set_pos(wifi_title, 11, 7);

    lv_obj_t *ble_card =
        make_card(s_content, 160, 75, 148, 148, kPurple);
    lv_obj_t *ble_title =
        make_label(ble_card, "BLE", &lv_font_montserrat_14, kPurple);
    lv_obj_set_pos(ble_title, 11, 7);

    for (size_t i = 0; i < BOARD_DEMO_WIRELESS_ROWS; ++i) {
        s_refs.wifi_rows[i] =
            make_label(wifi_card, "--", &lv_font_montserrat_14, kText);
        lv_obj_set_pos(s_refs.wifi_rows[i], 11, 34 + static_cast<int>(i) * 34);
        lv_obj_set_width(s_refs.wifi_rows[i], 124);
        lv_label_set_long_mode(
            s_refs.wifi_rows[i], LV_LABEL_LONG_DOT);

        s_refs.ble_rows[i] =
            make_label(ble_card, "--", &lv_font_montserrat_14, kText);
        lv_obj_set_pos(s_refs.ble_rows[i], 11, 34 + static_cast<int>(i) * 34);
        lv_obj_set_width(s_refs.ble_rows[i], 124);
        lv_label_set_long_mode(
            s_refs.ble_rows[i], LV_LABEL_LONG_DOT);
    }
}

void create_system_page()
{
    s_refs.system_uptime =
        make_metric(s_content, 0, 0, "UPTIME", "--:--:--", kGreen);
    s_refs.system_flash =
        make_metric(s_content, 162, 0, "FLASH", "--", kCyan);
    s_refs.system_psram =
        make_metric(s_content, 0, 64, "PSRAM", "--", kPurple);
    s_refs.system_usb =
        make_metric(s_content, 162, 64, "USB MODE", "CDC", kAmber);
    s_refs.system_input =
        make_metric(s_content, 0, 128, "INPUT TEST", "--", kGreen);
    s_refs.system_hid =
        make_metric(s_content, 162, 128, "HID VOLUME", "--", kPurple);

    if (board_hid_available()) {
        make_button(
            s_content, "TOGGLE HID VOLUME", 72, 197, 164, 38, Action::HidToggle, kPurple);
    } else {
        lv_obj_t *hint = make_label(
            s_content, "HID: build jc3636k718_hid environment", &lv_font_montserrat_14, kMuted);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
}

void render_page()
{
    if (s_content == nullptr) {
        return;
    }

    lv_obj_clean(s_content);
    reset_page_refs();

    const uint8_t page_index = static_cast<uint8_t>(s_page);
    lv_label_set_text(s_title, kPageNames[page_index]);
    lv_label_set_text_fmt(
        s_page_index,
        "LIVE  %u / %u",
        static_cast<unsigned>(page_index + 1),
        static_cast<unsigned>(Page::Count));

    switch (s_page) {
        case Page::Overview:
            create_overview_page();
            break;
        case Page::Spectrum:
            create_spectrum_page();
            break;
        case Page::Media:
            create_media_page();
            break;
        case Page::Actuators:
            create_actuators_page();
            break;
        case Page::Storage:
            create_storage_page();
            break;
        case Page::Wireless:
            create_wireless_page();
            break;
        case Page::System:
            create_system_page();
            break;
        case Page::Count:
            break;
    }

    update_mode_button();
    update_current_page();
}

void update_overview(const BoardStatus &status)
{
    lv_label_set_text_fmt(
        s_refs.overview_power, "%.2f V", status.power_volts);
    set_label_color(
        s_refs.overview_power,
        status.power_volts > 3.0f ? kGreen : kAmber);

    lv_label_set_text_fmt(
        s_refs.overview_flash,
        "%u MB  %s",
        static_cast<unsigned>(status.flash_bytes / (1024U * 1024U)),
        status.flash_ok ? "OK" : "CHECK");
    set_label_color(
        s_refs.overview_flash, status.flash_ok ? kCyan : kRed);

    lv_label_set_text_fmt(
        s_refs.overview_psram,
        "%u MB  %s",
        static_cast<unsigned>(status.psram_bytes / (1024U * 1024U)),
        status.psram_ok ? "OK" : "CHECK");
    set_label_color(
        s_refs.overview_psram, status.psram_ok ? kPurple : kRed);

    lv_label_set_text(
        s_refs.overview_sd,
        status.msc_active
            ? "USB DISK"
            : (status.sd_present ? "MOUNTED" : "NO CARD"));
    set_label_color(
        s_refs.overview_sd,
        status.msc_active ? kCyan
                          : (status.sd_present ? kAmber : kRed));

    lv_label_set_text(
        s_refs.overview_mic,
        status.mic_ready
            ? (status.speaker_ready ? "MIC + OUT" : "MIC READY")
            : "CHECK MIC");
    set_label_color(
        s_refs.overview_mic, status.mic_ready ? kCyan : kRed);

    const bool both_inputs = status.touch_seen && status.knob_seen;
    lv_label_set_text(
        s_refs.overview_input,
        both_inputs
            ? "BOTH SEEN"
            : (status.touch_seen
                   ? "TOUCH SEEN"
                   : (status.knob_seen ? "KNOB SEEN" : "TRY BOTH")));
    set_label_color(
        s_refs.overview_input, both_inputs ? kGreen : kAmber);
}

void update_spectrum(
    const BoardStatus &status,
    const AudioSnapshot &audio)
{
    if (!audio.valid) {
        lv_label_set_text(s_refs.spectrum_rms, "WAITING");
        lv_label_set_text(s_refs.spectrum_frequency, "--- Hz");
    } else {
        lv_label_set_text_fmt(
            s_refs.spectrum_rms, "%.1f dB", audio.rms_db);
        lv_label_set_text_fmt(
            s_refs.spectrum_frequency,
            "%u Hz",
            static_cast<unsigned>(audio.dominant_hz));
        for (size_t i = 0; i < BOARD_DEMO_SPECTRUM_BANDS; ++i) {
            const int value = constrain(
                static_cast<int>(lroundf(audio.bands[i] * 100.0f)),
                1,
                100);
            lv_chart_set_value_by_id(
                s_refs.spectrum_chart,
                s_refs.spectrum_series,
                i,
                value);
        }
        lv_chart_refresh(s_refs.spectrum_chart);
    }
    lv_label_set_text_fmt(
        s_refs.spectrum_gain,
        "GAIN %+d dB",
        status.spectrum_gain_db);

    const RecordingStatus recording =
        board_get_recording_status();
    const char *record_state = "IDLE";
    lv_color_t record_color = kMuted;
    if (!recording.available) {
        record_state = "N/A";
        record_color = kRed;
    } else if (recording.stopping) {
        record_state = "STOPPING";
        record_color = kAmber;
    } else if (recording.recording) {
        record_state = "REC";
        record_color = kRed;
    } else if (recording.starting || s_record_start_pending) {
        record_state = "STARTING";
        record_color = kAmber;
    } else if (recording.error[0] != '\0') {
        record_state = "ERROR";
        record_color = kRed;
    }

    const uint32_t total_seconds = recording.duration_ms / 1000U;
    lv_label_set_text_fmt(
        s_refs.record_status,
        "%s | %02lu:%02lu | %llu KB | DROP %lu",
        record_state,
        static_cast<unsigned long>(total_seconds / 60U),
        static_cast<unsigned long>(total_seconds % 60U),
        static_cast<unsigned long long>(
            recording.data_bytes / 1024ULL),
        static_cast<unsigned long>(recording.dropped_chunks));
    set_label_color(
        s_refs.record_status,
        recording.dropped_chunks > 0 ? kAmber : record_color);

    lv_label_set_text(
        s_refs.record_button_label,
        recording.recording
            ? "RECORDING"
            : (recording.starting || s_record_start_pending
                   ? "STARTING"
                   : (recording.stopping ? "STOPPING" : "RECORD")));
    lv_obj_set_style_border_color(
        s_refs.record_button,
        recording.recording ? kRed
                            : (recording.starting ||
                                       recording.stopping ||
                                       s_record_start_pending
                                   ? kAmber
                                   : kRed),
        LV_PART_MAIN);

    if (recording.error[0] != '\0') {
        lv_label_set_text_fmt(
            s_refs.record_path, "ERROR: %s", recording.error);
        set_label_color(s_refs.record_path, kRed);
    } else {
        lv_label_set_text(
            s_refs.record_path,
            recording.path[0] != '\0'
                ? recording.path
                : "/recordings");
        set_label_color(
            s_refs.record_path,
            recording.recording ? kGreen : kMuted);
    }
}

void update_media(const BoardStatus &status)
{
    const MediaStatus media = media_engine_get_status();
    const RecordingStatus recording =
        board_get_recording_status();
    const bool recording_busy = recording_uses_tf(recording);
    const char *state_text = "OFFLINE";
    lv_color_t state_color = kRed;

    if (status.msc_active) {
        state_text = "USB DISK ACTIVE";
        state_color = kCyan;
    } else if (recording_busy) {
        state_text =
            recording.stopping
                ? "REC STOPPING"
                : (recording.recording ? "RECORDING"
                                       : "REC STARTING");
        state_color =
            recording.recording ? kRed : kAmber;
    } else if (s_msc_enable_pending) {
        state_text = "PREPARING USB";
        state_color = kAmber;
    } else if (s_media_rescan_pending) {
        state_text = "RESCANNING";
        state_color = kAmber;
    } else if (!status.sd_present || !media.sd_available) {
        state_text = "INSERT TF CARD";
        state_color = kRed;
    } else {
        switch (media.state) {
            case MediaState::Uninitialized:
                state_text = "OFFLINE";
                state_color = kRed;
                break;
            case MediaState::Idle:
                state_text = "TF READY";
                state_color = kGreen;
                break;
            case MediaState::Scanning:
                state_text = "SCANNING";
                state_color = kAmber;
                break;
            case MediaState::Decoding:
                state_text = "DECODING";
                state_color = kCyan;
                break;
            case MediaState::Playing:
                state_text = "PLAYING";
                state_color = kGreen;
                break;
            case MediaState::Ready:
                state_text = "FRAME READY";
                state_color = kGreen;
                break;
            case MediaState::Error:
                state_text = "ERROR";
                state_color = kRed;
                break;
        }
    }

    lv_label_set_text(s_refs.media_state, state_text);
    set_label_color(s_refs.media_state, state_color);
    lv_label_set_text_fmt(
        s_refs.media_detail,
        "JPG %u | MJPEG %u",
        static_cast<unsigned>(media.jpeg_file_count),
        static_cast<unsigned>(media.mjpeg_file_count));

    if (status.msc_active || s_msc_enable_pending) {
        lv_label_set_text(
            s_refs.media_metrics, "MEDIA LOCKED FOR USB DISK");
        lv_label_set_text(
            s_refs.media_path,
            status.msc_active
                ? "Eject on computer, then disable in STORAGE"
                : "Waiting for TF card readers to stop");
        set_label_color(s_refs.media_path, kAmber);
    } else if (recording_busy) {
        lv_label_set_text(
            s_refs.media_metrics, "MEDIA LOCKED FOR RECORDING");
        lv_label_set_text(
            s_refs.media_path,
            recording.error[0] != '\0'
                ? recording.error
                : (recording.path[0] != '\0'
                       ? recording.path
                       : "Preparing /recordings WAV file"));
        set_label_color(
            s_refs.media_path,
            recording.error[0] != '\0' ? kRed : kAmber);
    } else {
        if (media.active_kind == MediaKind::Mjpeg &&
            media.active_path[0] != '\0') {
            lv_label_set_text_fmt(
                s_refs.media_metrics,
                "FPS %.1f/%u | DECODE %lu ms",
                static_cast<double>(media.measured_fps_x10) / 10.0,
                static_cast<unsigned>(media.target_fps),
                static_cast<unsigned long>(media.last_decode_ms));
        } else {
            lv_label_set_text_fmt(
                s_refs.media_metrics,
                "PHOTO | DECODE %lu ms | %ux%u",
                static_cast<unsigned long>(media.last_decode_ms),
                static_cast<unsigned>(media.source_width),
                static_cast<unsigned>(media.source_height));
        }

        if (media.state == MediaState::Error &&
            media.last_error[0] != '\0') {
            lv_label_set_text_fmt(
                s_refs.media_path, "ERROR: %s", media.last_error);
            set_label_color(s_refs.media_path, kRed);
        } else {
            lv_label_set_text(
                s_refs.media_path,
                media.active_path[0] != '\0'
                    ? media.active_path
                    : "/pic + /mjpeg");
            set_label_color(s_refs.media_path, kMuted);
        }
    }

    if (s_media_overlay_status != nullptr) {
        if (media.state == MediaState::Error) {
            lv_label_set_text(
                s_media_overlay_status, "DECODE ERROR");
            if (media.last_error[0] != '\0') {
                lv_label_set_text(
                    s_media_overlay_path, media.last_error);
            }
        } else if (s_overlay_kind == MediaKind::Mjpeg) {
            lv_label_set_text_fmt(
                s_media_overlay_status,
                "VIDEO %.1f FPS  %lums",
                static_cast<double>(media.measured_fps_x10) / 10.0,
                static_cast<unsigned long>(media.last_decode_ms));
        } else {
            lv_label_set_text_fmt(
                s_media_overlay_status,
                "PHOTO  %lums",
                static_cast<unsigned long>(media.last_decode_ms));
        }
        set_label_color(
            s_media_overlay_status,
            media.state == MediaState::Error ? kRed : kText);
        if (media.state != MediaState::Error &&
            media.active_path[0] != '\0') {
            lv_label_set_text(
                s_media_overlay_path, media.active_path);
        }
    }
}

void update_actuators(const BoardStatus &status)
{
    lv_label_set_text(
        s_refs.actuator_haptic,
        status.haptic_ready ? "READY" : "CHECK");
    set_label_color(
        s_refs.actuator_haptic,
        status.haptic_ready ? kPurple : kRed);

    lv_label_set_text(
        s_refs.actuator_rgb,
        !status.rgb_ready
            ? "CHECK"
            : (status.rgb_chase_active ? "RUNNING" : "READY"));
    set_label_color(
        s_refs.actuator_rgb, status.rgb_ready ? kCyan : kRed);

    lv_label_set_text(
        s_refs.actuator_speaker,
        status.speaker_ready ? "READY" : "CHECK");
    set_label_color(
        s_refs.actuator_speaker,
        status.speaker_ready ? kAmber : kRed);

    lv_label_set_text_fmt(
        s_refs.brightness_value,
        "%u%%",
        static_cast<unsigned>(status.brightness));
    if (!lv_obj_has_state(
            s_refs.brightness_slider, LV_STATE_PRESSED)) {
        s_syncing_slider = true;
        lv_slider_set_value(
            s_refs.brightness_slider,
            status.brightness,
            LV_ANIM_OFF);
        s_syncing_slider = false;
    }
}

void update_storage(const BoardStatus &status)
{
    const RecordingStatus recording =
        board_get_recording_status();
    const bool recording_busy = recording_uses_tf(recording);

    if (status.msc_active) {
        lv_label_set_text(
            s_refs.storage_state, "USB DISK ACTIVE");
        set_label_color(s_refs.storage_state, kCyan);
    } else if (s_msc_enable_pending) {
        lv_label_set_text(
            s_refs.storage_state,
            recording_busy ? "FINALIZING WAV" : "PREPARING USB");
        set_label_color(s_refs.storage_state, kAmber);
    } else if (recording_busy) {
        lv_label_set_text(
            s_refs.storage_state,
            recording.stopping
                ? "REC STOPPING"
                : (recording.recording ? "RECORDING"
                                       : "REC STARTING"));
        set_label_color(
            s_refs.storage_state,
            recording.recording ? kRed : kAmber);
    } else {
        lv_label_set_text(
            s_refs.storage_state,
            status.sd_present ? "MOUNTED" : "NOT FOUND");
        set_label_color(
            s_refs.storage_state,
            status.sd_present ? kGreen : kRed);
    }

    const char *usb_button_text = "ENABLE USB";
    lv_color_t usb_color = kCyan;
    const char *safety_text =
        "USB disk shares the TF card.\n"
        "Eject it on the computer before closing.";
    if (!status.msc_compiled) {
        usb_button_text = "USB DISK N/A";
        usb_color = kMuted;
        safety_text =
            "USB disk is not compiled in this firmware.\n"
            "Use the dedicated MSC build when needed.";
    } else if (s_msc_enable_pending) {
        usb_button_text =
            recording_busy ? "FINALIZING REC" : "PREPARING...";
        usb_color = kAmber;
        safety_text = recording_busy
                          ? "Closing the WAV file and releasing TF.\n"
                            "USB disk starts only after finalization."
                          : "Stopping media and releasing the TF card.\n"
                            "Please wait before using it on the computer.";
    } else if (status.msc_active && !status.msc_ejected) {
        usb_button_text = "USB DISK ON";
        usb_color = kGreen;
        safety_text =
            "USB DISK ACTIVE - firmware access is locked.\n"
            "Eject it on the computer before closing.";
    } else if (status.msc_active) {
        usb_button_text = "DISCONNECT";
        usb_color = kPurple;
        safety_text =
            "Computer eject confirmed safe.\n"
            "Tap DISCONNECT to return TF to the demo.";
    } else if (recording_busy) {
        usb_button_text = "STOP REC + USB";
        usb_color = kAmber;
        safety_text =
            "Recording owns the TF card.\n"
            "USB start will stop and finalize it safely.";
    }
    lv_label_set_text(
        s_refs.storage_usb_label, usb_button_text);
    lv_obj_set_style_border_color(
        s_refs.storage_usb_button, usb_color, LV_PART_MAIN);
    set_label_color(s_refs.storage_usb_label, kText);
    lv_label_set_text(s_refs.storage_safety, safety_text);
    set_label_color(
        s_refs.storage_safety,
        status.msc_active && !status.msc_ejected ? kRed : kMuted);

    if (!status.sd_present || status.sd_total_bytes == 0) {
        lv_label_set_text(
            s_refs.storage_capacity,
            status.msc_active ? "PC" : "-- GB");
        lv_label_set_text(
            s_refs.storage_used,
            status.msc_active ? "Firmware TF access locked" : "Used --");
        lv_bar_set_value(s_refs.storage_bar, 0, LV_ANIM_OFF);
        return;
    }

    const double total_gb =
        static_cast<double>(status.sd_total_bytes) /
        (1024.0 * 1024.0 * 1024.0);
    const double used_gb =
        static_cast<double>(status.sd_used_bytes) /
        (1024.0 * 1024.0 * 1024.0);
    const int used_percent = constrain(
        static_cast<int>(
            status.sd_used_bytes * 100ULL / status.sd_total_bytes),
        0,
        100);
    lv_label_set_text_fmt(
        s_refs.storage_capacity, "%.1f GB", total_gb);
    lv_label_set_text_fmt(
        s_refs.storage_used,
        "Used %.1f GB  |  %d%%",
        used_gb,
        used_percent);
    lv_bar_set_value(
        s_refs.storage_bar, used_percent, LV_ANIM_OFF);
}

void update_wireless(const WirelessSnapshot &wireless)
{
    if (wireless.wifi_busy) {
        lv_label_set_text(s_refs.wireless_state, "Scanning Wi-Fi...");
    } else if (wireless.ble_busy) {
        lv_label_set_text(s_refs.wireless_state, "Scanning BLE...");
    } else if (wireless.wifi_done || wireless.ble_done) {
        lv_label_set_text_fmt(
            s_refs.wireless_state,
            "Found Wi-Fi %d  |  BLE %d",
            wireless.wifi_count,
            wireless.ble_count);
    } else {
        lv_label_set_text(
            s_refs.wireless_state, "Tap a scan button");
    }

    for (size_t i = 0; i < BOARD_DEMO_WIRELESS_ROWS; ++i) {
        lv_label_set_text(
            s_refs.wifi_rows[i],
            wireless.wifi_rows[i][0] != '\0'
                ? wireless.wifi_rows[i]
                : "--");
        lv_label_set_text(
            s_refs.ble_rows[i],
            wireless.ble_rows[i][0] != '\0'
                ? wireless.ble_rows[i]
                : "--");
    }
}

void update_system(const BoardStatus &status)
{
    const uint32_t seconds = millis() / 1000U;
    lv_label_set_text_fmt(
        s_refs.system_uptime,
        "%02u:%02u:%02u",
        static_cast<unsigned>(seconds / 3600U),
        static_cast<unsigned>((seconds / 60U) % 60U),
        static_cast<unsigned>(seconds % 60U));

    lv_label_set_text_fmt(
        s_refs.system_flash,
        "%u MB  %s",
        static_cast<unsigned>(status.flash_bytes / (1024U * 1024U)),
        status.flash_ok ? "OK" : "CHECK");
    set_label_color(
        s_refs.system_flash, status.flash_ok ? kCyan : kRed);

    lv_label_set_text_fmt(
        s_refs.system_psram,
        "%u MB  %s",
        static_cast<unsigned>(status.psram_bytes / (1024U * 1024U)),
        status.psram_ok ? "OK" : "CHECK");
    set_label_color(
        s_refs.system_psram, status.psram_ok ? kPurple : kRed);

    lv_label_set_text(
        s_refs.system_usb,
        status.msc_active
            ? "MSC ACTIVE"
            : (status.msc_compiled
                   ? "CDC + DISK"
                   : (status.hid_compiled
                          ? "CDC + HID"
                          : "CDC SERIAL")));
    lv_label_set_text(
        s_refs.system_input,
        status.touch_seen && status.knob_seen
            ? "BOTH PASS"
            : (status.touch_seen || status.knob_seen
                   ? "ONE SEEN"
                   : "TRY INPUTS"));
    set_label_color(
        s_refs.system_input,
        status.touch_seen && status.knob_seen ? kGreen : kAmber);

    lv_label_set_text(
        s_refs.system_hid,
        !status.hid_compiled
            ? "OTHER BUILD"
            : (status.hid_volume_enabled ? "ENABLED" : "DISABLED"));
    set_label_color(
        s_refs.system_hid,
        status.hid_volume_enabled ? kPurple : kMuted);
}

void update_current_page()
{
    const BoardStatus status = board_get_status();
    switch (s_page) {
        case Page::Overview:
            update_overview(status);
            break;
        case Page::Spectrum:
            update_spectrum(status, board_get_audio_snapshot());
            break;
        case Page::Media:
            update_media(status);
            break;
        case Page::Actuators:
            update_actuators(status);
            break;
        case Page::Storage:
            update_storage(status);
            break;
        case Page::Wireless:
            update_wireless(board_get_wireless_snapshot());
            break;
        case Page::System:
            update_system(status);
            break;
        case Page::Count:
            break;
    }
}

void handle_knob(int32_t delta)
{
    if (delta == 0) {
        return;
    }
    delta = constrain(delta, -8, 8);

    if (s_knob_mode == KnobMode::Page) {
        navigate_pages(static_cast<int>(delta));
        return;
    }

    if (s_knob_mode == KnobMode::Gain) {
        board_adjust_spectrum_gain(static_cast<int>(delta) * 2);
        update_current_page();
        return;
    }

    if (s_knob_mode == KnobMode::Brightness) {
        const BoardStatus status = board_get_status();
        const int next =
            constrain(
                static_cast<int>(status.brightness) +
                    static_cast<int>(delta) * 4,
                12,
                100);
        board_set_brightness(static_cast<uint8_t>(next));
        update_current_page();
        return;
    }

    const int steps = abs(static_cast<int>(delta));
    for (int i = 0; i < steps; ++i) {
        board_hid_volume_step(delta > 0 ? 1 : -1);
    }
    show_toast(delta > 0 ? "Volume +" : "Volume -");
}

void consume_media_frame()
{
    if (s_media_overlay == nullptr || s_media_image == nullptr) {
        return;
    }

    MediaFrame frame = {};
    if (!media_engine_take_frame(&frame)) {
        return;
    }

    // Discard a frame left by the previously selected file. The new worker
    // command will publish a strictly newer sequence.
    if (frame.sequence <= s_media_sequence_floor) {
        return;
    }
    if (frame.data == nullptr ||
        frame.width != MEDIA_ENGINE_FRAME_WIDTH ||
        frame.height != MEDIA_ENGINE_FRAME_HEIGHT ||
        frame.data_size < MEDIA_ENGINE_FRAME_BYTES) {
        show_toast("Invalid decoded media frame");
        return;
    }

    s_media_image_descriptor.header.always_zero = 0;
    s_media_image_descriptor.header.reserved = 0;
    s_media_image_descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_media_image_descriptor.header.w = frame.width;
    s_media_image_descriptor.header.h = frame.height;
    s_media_image_descriptor.data_size =
        static_cast<uint32_t>(frame.data_size);
    s_media_image_descriptor.data = frame.data;

    lv_img_set_src(s_media_image, &s_media_image_descriptor);
    lv_obj_set_pos(s_media_image, 0, 0);
    lv_obj_clear_flag(s_media_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_media_image);
}

void process_recording_transition(uint32_t now)
{
    if (!s_record_start_pending ||
        static_cast<int32_t>(now - s_record_transition_next_ms) < 0) {
        return;
    }

    const RecordingStatus recording =
        board_get_recording_status();
    if (recording.starting || recording.recording) {
        s_record_start_pending = false;
        s_record_start_command_sent = false;
        show_toast(
            recording.recording ? "Recording started"
                                : "Recorder is starting");
        return;
    }
    if (!recording.available) {
        s_record_start_pending = false;
        s_record_start_command_sent = false;
        show_toast(
            recording.error[0] != '\0'
                ? recording.error
                : "Recording is unavailable");
        return;
    }
    if (recording.error[0] != '\0' &&
        s_record_start_command_sent) {
        s_record_start_pending = false;
        s_record_start_command_sent = false;
        show_toast(recording.error);
        return;
    }

    const BoardStatus board = board_get_status();
    if (board.msc_active || s_msc_enable_pending) {
        s_record_start_pending = false;
        s_record_start_command_sent = false;
        show_toast("USB disk owns the TF card");
        return;
    }

    const MediaStatus media = media_engine_get_status();
    if (media.state == MediaState::Playing ||
        media.state == MediaState::Decoding ||
        media.state == MediaState::Scanning ||
        recording.stopping) {
        media_engine_stop();
        s_record_transition_next_ms = now + 120U;
    } else if (!s_record_start_command_sent) {
        board_recording_start();
        s_record_start_command_sent = true;
        s_record_transition_next_ms = now + 180U;
    } else {
        s_record_transition_next_ms = now + 180U;
    }

    if (now - s_record_transition_started_ms >= 7000U) {
        s_record_start_pending = false;
        s_record_start_command_sent = false;
        show_toast("Recording start timed out");
    }
}

void process_msc_transition(uint32_t now)
{
    if (!s_msc_enable_pending ||
        static_cast<int32_t>(now - s_msc_transition_next_ms) < 0) {
        return;
    }

    const BoardStatus board = board_get_status();
    if (board.msc_active) {
        s_msc_enable_pending = false;
        show_toast("USB disk is active");
        return;
    }
    if (!board.msc_compiled) {
        s_msc_enable_pending = false;
        show_toast("USB disk backend unavailable");
        return;
    }

    const RecordingStatus recording =
        board_get_recording_status();
    if (recording_uses_tf(recording)) {
        if (!recording.stopping) {
            board_recording_stop();
        }
        s_msc_transition_next_ms = now + 120U;
        if (now - s_msc_transition_started_ms >= 7000U) {
            s_msc_enable_pending = false;
            show_toast("Recorder did not release the TF card");
        }
        return;
    }

    const MediaStatus media = media_engine_get_status();
    if (media.state == MediaState::Playing ||
        media.state == MediaState::Decoding ||
        media.state == MediaState::Scanning) {
        media_engine_stop();
        s_msc_transition_next_ms = now + 120U;
    } else if (board_usb_msc_set_active(true)) {
        s_msc_enable_pending = false;
        show_toast("USB disk is active");
        return;
    } else {
        s_msc_transition_next_ms = now + 250U;
    }

    if (now - s_msc_transition_started_ms >= 7000U) {
        s_msc_enable_pending = false;
        show_toast("USB disk start timed out");
    }
}

void process_media_rescan(uint32_t now)
{
    if (!s_media_rescan_pending ||
        static_cast<int32_t>(now - s_media_scan_after_ms) < 0) {
        return;
    }

    const BoardStatus ownership = board_get_status();
    if (ownership.msc_active || s_msc_enable_pending) {
        s_media_scan_after_ms = now + 350U;
        return;
    }
    if (recording_uses_tf(board_get_recording_status())) {
        s_media_scan_after_ms = now + 350U;
        return;
    }

    const MediaStatus before = media_engine_get_status();
    if (!before.initialized) {
        s_media_rescan_pending = false;
        show_toast("Media engine unavailable");
        return;
    }

    if (before.state == MediaState::Playing ||
        before.state == MediaState::Decoding ||
        before.state == MediaState::Scanning) {
        media_engine_stop();
        s_media_scan_after_ms = now + 180U;
        return;
    }

    const bool scanned = media_engine_scan();
    const BoardStatus board = board_get_status();
    const MediaStatus after = media_engine_get_status();
    if (scanned || !board.sd_present) {
        s_media_rescan_pending = false;
        s_next_photo_index = 0;
        s_next_video_index = 0;
        show_toast(
            scanned ? "TF media list refreshed"
                    : "TF card not found");
        return;
    }

    if (now - s_media_rescan_started_ms >= 7000U) {
        s_media_rescan_pending = false;
        show_toast(
            after.last_error[0] != '\0'
                ? after.last_error
                : "TF media scan timed out");
        return;
    }
    s_media_scan_after_ms = now + 350U;
}

void init_styles()
{
    if (s_styles_ready) {
        return;
    }

    lv_style_init(&s_card_style);
    lv_style_set_bg_color(&s_card_style, kPanel);
    lv_style_set_bg_opa(&s_card_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_card_style, kBorder);
    lv_style_set_border_width(&s_card_style, 1);
    lv_style_set_radius(&s_card_style, 16);
    lv_style_set_pad_all(&s_card_style, 0);

    lv_style_init(&s_button_style);
    lv_style_set_bg_color(&s_button_style, kPanelRaised);
    lv_style_set_bg_opa(&s_button_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_button_style, kCyan);
    lv_style_set_border_width(&s_button_style, 1);
    lv_style_set_radius(&s_button_style, 15);
    lv_style_set_pad_all(&s_button_style, 0);
    lv_style_set_shadow_width(&s_button_style, 0);

    lv_style_init(&s_nav_style);
    lv_style_set_bg_color(&s_nav_style, kPanelRaised);
    lv_style_set_bg_opa(&s_nav_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_nav_style, kBorder);
    lv_style_set_border_width(&s_nav_style, 1);
    lv_style_set_radius(&s_nav_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&s_nav_style, 0);
    lv_style_set_shadow_width(&s_nav_style, 0);

    s_styles_ready = true;
}

}  // namespace

void app_ui_begin()
{
    if (s_root != nullptr) {
        return;
    }

    init_styles();
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, kBg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_root = lv_obj_create(screen);
    lv_obj_set_size(s_root, kScreenSize, kScreenSize);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_root, kBg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_root, kBorder, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_root, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        s_root,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
                board_mark_touch();
            }
        },
        LV_EVENT_PRESSED,
        nullptr);

    s_title =
        make_label(s_root, "OVERVIEW", &lv_font_montserrat_22, kText);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 8);

    s_page_index =
        make_label(s_root, "LIVE  1 / 7", &lv_font_montserrat_14, kGreen);
    lv_obj_align(s_page_index, LV_ALIGN_TOP_MID, 0, 36);

    s_content = lv_obj_create(s_root);
    set_plain_container(s_content);
    lv_obj_set_pos(s_content, kContentX, kContentY);
    lv_obj_set_size(s_content, kContentWidth, kContentHeight);

    lv_obj_t *previous = lv_btn_create(s_root);
    lv_obj_add_style(previous, &s_nav_style, LV_PART_MAIN);
    lv_obj_set_pos(previous, 80, 299);
    lv_obj_set_size(previous, 44, 44);
    lv_obj_set_style_bg_color(previous, kCyan, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(previous, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        previous,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
                board_mark_touch();
                navigate_pages(-1);
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_obj_t *previous_label =
        make_label(previous, "<", &lv_font_montserrat_22, kText);
    lv_obj_center(previous_label);

    s_mode_button = make_button(
        s_root, "PAGE", 153, 306, 54, 38, Action::ToggleKnobMode, kMuted);
    s_mode_label = lv_obj_get_child(s_mode_button, 0);

    lv_obj_t *next = lv_btn_create(s_root);
    lv_obj_add_style(next, &s_nav_style, LV_PART_MAIN);
    lv_obj_set_pos(next, 236, 299);
    lv_obj_set_size(next, 44, 44);
    lv_obj_set_style_bg_color(next, kCyan, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(next, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        next,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
                board_mark_touch();
                navigate_pages(1);
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_obj_t *next_label =
        make_label(next, ">", &lv_font_montserrat_22, kText);
    lv_obj_center(next_label);

    s_toast = lv_obj_create(s_root);
    lv_obj_set_pos(s_toast, 63, 262);
    lv_obj_set_size(s_toast, 234, 34);
    lv_obj_set_style_bg_color(s_toast, kText, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_toast, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_toast, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    s_toast_label =
        make_label(s_toast, "", &lv_font_montserrat_14, kBg);
    lv_obj_center(s_toast_label);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    render_page();
    s_last_refresh_ms = millis();
}

void app_ui_poll()
{
    if (s_root == nullptr) {
        return;
    }

    handle_knob(knob_take_delta());

    const uint32_t now = millis();
    process_recording_transition(now);
    process_msc_transition(now);
    process_media_rescan(now);
    consume_media_frame();

    if (s_toast_until_ms != 0 &&
        static_cast<int32_t>(now - s_toast_until_ms) >= 0) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toast_until_ms = 0;
    }

    if (now - s_last_refresh_ms >= kUiRefreshMs) {
        s_last_refresh_ms = now;
        update_current_page();
    }
}
