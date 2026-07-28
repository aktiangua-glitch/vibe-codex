#pragma once

#include <stddef.h>
#include <stdint.h>

// The media engine never calls LVGL. Decode runs on a worker task and the
// LVGL/main thread consumes complete RGB565 frames with take_frame().

constexpr uint16_t MEDIA_ENGINE_FRAME_WIDTH = 360;
constexpr uint16_t MEDIA_ENGINE_FRAME_HEIGHT = 360;
constexpr size_t MEDIA_ENGINE_FRAME_BYTES =
    static_cast<size_t>(MEDIA_ENGINE_FRAME_WIDTH) *
    static_cast<size_t>(MEDIA_ENGINE_FRAME_HEIGHT) * 2U;
constexpr size_t MEDIA_ENGINE_MAX_COMPRESSED_BYTES = 256U * 1024U;
constexpr size_t MEDIA_ENGINE_MAX_FILES_PER_KIND = 24;
constexpr size_t MEDIA_ENGINE_PATH_CAPACITY = 128;

enum class MediaKind : uint8_t {
    Jpeg = 0,
    Mjpeg = 1,
};

enum class MediaState : uint8_t {
    Uninitialized = 0,
    Idle,
    Scanning,
    Decoding,
    Playing,
    Ready,
    Error,
};

struct MediaFileInfo {
    char path[MEDIA_ENGINE_PATH_CAPACITY];
    uint32_t size_bytes;
};

struct MediaFrame {
    // Valid until a later successful media_engine_take_frame() call.
    // The bytes are RGB565 big-endian, matching LV_COLOR_16_SWAP=1.
    const uint8_t *data;
    size_t data_size;
    uint16_t width;
    uint16_t height;
    uint32_t sequence;
};

struct MediaStatus {
    bool decoder_available;
    bool initialized;
    bool sd_available;
    bool frame_ready;
    bool looping;
    MediaState state;
    MediaKind active_kind;
    uint16_t jpeg_file_count;
    uint16_t mjpeg_file_count;
    uint16_t target_fps;
    uint16_t measured_fps_x10;
    uint16_t source_width;
    uint16_t source_height;
    uint32_t sequence;
    uint32_t frames_decoded;
    uint32_t frames_dropped;
    uint32_t last_decode_ms;
    uint32_t last_compressed_bytes;
    char active_path[MEDIA_ENGINE_PATH_CAPACITY];
    char last_error[96];
};

// Call once after SD_MMC has been mounted by board_hardware.
bool media_engine_begin();

// Synchronously scans /pic for .jpg/.jpeg and /mjpeg for .mjpeg/.mjpg.
// Do not call while video is playing.
bool media_engine_scan();

size_t media_engine_file_count(MediaKind kind);
bool media_engine_file_info(MediaKind kind, size_t index, MediaFileInfo *out);

// JPEG produces one frame. MJPEG accepts raw concatenated JPEG frames and
// AVI-style files because frames are located by JPEG SOI/EOI markers.
bool media_engine_play_file(
    MediaKind kind,
    const char *path,
    uint16_t fps = 20,
    bool loop = true);
bool media_engine_play_index(
    MediaKind kind,
    size_t index,
    uint16_t fps = 20,
    bool loop = true);
void media_engine_stop();

MediaStatus media_engine_get_status();

// Must be called only from the LVGL/main thread. On true, update an
// lv_img_dsc_t (360x360, LV_IMG_CF_TRUE_COLOR) and call lv_img_set_src().
// The decoder never overwrites the currently presented buffer.
bool media_engine_take_frame(MediaFrame *out);
