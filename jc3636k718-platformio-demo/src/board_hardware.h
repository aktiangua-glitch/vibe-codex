#ifndef JC3636K718_BOARD_HARDWARE_H
#define JC3636K718_BOARD_HARDWARE_H

#include <Arduino.h>
#include <stdint.h>

constexpr size_t BOARD_DEMO_SPECTRUM_BANDS = 24;
constexpr size_t BOARD_DEMO_WIRELESS_ROWS = 3;

struct AudioSnapshot {
    bool valid;
    float rms_db;
    float peak_db;
    uint16_t dominant_hz;
    uint32_t frame_count;
    float bands[BOARD_DEMO_SPECTRUM_BANDS];
    float peaks[BOARD_DEMO_SPECTRUM_BANDS];
};

struct RecordingStatus {
    bool available;
    bool starting;
    bool recording;
    bool stopping;
    uint32_t duration_ms;
    uint32_t data_bytes;
    uint32_t dropped_chunks;
    char path[64];
    char error[96];
};

struct WirelessSnapshot {
    bool wifi_busy;
    bool wifi_done;
    int wifi_count;
    char wifi_rows[BOARD_DEMO_WIRELESS_ROWS][40];
    bool ble_busy;
    bool ble_done;
    int ble_count;
    char ble_rows[BOARD_DEMO_WIRELESS_ROWS][40];
};

struct BoardStatus {
    bool flash_ok;
    bool psram_ok;
    bool touch_seen;
    bool knob_seen;
    bool mic_ready;
    bool speaker_ready;
    bool haptic_ready;
    bool rgb_ready;
    bool rgb_chase_active;
    bool sd_ready;
    bool sd_present;
    bool hid_compiled;
    bool hid_volume_enabled;
    bool msc_compiled;
    bool msc_active;
    bool msc_ejected;
    size_t flash_bytes;
    size_t psram_bytes;
    uint64_t sd_total_bytes;
    uint64_t sd_used_bytes;
    float power_volts;
    uint8_t brightness;
    uint8_t rgb_palette;
    int spectrum_gain_db;
};

void board_hardware_begin();
void board_hardware_poll();

BoardStatus board_get_status();
AudioSnapshot board_get_audio_snapshot();
RecordingStatus board_get_recording_status();
WirelessSnapshot board_get_wireless_snapshot();

void board_mark_touch();
void board_set_brightness(uint8_t value);
void board_adjust_spectrum_gain(int delta_db);

bool board_play_haptic(uint8_t pattern);
// Toggles a non-blocking per-pixel chase animation on the 13-LED ring.
// Returns true while the animation is running.
bool board_rgb_toggle_chase();
bool board_play_speaker_test();

// Records the existing 24 kHz / 16-bit / mono PDM stream as a WAV file in
// /recordings on the TF card. Writing is asynchronous; poll the status to learn
// when STARTING/STOPPING has completed.
bool board_recording_start();
bool board_recording_stop();

void board_request_sd_rescan();
// All TF-card readers share this mutex. A reader that keeps an open File must
// hold the lock until the File is closed so a rescan cannot remount underneath it.
// The lock is intentionally unavailable while USB MSC owns the card.
bool board_sd_lock(uint32_t timeout_ms);
void board_sd_unlock();
// USB MSC and the firmware filesystem are mutually exclusive. Disabling an
// active MSC session is accepted only after the computer has ejected the disk.
bool board_usb_msc_set_active(bool active);
void board_request_wifi_scan();
void board_request_ble_scan();

bool board_hid_available();
bool board_hid_volume_enabled();
void board_set_hid_volume_enabled(bool enabled);
void board_hid_volume_step(int direction);

#endif
