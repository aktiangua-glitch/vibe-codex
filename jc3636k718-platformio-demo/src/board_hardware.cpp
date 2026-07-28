#include "board_hardware.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <ESP_I2S.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <driver/i2c.h>
#include <esp32-hal-rmt.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "display_port.h"
#include "knob.h"
#include "pincfg.h"

#ifndef BOARD_DEMO_USB_HID
#define BOARD_DEMO_USB_HID 0
#endif

#ifndef BOARD_DEMO_USB_MSC
#define BOARD_DEMO_USB_MSC 0
#endif

#if BOARD_DEMO_USB_HID && BOARD_DEMO_USB_MSC
#error USB HID and USB MSC are intentionally separate build modes
#endif

#if BOARD_DEMO_USB_MSC && ARDUINO_USB_MODE
#error USB MSC requires native TinyUSB mode (ARDUINO_USB_MODE=0)
#endif

#if BOARD_DEMO_USB_HID && !ARDUINO_USB_MODE
#include <USBHIDConsumerControl.h>
static USBHIDConsumerControl s_consumer_control;
#endif

#if BOARD_DEMO_USB_MSC && !ARDUINO_USB_MODE
#include <USBMSC.h>
static USBMSC s_usb_msc;
#endif

namespace {

constexpr uint32_t kAudioSampleRate = 24000;
constexpr size_t kFftSize = 512;
constexpr size_t kRecordingChunkBytes = kFftSize * sizeof(int16_t);
constexpr UBaseType_t kRecordingQueueDepth = 10;
constexpr uint32_t kRecordingBytesPerSecond =
    kAudioSampleRate * sizeof(int16_t);
constexpr uint32_t kRecordingCheckpointBytes =
    kRecordingBytesPerSecond;
constexpr uint32_t kMaxWavDataBytes = 0xFFFF0000UL;
constexpr char kRecordingDirectory[] = "/recordings";
constexpr float kPi = 3.14159265358979323846f;
constexpr uint8_t kDrv2605Address = HAPTIC_I2C_ADDRESS;
// The vendor firmware configures the base-board light ring as 13 WS2812 LEDs.
constexpr size_t kRgbPixelsToSend = 13;
constexpr uint32_t kRgbChaseFrameMs = 75;
constexpr size_t kRgbChaseTailPixels = 3;

constexpr uint8_t kDrvRegStatus = 0x00;
constexpr uint8_t kDrvRegMode = 0x01;
constexpr uint8_t kDrvRegRtp = 0x02;
constexpr uint8_t kDrvRegLibrary = 0x03;
constexpr uint8_t kDrvRegWaveSeq1 = 0x04;
constexpr uint8_t kDrvRegWaveSeq2 = 0x05;
constexpr uint8_t kDrvRegGo = 0x0C;
constexpr uint8_t kDrvRegFeedback = 0x1A;
constexpr uint8_t kDrvRegControl1 = 0x1B;
constexpr uint8_t kDrvRegControl3 = 0x1D;

portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_audio_mux = portMUX_INITIALIZER_UNLOCKED;

BoardStatus s_status = {};
AudioSnapshot s_audio = {};
RecordingStatus s_recording = {};
WirelessSnapshot s_wireless = {};

I2SClass s_mic_i2s;
I2SClass s_speaker_i2s;
TaskHandle_t s_audio_task_handle = nullptr;
TaskHandle_t s_recording_task_handle = nullptr;
QueueHandle_t s_recording_queue = nullptr;
volatile bool s_speaker_task_busy = false;
volatile bool s_ble_task_busy = false;
bool s_ble_initialized = false;
SemaphoreHandle_t s_sd_mutex = nullptr;

bool s_sd_rescan_requested = false;
bool s_msc_transitioning = false;
bool s_recording_backend_ready = false;
bool s_recording_start_requested = false;
bool s_recording_stop_requested = false;
uint32_t s_recording_capture_inflight = 0;
bool s_wifi_scan_requested = false;
bool s_ble_scan_requested = false;
bool s_rgb_driver_started = false;
bool s_rgb_chase_active = false;
uint8_t s_rgb_chase_head = 0;
uint8_t s_rgb_chase_lap = 0;
uint32_t s_last_rgb_frame_ms = 0;
uint32_t s_last_power_read_ms = 0;

float s_fft_real[kFftSize];
float s_fft_imag[kFftSize];
float s_fft_magnitude[kFftSize / 2];
float s_hann_window[kFftSize];
int16_t s_pcm[kFftSize];
uint16_t s_band_first_bin[BOARD_DEMO_SPECTRUM_BANDS];
uint16_t s_band_last_bin[BOARD_DEMO_SPECTRUM_BANDS];
float s_smoothed_bands[BOARD_DEMO_SPECTRUM_BANDS] = {};
float s_band_peaks[BOARD_DEMO_SPECTRUM_BANDS] = {};
rmt_data_t s_rgb_symbols[kRgbPixelsToSend * 24];

struct RgbPixel {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

RgbPixel s_rgb_frame[kRgbPixelsToSend] = {};

#if BOARD_DEMO_USB_MSC && !ARDUINO_USB_MODE
constexpr uint32_t kMscLockTimeoutMs = 250;
constexpr size_t kMscSectorBufferBytes = 512;
alignas(4) uint8_t s_msc_sector_buffer[kMscSectorBufferBytes];
uint32_t s_msc_sector_count = 0;
uint16_t s_msc_sector_size = 0;
#endif

const uint8_t kRgbPalette[][3] = {
    {0, 10, 18},
    {16, 3, 20},
    {20, 7, 0},
    {0, 20, 8},
    {18, 18, 18},
    {0, 0, 0},
};

const uint8_t kRgbChaseColors[][3] = {
    {0, 30, 42},
    {34, 4, 42},
    {42, 13, 0},
    {0, 38, 12},
};

bool sd_mutex_take_internal(uint32_t timeout_ms)
{
    return s_sd_mutex != nullptr &&
           xSemaphoreTake(
               s_sd_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sd_mutex_give_internal()
{
    if (s_sd_mutex != nullptr) {
        xSemaphoreGive(s_sd_mutex);
    }
}

bool msc_blocks_firmware_sd()
{
    bool blocked = false;
    portENTER_CRITICAL(&s_state_mux);
    blocked = s_status.msc_active || s_msc_transitioning;
    portEXIT_CRITICAL(&s_state_mux);
    return blocked;
}

bool drv_write(uint8_t reg, uint8_t value)
{
    const uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(
               static_cast<i2c_port_t>(TOUCH_I2C_PORT),
               kDrv2605Address,
               payload,
               sizeof(payload),
               pdMS_TO_TICKS(40)) == ESP_OK;
}

bool drv_read(uint8_t reg, uint8_t &value)
{
    return i2c_master_write_read_device(
               static_cast<i2c_port_t>(TOUCH_I2C_PORT),
               kDrv2605Address,
               &reg,
               1,
               &value,
               1,
               pdMS_TO_TICKS(40)) == ESP_OK;
}

bool haptic_begin()
{
    uint8_t status = 0;
    if (!drv_read(kDrvRegStatus, status)) {
        Serial.println("[HAPTIC] DRV2605L not found at 0x5A");
        return false;
    }

    bool ok = true;
    ok &= drv_write(kDrvRegMode, 0x00);
    ok &= drv_write(kDrvRegRtp, 0x00);
    ok &= drv_write(kDrvRegWaveSeq1, 0x01);
    ok &= drv_write(kDrvRegWaveSeq2, 0x00);
    ok &= drv_write(kDrvRegLibrary, 0x06);  // LRA effect library
    ok &= drv_write(kDrvRegFeedback, 0xB6);
    ok &= drv_write(kDrvRegControl1, 0x93);
    ok &= drv_write(kDrvRegControl3, 0xA0);

    Serial.printf("[HAPTIC] DRV2605L status=0x%02X, init=%s\n", status, ok ? "PASS" : "FAIL");
    return ok;
}

void rgb_encode_byte(uint8_t byte, size_t &symbol_index)
{
    for (int bit = 7; bit >= 0; --bit) {
        const bool one = byte & (1U << bit);
        rmt_data_t &symbol = s_rgb_symbols[symbol_index++];
        symbol.level0 = 1;
        symbol.duration0 = one ? 8 : 4;
        symbol.level1 = 0;
        symbol.duration1 = one ? 4 : 8;
    }
}

bool rgb_write_pixels(const RgbPixel *pixels)
{
    if (!s_rgb_driver_started) {
        s_rgb_driver_started = rmtInit(
            RGB_DATA_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
        if (!s_rgb_driver_started) {
            return false;
        }
        rmtSetEOT(RGB_DATA_PIN, LOW);
    }

    size_t index = 0;
    for (size_t pixel = 0; pixel < kRgbPixelsToSend; ++pixel) {
        rgb_encode_byte(pixels[pixel].green, index);  // WS2812 GRB order
        rgb_encode_byte(pixels[pixel].red, index);
        rgb_encode_byte(pixels[pixel].blue, index);
    }
    return rmtWrite(RGB_DATA_PIN, s_rgb_symbols, index, 50);
}

bool rgb_show(uint8_t red, uint8_t green, uint8_t blue)
{
    for (RgbPixel &pixel : s_rgb_frame) {
        pixel = {red, green, blue};
    }
    return rgb_write_pixels(s_rgb_frame);
}

bool rgb_show_chase_frame()
{
    memset(s_rgb_frame, 0, sizeof(s_rgb_frame));
    const uint8_t *color =
        kRgbChaseColors[
            s_rgb_chase_lap %
            (sizeof(kRgbChaseColors) / sizeof(kRgbChaseColors[0]))];
    static const uint8_t divisors[kRgbChaseTailPixels] = {1, 3, 8};

    for (size_t tail = 0; tail < kRgbChaseTailPixels; ++tail) {
        const size_t pixel =
            (static_cast<size_t>(s_rgb_chase_head) +
             kRgbPixelsToSend - tail) %
            kRgbPixelsToSend;
        s_rgb_frame[pixel] = {
            static_cast<uint8_t>(color[0] / divisors[tail]),
            static_cast<uint8_t>(color[1] / divisors[tail]),
            static_cast<uint8_t>(color[2] / divisors[tail]),
        };
    }
    return rgb_write_pixels(s_rgb_frame);
}

void rgb_chase_poll(uint32_t now)
{
    if (!s_rgb_chase_active ||
        (s_last_rgb_frame_ms != 0 &&
         now - s_last_rgb_frame_ms < kRgbChaseFrameMs)) {
        return;
    }

    s_last_rgb_frame_ms = now;
    const bool ok = rgb_show_chase_frame();
    if (ok) {
        ++s_rgb_chase_head;
        if (s_rgb_chase_head >= kRgbPixelsToSend) {
            s_rgb_chase_head = 0;
            ++s_rgb_chase_lap;
        }
    } else {
        s_rgb_chase_active = false;
    }

    portENTER_CRITICAL(&s_state_mux);
    s_status.rgb_ready = ok;
    s_status.rgb_chase_active = s_rgb_chase_active;
    portEXIT_CRITICAL(&s_state_mux);
}

bool sd_mount_locked()
{
    SD_MMC.end();
    if (!SD_MMC.setPins(
            SD_MMC_CLK_PIN,
            SD_MMC_CMD_PIN,
            SD_MMC_D0_PIN,
            SD_MMC_D1_PIN,
            SD_MMC_D2_PIN,
            SD_MMC_D3_PIN)) {
        Serial.println("[SD] Failed to assign SD_MMC pins");
        return false;
    }

    if (!SD_MMC.begin("/sdcard", false, false)) {
        Serial.println("[SD] No card or mount failed");
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[SD] No card inserted");
        return false;
    }

    // FAT free-space queries can be comparatively expensive on a large card.
    // Read each value once and reuse it for both status and diagnostics.
    const uint64_t total_bytes = SD_MMC.totalBytes();
    const uint64_t used_bytes = SD_MMC.usedBytes();
    portENTER_CRITICAL(&s_state_mux);
    s_status.sd_total_bytes = total_bytes;
    s_status.sd_used_bytes = used_bytes;
    portEXIT_CRITICAL(&s_state_mux);
    Serial.printf(
        "[SD] Mounted: total=%llu MB, used=%llu MB\n",
        total_bytes / (1024ULL * 1024ULL),
        used_bytes / (1024ULL * 1024ULL));
    return true;
}

#if BOARD_DEMO_USB_MSC && !ARDUINO_USB_MODE
bool msc_media_online()
{
    bool online = false;
    portENTER_CRITICAL(&s_state_mux);
    online = s_status.msc_active && !s_status.msc_ejected;
    portEXIT_CRITICAL(&s_state_mux);
    return online;
}

bool msc_transfer_raw(
    uint32_t lba,
    uint32_t offset,
    uint8_t *buffer,
    uint32_t bufsize,
    bool write)
{
    // These physical-media values were captured before MSC ownership began.
    // Do not call numSectors() here: in this core it consults FATFS metadata,
    // which must remain completely idle while the host owns the raw disk.
    const uint16_t sector_size_value = s_msc_sector_size;
    const uint32_t sector_count_value = s_msc_sector_count;
    if (sector_size_value == 0 ||
        sector_count_value == 0 ||
        static_cast<size_t>(sector_size_value) >
            sizeof(s_msc_sector_buffer)) {
        return false;
    }

    const uint32_t sector_size =
        static_cast<uint32_t>(sector_size_value);
    const uint64_t capacity =
        static_cast<uint64_t>(sector_count_value) * sector_size;
    const uint64_t first_byte =
        static_cast<uint64_t>(lba) * sector_size + offset;
    if (first_byte > capacity ||
        static_cast<uint64_t>(bufsize) > capacity - first_byte) {
        return false;
    }

    uint64_t position = first_byte;
    uint32_t remaining = bufsize;
    uint8_t *cursor = buffer;
    while (remaining > 0) {
        const uint32_t sector =
            static_cast<uint32_t>(position / sector_size);
        const uint32_t sector_offset =
            static_cast<uint32_t>(position % sector_size);
        const uint32_t available = sector_size - sector_offset;
        const uint32_t chunk =
            remaining < available ? remaining : available;

        if (write) {
            if ((sector_offset != 0 || chunk != sector_size) &&
                !SD_MMC.readRAW(s_msc_sector_buffer, sector)) {
                return false;
            }
            memcpy(
                s_msc_sector_buffer + sector_offset,
                cursor,
                chunk);
            if (!SD_MMC.writeRAW(s_msc_sector_buffer, sector)) {
                return false;
            }
        } else {
            if (!SD_MMC.readRAW(s_msc_sector_buffer, sector)) {
                return false;
            }
            memcpy(
                cursor,
                s_msc_sector_buffer + sector_offset,
                chunk);
        }

        cursor += chunk;
        remaining -= chunk;
        position += chunk;
    }
    return true;
}

int32_t msc_on_read(
    uint32_t lba,
    uint32_t offset,
    void *buffer,
    uint32_t bufsize)
{
    if ((buffer == nullptr && bufsize != 0) || !msc_media_online()) {
        return -1;
    }
    if (bufsize == 0) {
        return 0;
    }
    if (!sd_mutex_take_internal(kMscLockTimeoutMs)) {
        return -1;
    }

    const bool ok =
        msc_media_online() &&
        msc_transfer_raw(
            lba,
            offset,
            static_cast<uint8_t *>(buffer),
            bufsize,
            false);
    sd_mutex_give_internal();
    return ok ? static_cast<int32_t>(bufsize) : -1;
}

int32_t msc_on_write(
    uint32_t lba,
    uint32_t offset,
    uint8_t *buffer,
    uint32_t bufsize)
{
    if ((buffer == nullptr && bufsize != 0) || !msc_media_online()) {
        return -1;
    }
    if (bufsize == 0) {
        return 0;
    }
    if (!sd_mutex_take_internal(kMscLockTimeoutMs)) {
        return -1;
    }

    const bool ok =
        msc_media_online() &&
        msc_transfer_raw(lba, offset, buffer, bufsize, true);
    sd_mutex_give_internal();
    return ok ? static_cast<int32_t>(bufsize) : -1;
}

bool msc_on_start_stop(
    uint8_t power_condition,
    bool start,
    bool load_eject)
{
    (void)power_condition;
    bool active = false;
    bool set_present = false;
    bool present = false;

    portENTER_CRITICAL(&s_state_mux);
    active = s_status.msc_active && !s_msc_transitioning;
    if (active && !start && load_eject) {
        s_status.msc_ejected = true;
        set_present = true;
        present = false;
    } else if (active && start) {
        s_status.msc_ejected = false;
        set_present = true;
        present = true;
    }
    portEXIT_CRITICAL(&s_state_mux);

    if (set_present) {
        s_usb_msc.mediaPresent(present);
    }
    if (active && !start && load_eject) {
        Serial.println("[MSC] Host safely ejected the TF card");
    }
    return active;
}

void msc_configure()
{
    s_msc_sector_count = 0;
    s_msc_sector_size = 0;
    s_usb_msc.vendorID("JC3636");
    s_usb_msc.productID("TF USB DISK");
    s_usb_msc.productRevision("1.0");
    s_usb_msc.onRead(msc_on_read);
    s_usb_msc.onWrite(msc_on_write);
    s_usb_msc.onStartStop(msc_on_start_stop);
    s_usb_msc.isWritable(true);
    // The computer must never see the medium until the firmware has
    // exclusively acquired the shared TF-card mutex.
    s_usb_msc.mediaPresent(false);
}
#endif

void copy_recording_text(
    char *destination,
    size_t destination_size,
    const char *source)
{
    if (destination_size == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

void wav_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void wav_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFUL);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFUL);
    destination[2] = static_cast<uint8_t>((value >> 16) & 0xFFUL);
    destination[3] = static_cast<uint8_t>((value >> 24) & 0xFFUL);
}

void build_wav_header(uint8_t *header, uint32_t data_bytes)
{
    memcpy(header, "RIFF", 4);
    wav_put_u32(header + 4, 36U + data_bytes);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    wav_put_u32(header + 16, 16);
    wav_put_u16(header + 20, 1);  // PCM
    wav_put_u16(header + 22, 1);  // mono
    wav_put_u32(header + 24, kAudioSampleRate);
    wav_put_u32(header + 28, kRecordingBytesPerSecond);
    wav_put_u16(header + 32, sizeof(int16_t));
    wav_put_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    wav_put_u32(header + 40, data_bytes);
}

bool write_wav_header(File &file, uint32_t data_bytes)
{
    uint8_t header[44];
    build_wav_header(header, data_bytes);
    return file.seek(0) &&
           file.write(header, sizeof(header)) == sizeof(header);
}

bool checkpoint_wav(
    File &file,
    uint32_t data_bytes,
    bool resume_at_end)
{
    // First commit the audio payload, then publish its new length. A previous
    // checkpoint remains usable if power disappears before this one completes.
    file.flush();
    if (!write_wav_header(file, data_bytes)) {
        return false;
    }
    file.flush();
    return !resume_at_end ||
           file.seek(static_cast<uint32_t>(44U + data_bytes));
}

bool choose_recording_path(char *path, size_t path_size)
{
    if (!SD_MMC.exists(kRecordingDirectory) &&
        !SD_MMC.mkdir(kRecordingDirectory)) {
        return false;
    }

    for (uint32_t index = 1; index <= 9999; ++index) {
        snprintf(
            path,
            path_size,
            "%s/REC_%04lu.WAV",
            kRecordingDirectory,
            static_cast<unsigned long>(index));
        if (!SD_MMC.exists(path)) {
            return true;
        }
    }
    path[0] = '\0';
    return false;
}

void recording_set_start_failure(const char *message)
{
    portENTER_CRITICAL(&s_state_mux);
    s_recording_start_requested = false;
    s_recording_stop_requested = false;
    s_recording.starting = false;
    s_recording.recording = false;
    s_recording.stopping = false;
    copy_recording_text(
        s_recording.error,
        sizeof(s_recording.error),
        message);
    portEXIT_CRITICAL(&s_state_mux);
    Serial.printf("[REC] Start failed: %s\n", message);
}

void recording_mark_stopping(const char *error)
{
    portENTER_CRITICAL(&s_state_mux);
    s_recording.recording = false;
    s_recording.stopping = true;
    s_recording_stop_requested = true;
    if (error != nullptr && error[0] != '\0') {
        copy_recording_text(
            s_recording.error,
            sizeof(s_recording.error),
            error);
    }
    portEXIT_CRITICAL(&s_state_mux);
}

void recording_writer_task(void *)
{
    File file;
    bool session_active = false;
    bool sd_locked = false;
    bool discard_pending_audio = false;
    uint32_t data_bytes = 0;
    uint32_t next_checkpoint = kRecordingCheckpointBytes;
    alignas(4) uint8_t chunk[kRecordingChunkBytes];
    char active_path[sizeof(s_recording.path)] = {};

    for (;;) {
        if (!session_active) {
            bool start_requested = false;
            portENTER_CRITICAL(&s_state_mux);
            if (s_recording_start_requested) {
                s_recording_start_requested = false;
                start_requested = true;
            } else if (s_recording_stop_requested) {
                // STOP can overtake START before this worker wakes up.
                s_recording_stop_requested = false;
                s_recording.starting = false;
                s_recording.recording = false;
                s_recording.stopping = false;
            }
            portEXIT_CRITICAL(&s_state_mux);

            if (!start_requested) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            if (!board_sd_lock(2000)) {
                recording_set_start_failure("TF card is busy");
                continue;
            }
            sd_locked = true;

            bool cancelled = false;
            portENTER_CRITICAL(&s_state_mux);
            cancelled = s_recording_stop_requested;
            if (cancelled) {
                s_recording_stop_requested = false;
                s_recording.starting = false;
                s_recording.recording = false;
                s_recording.stopping = false;
            }
            portEXIT_CRITICAL(&s_state_mux);
            if (cancelled) {
                board_sd_unlock();
                sd_locked = false;
                continue;
            }

            active_path[0] = '\0';
            bool created_file = false;
            const char *start_error = nullptr;
            if (SD_MMC.cardType() == CARD_NONE) {
                start_error = "TF card was removed";
            } else if (!choose_recording_path(
                           active_path,
                           sizeof(active_path))) {
                start_error = "Cannot create /recordings";
            } else {
                file = SD_MMC.open(active_path, FILE_WRITE);
                created_file = static_cast<bool>(file);
                if (!created_file) {
                    start_error = "Cannot create WAV file";
                } else if (!write_wav_header(file, 0)) {
                    start_error = "Cannot write WAV header";
                } else {
                    file.flush();
                }
            }

            if (start_error != nullptr) {
                if (file) {
                    file.close();
                }
                if (created_file && active_path[0] != '\0') {
                    SD_MMC.remove(active_path);
                }
                board_sd_unlock();
                sd_locked = false;
                recording_set_start_failure(start_error);
                continue;
            }

            xQueueReset(s_recording_queue);
            portENTER_CRITICAL(&s_state_mux);
            cancelled = s_recording_stop_requested;
            if (cancelled) {
                s_recording_stop_requested = false;
                s_recording.starting = false;
                s_recording.recording = false;
                s_recording.stopping = false;
            } else {
                s_recording.starting = false;
                s_recording.recording = true;
                s_recording.stopping = false;
                s_recording.duration_ms = 0;
                s_recording.data_bytes = 0;
                copy_recording_text(
                    s_recording.path,
                    sizeof(s_recording.path),
                    active_path);
            }
            portEXIT_CRITICAL(&s_state_mux);

            if (cancelled) {
                file.close();
                SD_MMC.remove(active_path);
                board_sd_unlock();
                sd_locked = false;
                continue;
            }

            data_bytes = 0;
            next_checkpoint = kRecordingCheckpointBytes;
            discard_pending_audio = false;
            session_active = true;
            Serial.printf(
                "[REC] Recording %s at %lu Hz, 16-bit mono\n",
                active_path,
                static_cast<unsigned long>(kAudioSampleRate));
            continue;
        }

        bool stopping = false;
        portENTER_CRITICAL(&s_state_mux);
        stopping =
            s_recording_stop_requested ||
            s_recording.stopping ||
            !s_recording.recording;
        portEXIT_CRITICAL(&s_state_mux);
        bool received = false;
        if (!discard_pending_audio) {
            const TickType_t wait_ticks =
                pdMS_TO_TICKS(stopping ? 30 : 100);
            received =
                xQueueReceive(
                    s_recording_queue,
                    chunk,
                    wait_ticks) == pdTRUE;
        }

        if (received) {
            if (data_bytes >
                kMaxWavDataBytes -
                    static_cast<uint32_t>(sizeof(chunk))) {
                recording_mark_stopping(nullptr);
                stopping = true;
                discard_pending_audio = true;
            } else {
                const size_t written =
                    file.write(chunk, sizeof(chunk));
                if (written != sizeof(chunk)) {
                    recording_mark_stopping("TF write failed");
                    stopping = true;
                    discard_pending_audio = true;
                } else {
                    data_bytes += static_cast<uint32_t>(written);
                    portENTER_CRITICAL(&s_state_mux);
                    s_recording.data_bytes = data_bytes;
                    s_recording.duration_ms =
                        static_cast<uint32_t>(
                            (static_cast<uint64_t>(data_bytes) * 1000ULL) /
                            kRecordingBytesPerSecond);
                    portEXIT_CRITICAL(&s_state_mux);

                    if (data_bytes >= next_checkpoint) {
                        if (!checkpoint_wav(file, data_bytes, true)) {
                            recording_mark_stopping(
                                "TF checkpoint failed");
                            stopping = true;
                            discard_pending_audio = true;
                        } else {
                            next_checkpoint =
                                data_bytes + kRecordingCheckpointBytes;
                        }
                    }
                }
            }
        }

        bool capture_closed = false;
        uint32_t capture_inflight = 0;
        portENTER_CRITICAL(&s_state_mux);
        stopping =
            stopping ||
            s_recording_stop_requested ||
            s_recording.stopping ||
            !s_recording.recording;
        capture_closed = !s_recording.recording;
        capture_inflight = s_recording_capture_inflight;
        portEXIT_CRITICAL(&s_state_mux);

        if (!stopping ||
            !capture_closed ||
            capture_inflight != 0) {
            continue;
        }

        // No producer may begin after capture_closed becomes true, and every
        // producer that passed the gate remains counted until xQueueSend has
        // returned. It is therefore safe to drain normally, or to reset after
        // a fatal error, only once the in-flight count reaches zero.
        if (discard_pending_audio) {
            xQueueReset(s_recording_queue);
        }
        if (uxQueueMessagesWaiting(s_recording_queue) != 0) {
            continue;
        }

        const bool final_header_ok =
            checkpoint_wav(file, data_bytes, false);
        file.close();

        uint64_t used_bytes = 0;
        if (SD_MMC.cardType() != CARD_NONE) {
            used_bytes = SD_MMC.usedBytes();
        }
        if (sd_locked) {
            board_sd_unlock();
            sd_locked = false;
        }

        portENTER_CRITICAL(&s_state_mux);
        s_recording_start_requested = false;
        s_recording_stop_requested = false;
        s_recording.starting = false;
        s_recording.recording = false;
        s_recording.stopping = false;
        s_recording.data_bytes = data_bytes;
        s_recording.duration_ms =
            static_cast<uint32_t>(
                (static_cast<uint64_t>(data_bytes) * 1000ULL) /
                kRecordingBytesPerSecond);
        if (!final_header_ok && s_recording.error[0] == '\0') {
            copy_recording_text(
                s_recording.error,
                sizeof(s_recording.error),
                "Cannot finalize WAV header");
        }
        if (used_bytes != 0) {
            s_status.sd_used_bytes = used_bytes;
        }
        portEXIT_CRITICAL(&s_state_mux);

        Serial.printf(
            "[REC] Saved %s (%lu bytes, %lu ms, dropped=%lu)%s\n",
            active_path,
            static_cast<unsigned long>(data_bytes),
            static_cast<unsigned long>(
                (static_cast<uint64_t>(data_bytes) * 1000ULL) /
                kRecordingBytesPerSecond),
            static_cast<unsigned long>(
                board_get_recording_status().dropped_chunks),
            final_header_ok ? "" : " [header error]");
        session_active = false;
        discard_pending_audio = false;
        active_path[0] = '\0';
    }
}

void fft_in_place(float *real, float *imag, size_t length)
{
    for (size_t i = 1, j = 0; i < length; ++i) {
        size_t bit = length >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            const float tmp_r = real[i];
            const float tmp_i = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = tmp_r;
            imag[j] = tmp_i;
        }
    }

    for (size_t len = 2; len <= length; len <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(len);
        const float w_len_r = cosf(angle);
        const float w_len_i = sinf(angle);
        for (size_t offset = 0; offset < length; offset += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (size_t j = 0; j < len / 2; ++j) {
                const size_t even = offset + j;
                const size_t odd = even + len / 2;
                const float odd_r = real[odd] * w_r - imag[odd] * w_i;
                const float odd_i = real[odd] * w_i + imag[odd] * w_r;
                real[odd] = real[even] - odd_r;
                imag[odd] = imag[even] - odd_i;
                real[even] += odd_r;
                imag[even] += odd_i;
                const float next_w_r = w_r * w_len_r - w_i * w_len_i;
                w_i = w_r * w_len_i + w_i * w_len_r;
                w_r = next_w_r;
            }
        }
    }
}

void prepare_spectrum_tables()
{
    for (size_t i = 0; i < kFftSize; ++i) {
        s_hann_window[i] =
            0.5f -
            0.5f * cosf(
                       2.0f * kPi * static_cast<float>(i) /
                       static_cast<float>(kFftSize - 1));
    }

    for (size_t band = 0; band < BOARD_DEMO_SPECTRUM_BANDS; ++band) {
        const float start_ratio =
            static_cast<float>(band) / BOARD_DEMO_SPECTRUM_BANDS;
        const float end_ratio =
            static_cast<float>(band + 1) / BOARD_DEMO_SPECTRUM_BANDS;
        const float start_hz =
            70.0f * powf(10000.0f / 70.0f, start_ratio);
        const float end_hz =
            70.0f * powf(10000.0f / 70.0f, end_ratio);
        size_t first_bin = static_cast<size_t>(
            ceilf(start_hz * kFftSize / kAudioSampleRate));
        size_t last_bin = static_cast<size_t>(
            floorf(end_hz * kFftSize / kAudioSampleRate));
        if (first_bin < 1) {
            first_bin = 1;
        }
        if (last_bin > kFftSize / 2 - 1) {
            last_bin = kFftSize / 2 - 1;
        }
        if (last_bin < first_bin) {
            last_bin = first_bin;
        }
        s_band_first_bin[band] = static_cast<uint16_t>(first_bin);
        s_band_last_bin[band] = static_cast<uint16_t>(last_bin);
    }
}

void audio_task(void *)
{
    uint32_t frames = 0;
    for (;;) {
        const size_t bytes = s_mic_i2s.readBytes(
            reinterpret_cast<char *>(s_pcm), sizeof(s_pcm));
        if (bytes != sizeof(s_pcm)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        bool record_this_chunk = false;
        portENTER_CRITICAL(&s_state_mux);
        record_this_chunk =
            s_recording.recording &&
            !s_recording.stopping &&
            s_recording_queue != nullptr;
        if (record_this_chunk) {
            ++s_recording_capture_inflight;
        }
        portEXIT_CRITICAL(&s_state_mux);

        if (record_this_chunk) {
            const bool queued =
                xQueueSend(s_recording_queue, s_pcm, 0) == pdTRUE;
            portENTER_CRITICAL(&s_state_mux);
            if (!queued) {
                ++s_recording.dropped_chunks;
            }
            if (s_recording_capture_inflight > 0) {
                --s_recording_capture_inflight;
            }
            portEXIT_CRITICAL(&s_state_mux);
        }

        float mean = 0.0f;
        for (size_t i = 0; i < kFftSize; ++i) {
            mean += static_cast<float>(s_pcm[i]);
        }
        mean /= static_cast<float>(kFftSize);

        float square_sum = 0.0f;
        float sample_peak = 0.0f;
        for (size_t i = 0; i < kFftSize; ++i) {
            const float centered = static_cast<float>(s_pcm[i]) - mean;
            square_sum += centered * centered;
            sample_peak = fmaxf(sample_peak, fabsf(centered));
            s_fft_real[i] = centered * s_hann_window[i];
            s_fft_imag[i] = 0.0f;
        }

        fft_in_place(s_fft_real, s_fft_imag, kFftSize);
        for (size_t bin = 0; bin < kFftSize / 2; ++bin) {
            s_fft_magnitude[bin] =
                sqrtf(
                    s_fft_real[bin] * s_fft_real[bin] +
                    s_fft_imag[bin] * s_fft_imag[bin]) *
                (2.0f / static_cast<float>(kFftSize));
        }

        const float rms = sqrtf(square_sum / static_cast<float>(kFftSize)) / 32768.0f;
        const float peak = sample_peak / 32768.0f;
        const float rms_db = 20.0f * log10f(fmaxf(rms, 0.000001f));
        const float peak_db = 20.0f * log10f(fmaxf(peak, 0.000001f));

        size_t dominant_bin = 1;
        float dominant_mag = 0.0f;
        for (size_t bin = 2; bin < kFftSize / 2; ++bin) {
            const float mag = s_fft_magnitude[bin];
            if (mag > dominant_mag) {
                dominant_mag = mag;
                dominant_bin = bin;
            }
        }

        int gain_db = 0;
        portENTER_CRITICAL(&s_state_mux);
        gain_db = s_status.spectrum_gain_db;
        portEXIT_CRITICAL(&s_state_mux);

        for (size_t band = 0; band < BOARD_DEMO_SPECTRUM_BANDS; ++band) {
            float band_mag = 0.0f;
            for (size_t bin = s_band_first_bin[band];
                 bin <= s_band_last_bin[band];
                 ++bin) {
                band_mag = fmaxf(band_mag, s_fft_magnitude[bin]);
            }
            const float band_db =
                20.0f * log10f(fmaxf(band_mag / 32768.0f, 0.000001f)) +
                static_cast<float>(gain_db);
            const float normalized = constrain((band_db + 72.0f) / 60.0f, 0.0f, 1.0f);
            const float alpha =
                normalized > s_smoothed_bands[band] ? 0.62f : 0.16f;
            s_smoothed_bands[band] +=
                (normalized - s_smoothed_bands[band]) * alpha;
            s_band_peaks[band] = fmaxf(
                s_smoothed_bands[band],
                s_band_peaks[band] - 0.018f);
        }

        AudioSnapshot snapshot = {};
        snapshot.valid = true;
        snapshot.rms_db = rms_db;
        snapshot.peak_db = peak_db;
        snapshot.dominant_hz = static_cast<uint16_t>(
            dominant_bin * kAudioSampleRate / kFftSize);
        snapshot.frame_count = ++frames;
        memcpy(snapshot.bands, s_smoothed_bands, sizeof(snapshot.bands));
        memcpy(snapshot.peaks, s_band_peaks, sizeof(snapshot.peaks));

        portENTER_CRITICAL(&s_audio_mux);
        s_audio = snapshot;
        portEXIT_CRITICAL(&s_audio_mux);
    }
}

void speaker_task(void *)
{
    constexpr size_t kFramesPerChunk = 128;
    int16_t samples[kFramesPerChunk * 2];
    const uint16_t tones[] = {523, 659, 784, 1047};

    digitalWrite(AUDIO_MUTE_PIN, HIGH);
    delay(12);

    float phase = 0.0f;
    for (uint16_t frequency : tones) {
        const float phase_step =
            2.0f * kPi * static_cast<float>(frequency) / kAudioSampleRate;
        const size_t total_frames = kAudioSampleRate / 7;
        size_t generated = 0;
        while (generated < total_frames) {
            const size_t remaining = total_frames - generated;
            const size_t frames =
                remaining < kFramesPerChunk ? remaining : kFramesPerChunk;
            for (size_t i = 0; i < frames; ++i) {
                const float envelope =
                    sinf(kPi * static_cast<float>(generated + i) /
                         static_cast<float>(total_frames));
                const int16_t sample =
                    static_cast<int16_t>(sinf(phase) * envelope * 4200.0f);
                samples[i * 2] = sample;
                samples[i * 2 + 1] = sample;
                phase += phase_step;
                if (phase >= 2.0f * kPi) {
                    phase -= 2.0f * kPi;
                }
            }
            s_speaker_i2s.write(
                reinterpret_cast<uint8_t *>(samples),
                frames * 2 * sizeof(int16_t));
            generated += frames;
        }
        delay(35);
    }

    memset(samples, 0, sizeof(samples));
    s_speaker_i2s.write(
        reinterpret_cast<uint8_t *>(samples), sizeof(samples));
    delay(10);
    digitalWrite(AUDIO_MUTE_PIN, LOW);
    __atomic_store_n(&s_speaker_task_busy, false, __ATOMIC_RELEASE);
    vTaskDelete(nullptr);
}

class DemoBleCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice device) override
    {
        if (!device.haveName()) {
            return;
        }
        const String name = device.getName();
        if (name.length() == 0) {
            return;
        }

        char row_text[40] = {};
        snprintf(
            row_text,
            sizeof(row_text),
            "%s  %d dBm",
            name.substring(0, 21).c_str(),
            device.getRSSI());

        portENTER_CRITICAL(&s_state_mux);
        for (size_t i = 0; i < BOARD_DEMO_WIRELESS_ROWS; ++i) {
            if (s_wireless.ble_rows[i][0] == '\0') {
                memcpy(s_wireless.ble_rows[i], row_text, sizeof(row_text));
                s_wireless.ble_rows[i][sizeof(s_wireless.ble_rows[i]) - 1] = '\0';
                break;
            }
        }
        portEXIT_CRITICAL(&s_state_mux);
    }
};

DemoBleCallbacks s_ble_callbacks;

void ble_task(void *)
{
    if (!s_ble_initialized) {
        BLEDevice::init("JC3636K718-LAB");
        s_ble_initialized = true;
    }
    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&s_ble_callbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    BLEScanResults *results = scan->start(3, false);

    portENTER_CRITICAL(&s_state_mux);
    s_wireless.ble_count = results ? results->getCount() : 0;
    s_wireless.ble_busy = false;
    s_wireless.ble_done = true;
    portEXIT_CRITICAL(&s_state_mux);

    scan->clearResults();
    __atomic_store_n(&s_ble_task_busy, false, __ATOMIC_RELEASE);
    vTaskDelete(nullptr);
}

void start_wifi_scan()
{
    portENTER_CRITICAL(&s_state_mux);
    s_wireless.wifi_busy = true;
    s_wireless.wifi_done = false;
    s_wireless.wifi_count = 0;
    memset(s_wireless.wifi_rows, 0, sizeof(s_wireless.wifi_rows));
    portEXIT_CRITICAL(&s_state_mux);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) {
        portENTER_CRITICAL(&s_state_mux);
        s_wireless.wifi_busy = false;
        s_wireless.wifi_done = true;
        portEXIT_CRITICAL(&s_state_mux);
    }
}

void poll_wifi_scan()
{
    bool busy = false;
    portENTER_CRITICAL(&s_state_mux);
    busy = s_wireless.wifi_busy;
    portEXIT_CRITICAL(&s_state_mux);
    if (!busy) {
        return;
    }

    const int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) {
        return;
    }

    char rows[BOARD_DEMO_WIRELESS_ROWS][40] = {};
    if (result > 0) {
        const int row_limit =
            result < static_cast<int>(BOARD_DEMO_WIRELESS_ROWS)
                ? result
                : static_cast<int>(BOARD_DEMO_WIRELESS_ROWS);
        for (int row = 0; row < row_limit; ++row) {
            snprintf(
                rows[row],
                sizeof(rows[row]),
                "%s  %d dBm",
                WiFi.SSID(row).substring(0, 21).c_str(),
                WiFi.RSSI(row));
        }
    }

    portENTER_CRITICAL(&s_state_mux);
    s_wireless.wifi_count = result > 0 ? result : 0;
    memcpy(s_wireless.wifi_rows, rows, sizeof(rows));
    s_wireless.wifi_busy = false;
    s_wireless.wifi_done = true;
    portEXIT_CRITICAL(&s_state_mux);

    WiFi.scanDelete();
}

}  // namespace

void board_hardware_begin()
{
    prepare_spectrum_tables();
    s_sd_mutex = xSemaphoreCreateMutex();
    if (s_sd_mutex == nullptr) {
        Serial.println("[SD] Failed to create shared TF-card mutex");
    }

    portENTER_CRITICAL(&s_state_mux);
    s_status.flash_bytes = ESP.getFlashChipSize();
    s_status.psram_bytes = ESP.getPsramSize();
    s_status.flash_ok = s_status.flash_bytes >= 16U * 1024U * 1024U;
    s_status.psram_ok =
        psramFound() && s_status.psram_bytes >= 8U * 1024U * 1024U;
    s_status.brightness = 88;
    s_status.spectrum_gain_db = 12;
    s_status.hid_compiled = BOARD_DEMO_USB_HID;
    s_status.msc_compiled = BOARD_DEMO_USB_MSC;
    s_status.msc_active = false;
    s_status.msc_ejected = true;
    portEXIT_CRITICAL(&s_state_mux);

    analogSetPinAttenuation(POWER_ADC_PIN, ADC_11db);
    pinMode(AUDIO_MUTE_PIN, OUTPUT);
    digitalWrite(AUDIO_MUTE_PIN, LOW);

    const bool haptic_ok = haptic_begin();
    const bool rgb_ok = rgb_show(
        kRgbPalette[0][0], kRgbPalette[0][1], kRgbPalette[0][2]);

    s_mic_i2s.setPinsPdmRx(MIC_I2S_SCK, MIC_I2S_SD);
    bool mic_ok = s_mic_i2s.begin(
        I2S_MODE_PDM_RX,
        kAudioSampleRate,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_MONO);

    s_speaker_i2s.setPins(
        AUDIO_I2S_BCK_IO,
        AUDIO_I2S_WS_IO,
        AUDIO_I2S_DO_IO,
        -1,
        AUDIO_I2S_MCK_IO);
    const bool speaker_ok = s_speaker_i2s.begin(
        I2S_MODE_STD,
        kAudioSampleRate,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO,
        I2S_STD_SLOT_BOTH);

    bool sd_ok = false;
    if (sd_mutex_take_internal(1000)) {
        sd_ok = sd_mount_locked();
        sd_mutex_give_internal();
    }

#if BOARD_DEMO_USB_HID && !ARDUINO_USB_MODE
    s_consumer_control.begin();
#endif
#if BOARD_DEMO_USB_MSC && !ARDUINO_USB_MODE
    msc_configure();
#endif

    s_recording_queue =
        xQueueCreate(kRecordingQueueDepth, kRecordingChunkBytes);
    if (s_recording_queue != nullptr) {
        s_recording_backend_ready =
            xTaskCreatePinnedToCore(
                recording_writer_task,
                "wav_writer",
                6144,
                nullptr,
                1,
                &s_recording_task_handle,
                1) == pdPASS;
    }
    if (!s_recording_backend_ready) {
        Serial.println("[REC] Failed to start WAV writer task");
        if (s_recording_queue != nullptr) {
            vQueueDelete(s_recording_queue);
            s_recording_queue = nullptr;
        }
    }

    if (mic_ok &&
        xTaskCreatePinnedToCore(
            audio_task,
            "pdm_fft",
            6144,
            nullptr,
            2,
            &s_audio_task_handle,
            0) != pdPASS) {
        mic_ok = false;
        Serial.println("[MIC] Failed to start PDM/FFT worker task");
    }

    portENTER_CRITICAL(&s_state_mux);
    s_status.haptic_ready = haptic_ok;
    s_status.rgb_ready = rgb_ok;
    s_status.mic_ready = mic_ok;
    s_status.speaker_ready = speaker_ok;
    s_status.sd_ready = sd_ok;
    s_status.sd_present = sd_ok;
    portEXIT_CRITICAL(&s_state_mux);

    Serial.printf("[MIC] PDM %s on CLK=%d DATA=%d @ %lu Hz\n",
                  mic_ok ? "PASS" : "FAIL",
                  MIC_I2S_SCK,
                  MIC_I2S_SD,
                  static_cast<unsigned long>(kAudioSampleRate));
    Serial.printf("[SPEAKER] I2S %s on BCLK=%d WS=%d DOUT=%d\n",
                  speaker_ok ? "PASS" : "FAIL",
                  AUDIO_I2S_BCK_IO,
                  AUDIO_I2S_WS_IO,
                  AUDIO_I2S_DO_IO);
    Serial.printf("[RGB] GPIO%d %s\n", RGB_DATA_PIN, rgb_ok ? "PASS" : "FAIL");
}

void board_hardware_poll()
{
    if (knob_has_moved()) {
        portENTER_CRITICAL(&s_state_mux);
        s_status.knob_seen = true;
        portEXIT_CRITICAL(&s_state_mux);
    }

    const uint32_t now = millis();
    rgb_chase_poll(now);
    if (now - s_last_power_read_ms >= 1000) {
        s_last_power_read_ms = now;
        const uint32_t millivolts = analogReadMilliVolts(POWER_ADC_PIN);
        portENTER_CRITICAL(&s_state_mux);
        s_status.power_volts = static_cast<float>(millivolts) * 0.002f;
        portEXIT_CRITICAL(&s_state_mux);
    }

    bool rescan_requested = false;
    portENTER_CRITICAL(&s_state_mux);
    rescan_requested =
        s_sd_rescan_requested &&
        !s_status.msc_active &&
        !s_msc_transitioning;
    portEXIT_CRITICAL(&s_state_mux);
    if (rescan_requested) {
        // A media decoder can keep an SD File open. Defer the remount until
        // that reader closes the file and releases the shared mutex.
        if (board_sd_lock(0)) {
            bool may_remount = false;
            portENTER_CRITICAL(&s_state_mux);
            may_remount =
                s_sd_rescan_requested &&
                !s_status.msc_active &&
                !s_msc_transitioning;
            if (may_remount) {
                s_sd_rescan_requested = false;
            }
            portEXIT_CRITICAL(&s_state_mux);
            const bool ok = may_remount && sd_mount_locked();
            board_sd_unlock();
            if (may_remount) {
                portENTER_CRITICAL(&s_state_mux);
                s_status.sd_ready = ok;
                s_status.sd_present = ok;
                if (!ok) {
                    s_status.sd_total_bytes = 0;
                    s_status.sd_used_bytes = 0;
                }
                portEXIT_CRITICAL(&s_state_mux);
            }
        }
    }

    if (s_wifi_scan_requested) {
        s_wifi_scan_requested = false;
        start_wifi_scan();
    }
    poll_wifi_scan();

    bool wifi_busy = false;
    portENTER_CRITICAL(&s_state_mux);
    wifi_busy = s_wireless.wifi_busy;
    portEXIT_CRITICAL(&s_state_mux);

    if (s_ble_scan_requested &&
        !wifi_busy &&
        !__atomic_exchange_n(
            &s_ble_task_busy, true, __ATOMIC_ACQ_REL)) {
        s_ble_scan_requested = false;
        portENTER_CRITICAL(&s_state_mux);
        s_wireless.ble_busy = true;
        s_wireless.ble_done = false;
        s_wireless.ble_count = 0;
        memset(s_wireless.ble_rows, 0, sizeof(s_wireless.ble_rows));
        portEXIT_CRITICAL(&s_state_mux);
        const BaseType_t created = xTaskCreatePinnedToCore(
            ble_task, "ble_scan", 6144, nullptr, 1, nullptr, 0);
        if (created != pdPASS) {
            __atomic_store_n(
                &s_ble_task_busy, false, __ATOMIC_RELEASE);
            portENTER_CRITICAL(&s_state_mux);
            s_wireless.ble_busy = false;
            s_wireless.ble_done = true;
            portEXIT_CRITICAL(&s_state_mux);
        }
    }
}

BoardStatus board_get_status()
{
    BoardStatus copy;
    portENTER_CRITICAL(&s_state_mux);
    copy = s_status;
    portEXIT_CRITICAL(&s_state_mux);
    return copy;
}

AudioSnapshot board_get_audio_snapshot()
{
    AudioSnapshot copy;
    portENTER_CRITICAL(&s_audio_mux);
    copy = s_audio;
    portEXIT_CRITICAL(&s_audio_mux);
    return copy;
}

RecordingStatus board_get_recording_status()
{
    RecordingStatus copy;
    portENTER_CRITICAL(&s_state_mux);
    copy = s_recording;
    copy.available =
        s_recording_backend_ready &&
        s_status.mic_ready &&
        s_status.sd_ready &&
        s_status.sd_present &&
        !s_status.msc_active &&
        !s_msc_transitioning;
    portEXIT_CRITICAL(&s_state_mux);
    return copy;
}

WirelessSnapshot board_get_wireless_snapshot()
{
    WirelessSnapshot copy;
    portENTER_CRITICAL(&s_state_mux);
    copy = s_wireless;
    portEXIT_CRITICAL(&s_state_mux);
    return copy;
}

void board_mark_touch()
{
    portENTER_CRITICAL(&s_state_mux);
    s_status.touch_seen = true;
    portEXIT_CRITICAL(&s_state_mux);
}

void board_set_brightness(uint8_t value)
{
    value = constrain(value, 12, 100);
    set_brightness(value);
    portENTER_CRITICAL(&s_state_mux);
    s_status.brightness = value;
    portEXIT_CRITICAL(&s_state_mux);
}

void board_adjust_spectrum_gain(int delta_db)
{
    portENTER_CRITICAL(&s_state_mux);
    s_status.spectrum_gain_db =
        constrain(s_status.spectrum_gain_db + delta_db, -12, 36);
    portEXIT_CRITICAL(&s_state_mux);
}

bool board_play_haptic(uint8_t pattern)
{
    const BoardStatus status = board_get_status();
    if (!status.haptic_ready) {
        return false;
    }
    static const uint8_t effects[] = {1, 10, 47, 15};
    const uint8_t effect = effects[pattern % (sizeof(effects) / sizeof(effects[0]))];
    return drv_write(kDrvRegMode, 0x00) &&
           drv_write(kDrvRegWaveSeq1, effect) &&
           drv_write(kDrvRegWaveSeq2, 0x00) &&
           drv_write(kDrvRegGo, 0x01);
}

bool board_rgb_toggle_chase()
{
    if (s_rgb_chase_active) {
        s_rgb_chase_active = false;
        const bool ok = rgb_show(0, 0, 0);
        portENTER_CRITICAL(&s_state_mux);
        s_status.rgb_ready = ok;
        s_status.rgb_chase_active = false;
        portEXIT_CRITICAL(&s_state_mux);
        return false;
    }

    s_rgb_chase_active = true;
    s_rgb_chase_head = 0;
    s_rgb_chase_lap = 0;
    s_last_rgb_frame_ms = 0;
    const bool ok = rgb_show_chase_frame();
    if (ok) {
        s_rgb_chase_head = 1;
        s_last_rgb_frame_ms = millis();
    } else {
        s_rgb_chase_active = false;
    }

    portENTER_CRITICAL(&s_state_mux);
    s_status.rgb_ready = ok;
    s_status.rgb_chase_active = s_rgb_chase_active;
    portEXIT_CRITICAL(&s_state_mux);
    return s_rgb_chase_active;
}

bool board_play_speaker_test()
{
    const BoardStatus status = board_get_status();
    if (!status.speaker_ready ||
        __atomic_exchange_n(
            &s_speaker_task_busy, true, __ATOMIC_ACQ_REL)) {
        return false;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        speaker_task, "speaker_test", 4096, nullptr, 2, nullptr, 0);
    if (created != pdPASS) {
        __atomic_store_n(
            &s_speaker_task_busy, false, __ATOMIC_RELEASE);
        return false;
    }
    return true;
}

bool board_recording_start()
{
    bool accepted = false;
    const char *error = nullptr;

    portENTER_CRITICAL(&s_state_mux);
    if (s_recording.starting ||
        s_recording.recording ||
        s_recording.stopping ||
        s_recording_start_requested ||
        s_recording_stop_requested) {
        // A repeated press while a transition is underway is not an error.
    } else if (!s_recording_backend_ready) {
        error = "Recorder worker unavailable";
    } else if (!s_status.mic_ready) {
        error = "Microphone unavailable";
    } else if (!s_status.sd_ready || !s_status.sd_present) {
        error = "TF card unavailable";
    } else if (s_status.msc_active || s_msc_transitioning) {
        error = "Disconnect USB disk first";
    } else if (s_sd_rescan_requested) {
        error = "TF card rescan pending";
    } else {
        s_recording_start_requested = true;
        s_recording.starting = true;
        s_recording.recording = false;
        s_recording.stopping = false;
        s_recording.duration_ms = 0;
        s_recording.data_bytes = 0;
        s_recording.dropped_chunks = 0;
        s_recording.path[0] = '\0';
        s_recording.error[0] = '\0';
        accepted = true;
    }
    if (error != nullptr) {
        copy_recording_text(
            s_recording.error,
            sizeof(s_recording.error),
            error);
    }
    portEXIT_CRITICAL(&s_state_mux);

    if (error != nullptr) {
        Serial.printf("[REC] Start rejected: %s\n", error);
    }
    return accepted;
}

bool board_recording_stop()
{
    bool accepted = false;
    portENTER_CRITICAL(&s_state_mux);
    if (s_recording.starting ||
        s_recording.recording ||
        s_recording.stopping ||
        s_recording_start_requested) {
        s_recording_start_requested = false;
        s_recording_stop_requested = true;
        s_recording.starting = false;
        s_recording.recording = false;
        s_recording.stopping = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_state_mux);
    return accepted;
}

void board_request_sd_rescan()
{
    portENTER_CRITICAL(&s_state_mux);
    if (!s_status.msc_active && !s_msc_transitioning) {
        s_sd_rescan_requested = true;
    }
    portEXIT_CRITICAL(&s_state_mux);
}

bool board_sd_lock(uint32_t timeout_ms)
{
    if (msc_blocks_firmware_sd() ||
        !sd_mutex_take_internal(timeout_ms)) {
        return false;
    }
    // Close the race where USB ownership starts while this task was waiting.
    if (msc_blocks_firmware_sd()) {
        sd_mutex_give_internal();
        return false;
    }
    return true;
}

void board_sd_unlock()
{
    sd_mutex_give_internal();
}

bool board_usb_msc_set_active(bool active)
{
#if BOARD_DEMO_USB_MSC && !ARDUINO_USB_MODE
    if (active) {
        bool already_active = false;
        bool may_start = false;
        portENTER_CRITICAL(&s_state_mux);
        already_active = s_status.msc_active;
        if (!already_active && !s_msc_transitioning) {
            s_msc_transitioning = true;
            may_start = true;
        }
        portEXIT_CRITICAL(&s_state_mux);
        if (already_active) {
            return true;
        }
        if (!may_start) {
            return false;
        }

        if (!sd_mutex_take_internal(kMscLockTimeoutMs)) {
            portENTER_CRITICAL(&s_state_mux);
            s_msc_transitioning = false;
            portEXIT_CRITICAL(&s_state_mux);
            return false;
        }

        const int sector_size = SD_MMC.sectorSize();
        const uint64_t card_size = SD_MMC.cardSize();
        const uint64_t sector_count_64 =
            sector_size > 0
                ? card_size / static_cast<uint32_t>(sector_size)
                : 0;
        const bool card_ready =
            SD_MMC.cardType() != CARD_NONE &&
            sector_size > 0 &&
            sector_size <= UINT16_MAX &&
            sector_count_64 > 0 &&
            sector_count_64 <= UINT32_MAX &&
            static_cast<size_t>(sector_size) <=
                sizeof(s_msc_sector_buffer);
        s_msc_sector_size =
            card_ready ? static_cast<uint16_t>(sector_size) : 0;
        s_msc_sector_count =
            card_ready ? static_cast<uint32_t>(sector_count_64) : 0;
        const bool started =
            card_ready &&
            s_usb_msc.begin(
                s_msc_sector_count,
                s_msc_sector_size);
        if (!started) {
            s_msc_sector_count = 0;
            s_msc_sector_size = 0;
        }

        portENTER_CRITICAL(&s_state_mux);
        s_status.msc_active = started;
        s_status.msc_ejected = !started;
        s_msc_transitioning = false;
        if (started) {
            s_sd_rescan_requested = false;
        }
        portEXIT_CRITICAL(&s_state_mux);
        sd_mutex_give_internal();

        s_usb_msc.mediaPresent(started);
        Serial.printf(
            "[MSC] USB disk %s (%lu sectors x %u bytes)\n",
            started ? "enabled" : "enable failed",
            static_cast<unsigned long>(s_msc_sector_count),
            static_cast<unsigned int>(s_msc_sector_size));
        return started;
    }

    bool already_inactive = false;
    bool may_stop = false;
    portENTER_CRITICAL(&s_state_mux);
    already_inactive = !s_status.msc_active;
    if (!already_inactive &&
        s_status.msc_ejected &&
        !s_msc_transitioning) {
        s_msc_transitioning = true;
        may_stop = true;
    }
    portEXIT_CRITICAL(&s_state_mux);
    if (already_inactive) {
        return true;
    }
    if (!may_stop) {
        return false;
    }

    s_usb_msc.mediaPresent(false);
    if (!sd_mutex_take_internal(1000)) {
        portENTER_CRITICAL(&s_state_mux);
        s_msc_transitioning = false;
        portEXIT_CRITICAL(&s_state_mux);
        return false;
    }

    // Force FATFS to forget any sectors changed by the computer before the
    // application can scan or open files again.
    const bool remounted = sd_mount_locked();
    s_msc_sector_count = 0;
    s_msc_sector_size = 0;
    portENTER_CRITICAL(&s_state_mux);
    s_status.msc_active = false;
    s_status.msc_ejected = true;
    s_status.sd_ready = remounted;
    s_status.sd_present = remounted;
    if (!remounted) {
        s_status.sd_total_bytes = 0;
        s_status.sd_used_bytes = 0;
    }
    s_sd_rescan_requested = false;
    s_msc_transitioning = false;
    portEXIT_CRITICAL(&s_state_mux);
    sd_mutex_give_internal();
    Serial.printf(
        "[MSC] USB disk disabled; TF remount=%s\n",
        remounted ? "PASS" : "FAIL");
    return true;
#else
    (void)active;
    return false;
#endif
}

void board_request_wifi_scan()
{
    const WirelessSnapshot snapshot = board_get_wireless_snapshot();
    if (!snapshot.wifi_busy &&
        !snapshot.ble_busy &&
        !s_wifi_scan_requested &&
        !s_ble_scan_requested) {
        s_wifi_scan_requested = true;
    }
}

void board_request_ble_scan()
{
    const WirelessSnapshot snapshot = board_get_wireless_snapshot();
    if (!snapshot.wifi_busy &&
        !snapshot.ble_busy &&
        !s_wifi_scan_requested &&
        !s_ble_scan_requested) {
        s_ble_scan_requested = true;
    }
}

bool board_hid_available()
{
    return BOARD_DEMO_USB_HID;
}

bool board_hid_volume_enabled()
{
    const BoardStatus status = board_get_status();
    return status.hid_volume_enabled;
}

void board_set_hid_volume_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_state_mux);
    s_status.hid_volume_enabled = BOARD_DEMO_USB_HID && enabled;
    portEXIT_CRITICAL(&s_state_mux);
}

void board_hid_volume_step(int direction)
{
#if BOARD_DEMO_USB_HID && !ARDUINO_USB_MODE
    if (!board_hid_volume_enabled() || direction == 0) {
        return;
    }
    s_consumer_control.press(
        direction > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT
                      : CONSUMER_CONTROL_VOLUME_DECREMENT);
    s_consumer_control.release();
#else
    (void)direction;
#endif
}
