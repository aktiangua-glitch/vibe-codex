#include "media_engine.h"

#include "board_hardware.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <strings.h>

#if defined(__has_include) && __has_include(<JPEGDEC.h>)
#include <JPEGDEC.h>
#define MEDIA_ENGINE_HAS_JPEGDEC 1
#else
#define MEDIA_ENGINE_HAS_JPEGDEC 0
#endif

namespace {

constexpr size_t kStreamReadBytes = 4096;
constexpr uint16_t kDefaultFps = 20;
constexpr uint16_t kMinimumFps = 1;
constexpr uint16_t kMaximumFps = 30;

enum class CommandType : uint8_t {
    Stop = 0,
    Play,
};

struct MediaCommand {
    CommandType type;
    MediaKind kind;
    uint16_t fps;
    bool loop;
    char path[MEDIA_ENGINE_PATH_CAPACITY];
};

struct DecodeTarget {
    uint8_t *pixels;
    uint16_t canvas_width;
    uint16_t canvas_height;
};

struct StreamCursor {
    File *file;
    size_t read_index;
    size_t read_size;
};

class SdLockGuard {
public:
    explicit SdLockGuard(uint32_t timeout_ms)
        : locked_(board_sd_lock(timeout_ms))
    {
    }

    ~SdLockGuard()
    {
        if (locked_) {
            board_sd_unlock();
        }
    }

    bool locked() const
    {
        return locked_;
    }

private:
    bool locked_;
};

enum class FrameReadResult : uint8_t {
    Ok = 0,
    EndOfFile,
    TooLarge,
    Interrupted,
};

portMUX_TYPE s_media_mux = portMUX_INITIALIZER_UNLOCKED;
MediaStatus s_status = {
    .decoder_available = MEDIA_ENGINE_HAS_JPEGDEC != 0,
    .initialized = false,
    .sd_available = false,
    .frame_ready = false,
    .looping = false,
    .state = MediaState::Uninitialized,
    .active_kind = MediaKind::Jpeg,
    .jpeg_file_count = 0,
    .mjpeg_file_count = 0,
    .target_fps = 0,
    .measured_fps_x10 = 0,
    .source_width = 0,
    .source_height = 0,
    .sequence = 0,
    .frames_decoded = 0,
    .frames_dropped = 0,
    .last_decode_ms = 0,
    .last_compressed_bytes = 0,
    .active_path = {0},
    .last_error = {0},
};

MediaFileInfo s_jpeg_files[MEDIA_ENGINE_MAX_FILES_PER_KIND] = {};
MediaFileInfo s_mjpeg_files[MEDIA_ENGINE_MAX_FILES_PER_KIND] = {};
size_t s_jpeg_file_count = 0;
size_t s_mjpeg_file_count = 0;

uint8_t *s_frame_buffers[2] = {nullptr, nullptr};
uint8_t *s_compressed_frame = nullptr;
uint8_t *s_stream_read_buffer = nullptr;
int s_front_buffer = -1;
int s_ready_buffer = -1;
int s_work_buffer = 0;

QueueHandle_t s_command_queue = nullptr;
TaskHandle_t s_media_task = nullptr;

#if MEDIA_ENGINE_HAS_JPEGDEC
alignas(16) JPEGDEC s_decoder;
#endif

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    strlcpy(destination, source, capacity);
}

void set_error(const char *message)
{
    char local[sizeof(s_status.last_error)] = {};
    copy_text(local, sizeof(local), message);

    portENTER_CRITICAL(&s_media_mux);
    copy_text(s_status.last_error, sizeof(s_status.last_error), local);
    s_status.state = MediaState::Error;
    portEXIT_CRITICAL(&s_media_mux);
}

void clear_error()
{
    portENTER_CRITICAL(&s_media_mux);
    s_status.last_error[0] = '\0';
    portEXIT_CRITICAL(&s_media_mux);
}

void set_state(MediaState state)
{
    portENTER_CRITICAL(&s_media_mux);
    s_status.state = state;
    portEXIT_CRITICAL(&s_media_mux);
}

bool has_extension(const char *path, MediaKind kind)
{
    if (path == nullptr) {
        return false;
    }
    const char *extension = strrchr(path, '.');
    if (extension == nullptr) {
        return false;
    }
    if (kind == MediaKind::Jpeg) {
        return strcasecmp(extension, ".jpg") == 0 ||
               strcasecmp(extension, ".jpeg") == 0;
    }
    return strcasecmp(extension, ".mjpeg") == 0 ||
           strcasecmp(extension, ".mjpg") == 0 ||
           strcasecmp(extension, ".avi") == 0;
}

void sort_files(MediaFileInfo *files, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcasecmp(files[i].path, files[j].path) > 0) {
                const MediaFileInfo temporary = files[i];
                files[i] = files[j];
                files[j] = temporary;
            }
        }
    }
}

size_t scan_directory(
    const char *directory,
    MediaKind kind,
    MediaFileInfo *output)
{
    File root = SD_MMC.open(directory, FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return 0;
    }

    size_t count = 0;
    File entry = root.openNextFile();
    while (entry && count < MEDIA_ENGINE_MAX_FILES_PER_KIND) {
        if (!entry.isDirectory() && has_extension(entry.path(), kind)) {
            copy_text(
                output[count].path,
                sizeof(output[count].path),
                entry.path());
            const size_t file_size = entry.size();
            output[count].size_bytes =
                file_size > UINT32_MAX ? UINT32_MAX
                                       : static_cast<uint32_t>(file_size);
            ++count;
        }
        entry.close();
        entry = root.openNextFile();
    }
    if (entry) {
        entry.close();
    }
    root.close();
    sort_files(output, count);
    return count;
}

int reserve_work_buffer()
{
    portENTER_CRITICAL(&s_media_mux);
    if (s_ready_buffer >= 0) {
        s_ready_buffer = -1;
        s_status.frame_ready = false;
        ++s_status.frames_dropped;
    }
    s_work_buffer = s_front_buffer == 0 ? 1 : 0;
    const int result = s_work_buffer;
    portEXIT_CRITICAL(&s_media_mux);
    return result;
}

void publish_frame(
    int buffer_index,
    uint16_t source_width,
    uint16_t source_height,
    uint32_t compressed_bytes,
    uint32_t decode_ms)
{
    portENTER_CRITICAL(&s_media_mux);
    s_ready_buffer = buffer_index;
    s_status.frame_ready = true;
    ++s_status.sequence;
    ++s_status.frames_decoded;
    s_status.source_width = source_width;
    s_status.source_height = source_height;
    s_status.last_compressed_bytes = compressed_bytes;
    s_status.last_decode_ms = decode_ms;
    portEXIT_CRITICAL(&s_media_mux);
}

bool wait_for_frame_consumed(MediaCommand *pending_command)
{
    while (true) {
        portENTER_CRITICAL(&s_media_mux);
        const bool consumed = s_ready_buffer < 0;
        portEXIT_CRITICAL(&s_media_mux);
        if (consumed) {
            return true;
        }
        if (xQueueReceive(
                s_command_queue,
                pending_command,
                pdMS_TO_TICKS(5)) == pdTRUE) {
            return false;
        }
    }
}

bool wait_with_command_check(
    uint32_t wait_ms,
    MediaCommand *pending_command)
{
    const uint32_t start = millis();
    while (millis() - start < wait_ms) {
        const uint32_t elapsed = millis() - start;
        const uint32_t remaining = wait_ms - elapsed;
        TickType_t wait_ticks = pdMS_TO_TICKS(remaining > 5 ? 5 : remaining);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }
        if (xQueueReceive(
                s_command_queue,
                pending_command,
                wait_ticks) == pdTRUE) {
            return false;
        }
    }
    return true;
}

#if MEDIA_ENGINE_HAS_JPEGDEC
int32_t jpeg_file_read(
    JPEGFILE *handle,
    uint8_t *buffer,
    int32_t length)
{
    if (handle == nullptr || handle->fHandle == nullptr ||
        buffer == nullptr || length <= 0) {
        return 0;
    }
    File *file = static_cast<File *>(handle->fHandle);
    if (!*file) {
        return 0;
    }
    return static_cast<int32_t>(
        file->read(buffer, static_cast<size_t>(length)));
}

int32_t jpeg_file_seek(JPEGFILE *handle, int32_t position)
{
    if (handle == nullptr || handle->fHandle == nullptr ||
        position < 0) {
        return 0;
    }
    File *file = static_cast<File *>(handle->fHandle);
    return *file && file->seek(static_cast<uint32_t>(position));
}

void jpeg_file_close(void *handle)
{
    if (handle == nullptr) {
        return;
    }
    File *file = static_cast<File *>(handle);
    if (*file) {
        file->close();
    }
}

int draw_jpeg_block(JPEGDRAW *draw)
{
    if (draw == nullptr || draw->pUser == nullptr || draw->pPixels == nullptr) {
        return 0;
    }
    DecodeTarget *target = static_cast<DecodeTarget *>(draw->pUser);
    int source_x = 0;
    int source_y = 0;
    int destination_x = draw->x;
    int destination_y = draw->y;
    int copy_width = draw->iWidthUsed;
    int copy_height = draw->iHeight;

    if (destination_x < 0) {
        source_x = -destination_x;
        copy_width -= source_x;
        destination_x = 0;
    }
    if (destination_y < 0) {
        source_y = -destination_y;
        copy_height -= source_y;
        destination_y = 0;
    }
    if (destination_x + copy_width > target->canvas_width) {
        copy_width = target->canvas_width - destination_x;
    }
    if (destination_y + copy_height > target->canvas_height) {
        copy_height = target->canvas_height - destination_y;
    }
    if (copy_width <= 0 || copy_height <= 0) {
        return 1;
    }

    const uint8_t *source = reinterpret_cast<const uint8_t *>(draw->pPixels);
    const size_t source_stride = static_cast<size_t>(draw->iWidth) * 2U;
    const size_t destination_stride =
        static_cast<size_t>(target->canvas_width) * 2U;
    source += static_cast<size_t>(source_y) * source_stride +
              static_cast<size_t>(source_x) * 2U;
    uint8_t *destination =
        target->pixels +
        static_cast<size_t>(destination_y) * destination_stride +
        static_cast<size_t>(destination_x) * 2U;

    const size_t row_bytes = static_cast<size_t>(copy_width) * 2U;
    for (int row = 0; row < copy_height; ++row) {
        memcpy(destination, source, row_bytes);
        source += source_stride;
        destination += destination_stride;
    }
    return 1;
}

int scale_option_for_size(
    int width,
    int height,
    int *divisor)
{
    int option = 0;
    *divisor = 1;
    if (width > MEDIA_ENGINE_FRAME_WIDTH ||
        height > MEDIA_ENGINE_FRAME_HEIGHT) {
        option = JPEG_SCALE_HALF;
        *divisor = 2;
    }
    if (width / *divisor > MEDIA_ENGINE_FRAME_WIDTH ||
        height / *divisor > MEDIA_ENGINE_FRAME_HEIGHT) {
        option = JPEG_SCALE_QUARTER;
        *divisor = 4;
    }
    if (width / *divisor > MEDIA_ENGINE_FRAME_WIDTH ||
        height / *divisor > MEDIA_ENGINE_FRAME_HEIGHT) {
        option = JPEG_SCALE_EIGHTH;
        *divisor = 8;
    }
    return option;
}

bool decode_open_jpeg(
    int buffer_index,
    uint32_t compressed_bytes,
    uint16_t *decoded_width,
    uint16_t *decoded_height,
    uint32_t *decode_ms)
{
    if (s_decoder.getJPEGType() == JPEG_MODE_PROGRESSIVE) {
        set_error("Progressive JPEG is unsupported; export baseline JPEG");
        return false;
    }

    const int source_width = s_decoder.getWidth();
    const int source_height = s_decoder.getHeight();
    if (source_width <= 0 || source_height <= 0) {
        set_error("Invalid JPEG dimensions");
        return false;
    }

    int scale_divisor = 1;
    const int decode_options =
        scale_option_for_size(source_width, source_height, &scale_divisor);
    int output_width = source_width / scale_divisor;
    int output_height = source_height / scale_divisor;
    if (output_width < 1) {
        output_width = 1;
    }
    if (output_height < 1) {
        output_height = 1;
    }

    memset(s_frame_buffers[buffer_index], 0, MEDIA_ENGINE_FRAME_BYTES);
    DecodeTarget target = {
        .pixels = s_frame_buffers[buffer_index],
        .canvas_width = MEDIA_ENGINE_FRAME_WIDTH,
        .canvas_height = MEDIA_ENGINE_FRAME_HEIGHT,
    };
    s_decoder.setPixelType(RGB565_BIG_ENDIAN);
    s_decoder.setUserPointer(&target);

    const int x = (static_cast<int>(MEDIA_ENGINE_FRAME_WIDTH) -
                   output_width) /
                  2;
    const int y = (static_cast<int>(MEDIA_ENGINE_FRAME_HEIGHT) -
                   output_height) /
                  2;
    const uint32_t start = millis();
    const int decoded = s_decoder.decode(x, y, decode_options);
    *decode_ms = millis() - start;
    if (decoded == 0) {
        char message[96] = {};
        snprintf(
            message,
            sizeof(message),
            "JPEG decode failed (error %d)",
            s_decoder.getLastError());
        set_error(message);
        return false;
    }

    *decoded_width = static_cast<uint16_t>(
        output_width > MEDIA_ENGINE_FRAME_WIDTH
            ? MEDIA_ENGINE_FRAME_WIDTH
            : output_width);
    *decoded_height = static_cast<uint16_t>(
        output_height > MEDIA_ENGINE_FRAME_HEIGHT
            ? MEDIA_ENGINE_FRAME_HEIGHT
            : output_height);
    (void)compressed_bytes;
    return true;
}

bool decode_jpeg_file(const MediaCommand &command)
{
    SdLockGuard sd_guard(1000);
    if (!sd_guard.locked()) {
        set_error("SD card is busy");
        return false;
    }

    File file = SD_MMC.open(command.path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        set_error("Cannot open JPEG file");
        return false;
    }

    const size_t file_size = file.size();
    if (file_size == 0 || file_size > INT32_MAX) {
        file.close();
        set_error("JPEG file is empty or too large");
        return false;
    }
    const uint32_t compressed_bytes =
        file_size > UINT32_MAX ? UINT32_MAX
                               : static_cast<uint32_t>(file_size);
    if (s_decoder.open(
            &file,
            static_cast<int>(file_size),
            jpeg_file_close,
            jpeg_file_read,
            jpeg_file_seek,
            draw_jpeg_block) == 0) {
        file.close();
        set_error("Cannot parse JPEG header");
        return false;
    }

    const int buffer_index = reserve_work_buffer();
    uint16_t decoded_width = 0;
    uint16_t decoded_height = 0;
    uint32_t decode_ms = 0;
    const bool ok = decode_open_jpeg(
        buffer_index,
        compressed_bytes,
        &decoded_width,
        &decoded_height,
        &decode_ms);
    s_decoder.close();
    if (!ok) {
        return false;
    }

    publish_frame(
        buffer_index,
        decoded_width,
        decoded_height,
        compressed_bytes,
        decode_ms);
    set_state(MediaState::Ready);
    return true;
}

bool decode_jpeg_memory(
    uint8_t *compressed,
    size_t compressed_size,
    int buffer_index,
    uint16_t *decoded_width,
    uint16_t *decoded_height,
    uint32_t *decode_ms)
{
    if (compressed_size == 0 || compressed_size > INT32_MAX) {
        set_error("Invalid MJPEG frame size");
        return false;
    }
    if (s_decoder.openRAM(
            compressed,
            static_cast<int>(compressed_size),
            draw_jpeg_block) == 0) {
        return false;
    }
    const bool ok = decode_open_jpeg(
        buffer_index,
        static_cast<uint32_t>(compressed_size),
        decoded_width,
        decoded_height,
        decode_ms);
    s_decoder.close();
    return ok;
}
#endif

int stream_next_byte(
    StreamCursor *cursor,
    MediaCommand *pending_command,
    bool *interrupted)
{
    if (cursor->read_index >= cursor->read_size) {
        if (xQueueReceive(
                s_command_queue,
                pending_command,
                0) == pdTRUE) {
            *interrupted = true;
            return -1;
        }
        cursor->read_size =
            cursor->file->read(s_stream_read_buffer, kStreamReadBytes);
        cursor->read_index = 0;
        if (cursor->read_size == 0) {
            return -1;
        }
    }
    return s_stream_read_buffer[cursor->read_index++];
}

FrameReadResult read_next_jpeg_frame(
    StreamCursor *cursor,
    size_t *frame_size,
    MediaCommand *pending_command)
{
    bool in_frame = false;
    bool overflow = false;
    bool have_previous = false;
    uint8_t previous = 0;
    size_t output_size = 0;
    bool interrupted = false;

    while (true) {
        const int next =
            stream_next_byte(cursor, pending_command, &interrupted);
        if (next < 0) {
            *frame_size = 0;
            return interrupted ? FrameReadResult::Interrupted
                               : FrameReadResult::EndOfFile;
        }
        const uint8_t byte = static_cast<uint8_t>(next);

        if (!in_frame) {
            if (have_previous && previous == 0xFF && byte == 0xD8) {
                in_frame = true;
                overflow = false;
                output_size = 2;
                s_compressed_frame[0] = 0xFF;
                s_compressed_frame[1] = 0xD8;
            }
            previous = byte;
            have_previous = true;
            continue;
        }

        if (output_size < MEDIA_ENGINE_MAX_COMPRESSED_BYTES) {
            s_compressed_frame[output_size] = byte;
        } else {
            overflow = true;
        }
        ++output_size;

        if (previous == 0xFF && byte == 0xD9) {
            if (overflow) {
                *frame_size = 0;
                return FrameReadResult::TooLarge;
            }
            *frame_size = output_size;
            return FrameReadResult::Ok;
        }
        previous = byte;
    }
}

void update_active_command(const MediaCommand &command)
{
    portENTER_CRITICAL(&s_media_mux);
    s_status.active_kind = command.kind;
    s_status.target_fps = command.kind == MediaKind::Mjpeg
                              ? command.fps
                              : 0;
    s_status.looping =
        command.kind == MediaKind::Mjpeg && command.loop;
    s_status.measured_fps_x10 = 0;
    copy_text(
        s_status.active_path,
        sizeof(s_status.active_path),
        command.path);
    s_status.last_error[0] = '\0';
    portEXIT_CRITICAL(&s_media_mux);
}

bool play_mjpeg(
    const MediaCommand &command,
    MediaCommand *pending_command)
{
#if !MEDIA_ENGINE_HAS_JPEGDEC
    (void)command;
    (void)pending_command;
    set_error("JPEGDEC is not installed");
    return true;
#else
    SdLockGuard sd_guard(1000);
    if (!sd_guard.locked()) {
        set_error("SD card is busy");
        return true;
    }

    File file = SD_MMC.open(command.path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        set_error("Cannot open MJPEG file");
        return true;
    }

    StreamCursor cursor = {
        .file = &file,
        .read_index = 0,
        .read_size = 0,
    };
    set_state(MediaState::Playing);

    uint32_t frames_in_pass = 0;
    uint32_t measurement_frames = 0;
    uint32_t measurement_start = millis();
    const uint32_t frame_period_ms =
        command.fps == 0 ? (1000U / kDefaultFps)
                         : (1000U / command.fps);

    while (true) {
        const uint32_t frame_started_ms = millis();
        if (xQueueReceive(
                s_command_queue,
                pending_command,
                0) == pdTRUE) {
            file.close();
            return false;
        }

        size_t compressed_size = 0;
        const FrameReadResult read_result =
            read_next_jpeg_frame(
                &cursor, &compressed_size, pending_command);
        if (read_result == FrameReadResult::Interrupted) {
            file.close();
            return false;
        }
        if (read_result == FrameReadResult::TooLarge) {
            portENTER_CRITICAL(&s_media_mux);
            ++s_status.frames_dropped;
            portEXIT_CRITICAL(&s_media_mux);
            continue;
        }
        if (read_result == FrameReadResult::EndOfFile) {
            if (command.loop && frames_in_pass > 0 && file.seek(0)) {
                cursor.read_index = 0;
                cursor.read_size = 0;
                frames_in_pass = 0;
                continue;
            }
            if (frames_in_pass == 0) {
                set_error("No complete JPEG frame found in MJPEG file");
            } else {
                set_state(MediaState::Ready);
            }
            file.close();
            return true;
        }

        const int buffer_index = reserve_work_buffer();
        uint16_t decoded_width = 0;
        uint16_t decoded_height = 0;
        uint32_t decode_ms = 0;
        clear_error();
        if (!decode_jpeg_memory(
                s_compressed_frame,
                compressed_size,
                buffer_index,
                &decoded_width,
                &decoded_height,
                &decode_ms)) {
            portENTER_CRITICAL(&s_media_mux);
            ++s_status.frames_dropped;
            s_status.state = MediaState::Playing;
            portEXIT_CRITICAL(&s_media_mux);
            continue;
        }

        ++frames_in_pass;
        ++measurement_frames;
        publish_frame(
            buffer_index,
            decoded_width,
            decoded_height,
            static_cast<uint32_t>(compressed_size),
            decode_ms);

        const uint32_t measurement_elapsed = millis() - measurement_start;
        if (measurement_elapsed >= 1000) {
            const uint32_t fps_x10 =
                measurement_frames * 10000U / measurement_elapsed;
            portENTER_CRITICAL(&s_media_mux);
            s_status.measured_fps_x10 =
                fps_x10 > UINT16_MAX
                    ? UINT16_MAX
                    : static_cast<uint16_t>(fps_x10);
            portEXIT_CRITICAL(&s_media_mux);
            measurement_frames = 0;
            measurement_start = millis();
        }

        if (!wait_for_frame_consumed(pending_command)) {
            file.close();
            return false;
        }

        const uint32_t frame_elapsed_ms = millis() - frame_started_ms;
        const uint32_t remaining =
            frame_elapsed_ms >= frame_period_ms
                ? 0
                : frame_period_ms - frame_elapsed_ms;
        if (remaining > 0 &&
            !wait_with_command_check(remaining, pending_command)) {
            file.close();
            return false;
        }
    }
#endif
}

void media_task(void *)
{
    MediaCommand command = {};
    bool have_pending_command = false;

    while (true) {
        if (!have_pending_command) {
            xQueueReceive(s_command_queue, &command, portMAX_DELAY);
        }
        have_pending_command = false;

        if (command.type == CommandType::Stop) {
            set_state(MediaState::Idle);
            continue;
        }

        update_active_command(command);
        if (command.kind == MediaKind::Jpeg) {
            set_state(MediaState::Decoding);
#if MEDIA_ENGINE_HAS_JPEGDEC
            decode_jpeg_file(command);
#else
            set_error("JPEGDEC is not installed");
#endif
            continue;
        }

        have_pending_command = !play_mjpeg(command, &command);
    }
}

bool allocate_media_buffers()
{
    for (int index = 0; index < 2; ++index) {
        s_frame_buffers[index] = static_cast<uint8_t *>(
            heap_caps_aligned_alloc(
                16,
                MEDIA_ENGINE_FRAME_BYTES,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_frame_buffers[index] == nullptr) {
            return false;
        }
        memset(s_frame_buffers[index], 0, MEDIA_ENGINE_FRAME_BYTES);
    }
    s_compressed_frame = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(
            16,
            MEDIA_ENGINE_MAX_COMPRESSED_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_stream_read_buffer = static_cast<uint8_t *>(
        heap_caps_malloc(kStreamReadBytes, MALLOC_CAP_8BIT));
    return s_compressed_frame != nullptr && s_stream_read_buffer != nullptr;
}

void free_media_buffers()
{
    for (int index = 0; index < 2; ++index) {
        if (s_frame_buffers[index] != nullptr) {
            heap_caps_free(s_frame_buffers[index]);
            s_frame_buffers[index] = nullptr;
        }
    }
    if (s_compressed_frame != nullptr) {
        heap_caps_free(s_compressed_frame);
        s_compressed_frame = nullptr;
    }
    if (s_stream_read_buffer != nullptr) {
        heap_caps_free(s_stream_read_buffer);
        s_stream_read_buffer = nullptr;
    }
}

uint16_t sanitize_fps(uint16_t fps)
{
    if (fps < kMinimumFps) {
        return kDefaultFps;
    }
    return fps > kMaximumFps ? kMaximumFps : fps;
}

}  // namespace

bool media_engine_begin()
{
    portENTER_CRITICAL(&s_media_mux);
    const bool already_initialized = s_status.initialized;
    portEXIT_CRITICAL(&s_media_mux);
    if (already_initialized) {
        return true;
    }

#if !MEDIA_ENGINE_HAS_JPEGDEC
    set_error("JPEGDEC dependency is unavailable");
    return false;
#endif

    if (!psramFound()) {
        set_error("PSRAM is required for media frame buffers");
        return false;
    }
    if (!allocate_media_buffers()) {
        free_media_buffers();
        set_error("Cannot allocate media buffers in PSRAM");
        return false;
    }

    s_command_queue = xQueueCreate(1, sizeof(MediaCommand));
    if (s_command_queue == nullptr) {
        free_media_buffers();
        set_error("Cannot create media command queue");
        return false;
    }
    if (xTaskCreatePinnedToCore(
            media_task,
            "media_decode",
            8192,
            nullptr,
            1,
            &s_media_task,
            0) != pdPASS) {
        vQueueDelete(s_command_queue);
        s_command_queue = nullptr;
        free_media_buffers();
        set_error("Cannot start media decode task");
        return false;
    }

    portENTER_CRITICAL(&s_media_mux);
    s_status.initialized = true;
    s_status.state = MediaState::Idle;
    portEXIT_CRITICAL(&s_media_mux);
    media_engine_scan();
    return true;
}

bool media_engine_scan()
{
    const MediaStatus before = media_engine_get_status();
    if (!before.initialized ||
        before.state == MediaState::Playing ||
        before.state == MediaState::Decoding) {
        return false;
    }

    set_state(MediaState::Scanning);
    SdLockGuard sd_guard(1000);
    if (!sd_guard.locked()) {
        set_error("SD card is busy");
        return false;
    }
    const bool sd_available = SD_MMC.cardType() != CARD_NONE;
    if (!sd_available) {
        s_jpeg_file_count = 0;
        s_mjpeg_file_count = 0;
        portENTER_CRITICAL(&s_media_mux);
        s_status.sd_available = false;
        s_status.jpeg_file_count = 0;
        s_status.mjpeg_file_count = 0;
        s_status.state = MediaState::Idle;
        portEXIT_CRITICAL(&s_media_mux);
        return false;
    }

    memset(s_jpeg_files, 0, sizeof(s_jpeg_files));
    memset(s_mjpeg_files, 0, sizeof(s_mjpeg_files));
    s_jpeg_file_count =
        scan_directory("/pic", MediaKind::Jpeg, s_jpeg_files);
    s_mjpeg_file_count =
        scan_directory("/mjpeg", MediaKind::Mjpeg, s_mjpeg_files);

    portENTER_CRITICAL(&s_media_mux);
    s_status.sd_available = true;
    s_status.jpeg_file_count =
        static_cast<uint16_t>(s_jpeg_file_count);
    s_status.mjpeg_file_count =
        static_cast<uint16_t>(s_mjpeg_file_count);
    s_status.state = MediaState::Idle;
    portEXIT_CRITICAL(&s_media_mux);
    return true;
}

size_t media_engine_file_count(MediaKind kind)
{
    portENTER_CRITICAL(&s_media_mux);
    const size_t result =
        kind == MediaKind::Jpeg ? s_jpeg_file_count : s_mjpeg_file_count;
    portEXIT_CRITICAL(&s_media_mux);
    return result;
}

bool media_engine_file_info(
    MediaKind kind,
    size_t index,
    MediaFileInfo *out)
{
    if (out == nullptr) {
        return false;
    }
    portENTER_CRITICAL(&s_media_mux);
    const MediaFileInfo *files =
        kind == MediaKind::Jpeg ? s_jpeg_files : s_mjpeg_files;
    const size_t count =
        kind == MediaKind::Jpeg ? s_jpeg_file_count : s_mjpeg_file_count;
    if (index >= count) {
        portEXIT_CRITICAL(&s_media_mux);
        return false;
    }
    *out = files[index];
    portEXIT_CRITICAL(&s_media_mux);
    return true;
}

bool media_engine_play_file(
    MediaKind kind,
    const char *path,
    uint16_t fps,
    bool loop)
{
    const MediaStatus status = media_engine_get_status();
    if (!status.initialized || path == nullptr || path[0] != '/' ||
        !has_extension(path, kind)) {
        return false;
    }

    MediaCommand command = {
        .type = CommandType::Play,
        .kind = kind,
        .fps = sanitize_fps(fps),
        .loop = loop,
        .path = {0},
    };
    copy_text(command.path, sizeof(command.path), path);
    return xQueueOverwrite(s_command_queue, &command) == pdTRUE;
}

bool media_engine_play_index(
    MediaKind kind,
    size_t index,
    uint16_t fps,
    bool loop)
{
    MediaFileInfo file = {};
    if (!media_engine_file_info(kind, index, &file)) {
        return false;
    }
    return media_engine_play_file(kind, file.path, fps, loop);
}

void media_engine_stop()
{
    const MediaStatus status = media_engine_get_status();
    if (!status.initialized || s_command_queue == nullptr) {
        return;
    }
    MediaCommand command = {
        .type = CommandType::Stop,
        .kind = MediaKind::Jpeg,
        .fps = 0,
        .loop = false,
        .path = {0},
    };
    xQueueOverwrite(s_command_queue, &command);
}

MediaStatus media_engine_get_status()
{
    portENTER_CRITICAL(&s_media_mux);
    const MediaStatus result = s_status;
    portEXIT_CRITICAL(&s_media_mux);
    return result;
}

bool media_engine_take_frame(MediaFrame *out)
{
    if (out == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_media_mux);
    if (s_ready_buffer < 0) {
        portEXIT_CRITICAL(&s_media_mux);
        return false;
    }

    const int previous_front = s_front_buffer;
    s_front_buffer = s_ready_buffer;
    s_ready_buffer = -1;
    s_work_buffer =
        previous_front >= 0 ? previous_front : (s_front_buffer == 0 ? 1 : 0);
    s_status.frame_ready = false;

    out->data = s_frame_buffers[s_front_buffer];
    out->data_size = MEDIA_ENGINE_FRAME_BYTES;
    out->width = MEDIA_ENGINE_FRAME_WIDTH;
    out->height = MEDIA_ENGINE_FRAME_HEIGHT;
    out->sequence = s_status.sequence;
    portEXIT_CRITICAL(&s_media_mux);
    return true;
}
