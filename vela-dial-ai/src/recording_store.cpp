#include "recording_store.h"

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <string.h>

#include "board_hardware.h"

namespace {

constexpr size_t kWavHeaderBytes = 44;
constexpr size_t kMaximumRecordingBytes = 2U * 1024U * 1024U;
constexpr size_t kReadChunkBytes = 16U * 1024U;
constexpr uint32_t kSdLockTimeoutMs = 2500;
constexpr uint32_t kPendingMagic = 0x5655504CUL;  // "VUPL"
constexpr uint16_t kPendingSchema = 1;
constexpr char kPendingNamespace[] = "vela_upload";
constexpr char kPendingKey[] = "pending";

struct StoredPendingUpload {
    uint32_t magic;
    uint16_t schema;
    uint8_t purpose;
    uint8_t reserved;
    uint32_t wav_size;
    uint32_t wav_crc32;
    char path[VELA_RECORDING_PATH_BYTES];
    char thread_id[VELA_THREAD_ID_BYTES];
    char approval_id[VELA_APPROVAL_ID_BYTES];
    char idempotency_key[VELA_IDEMPOTENCY_KEY_BYTES];
    uint32_t record_crc32;
};

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }
    snprintf(destination, capacity, "%s", source == nullptr ? "" : source);
}

bool recording_path_is_safe(const char *path)
{
    constexpr char kPrefix[] = "/recordings/";
    return path != nullptr &&
           strncmp(path, kPrefix, sizeof(kPrefix) - 1U) == 0 &&
           strstr(path, "..") == nullptr &&
           strlen(path) < VELA_RECORDING_PATH_BYTES;
}

uint16_t read_le16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1]) << 8U;
}

uint32_t read_le32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           static_cast<uint32_t>(data[1]) << 8U |
           static_cast<uint32_t>(data[2]) << 16U |
           static_cast<uint32_t>(data[3]) << 24U;
}

bool canonical_wav_header(
    const uint8_t *data,
    size_t size,
    size_t *committed_size)
{
    if (data == nullptr ||
        size < kWavHeaderBytes ||
        memcmp(data, "RIFF", 4) != 0 ||
        memcmp(data + 8, "WAVE", 4) != 0 ||
        memcmp(data + 12, "fmt ", 4) != 0 ||
        memcmp(data + 36, "data", 4) != 0) {
        return false;
    }

    const uint32_t riff_size = read_le32(data + 4);
    const uint32_t fmt_size = read_le32(data + 16);
    const uint16_t audio_format = read_le16(data + 20);
    const uint16_t channels = read_le16(data + 22);
    const uint32_t sample_rate = read_le32(data + 24);
    const uint32_t byte_rate = read_le32(data + 28);
    const uint16_t block_align = read_le16(data + 32);
    const uint16_t bits_per_sample = read_le16(data + 34);
    const uint32_t data_size = read_le32(data + 40);
    const uint64_t declared_riff_bytes =
        static_cast<uint64_t>(riff_size) + 8ULL;
    const uint64_t declared_data_bytes =
        static_cast<uint64_t>(data_size) + kWavHeaderBytes;

    const bool valid =
        fmt_size == 16U &&
        audio_format == 1U &&
        channels == 1U &&
        sample_rate == 24000U &&
        byte_rate == 48000U &&
        block_align == 2U &&
        bits_per_sample == 16U &&
        data_size > 0U &&
        (data_size % block_align) == 0U &&
        declared_riff_bytes == declared_data_bytes &&
        declared_data_bytes <= size;
    if (valid && committed_size != nullptr) {
        *committed_size =
            static_cast<size_t>(declared_data_bytes);
    }
    return valid;
}

uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(
                    -static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

uint32_t fnv1a_text(const char *text)
{
    uint32_t hash = 2166136261UL;
    if (text == nullptr) {
        return hash;
    }
    while (*text != '\0') {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 16777619UL;
    }
    return hash;
}

uint32_t pending_record_crc(const StoredPendingUpload &pending)
{
    return crc32_bytes(
        reinterpret_cast<const uint8_t *>(&pending),
        offsetof(StoredPendingUpload, record_crc32));
}

bool load_stored_pending(StoredPendingUpload *pending)
{
    if (pending == nullptr) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(kPendingNamespace, true)) {
        return false;
    }
    if (preferences.getBytesLength(kPendingKey) !=
        sizeof(StoredPendingUpload)) {
        preferences.end();
        return false;
    }
    StoredPendingUpload stored = {};
    const size_t read =
        preferences.getBytes(kPendingKey, &stored, sizeof(stored));
    preferences.end();
    if (read != sizeof(stored) ||
        stored.magic != kPendingMagic ||
        stored.schema != kPendingSchema ||
        stored.purpose >
            static_cast<uint8_t>(BridgeAudioPurpose::RejectReason) ||
        stored.path[sizeof(stored.path) - 1] != '\0' ||
        stored.thread_id[sizeof(stored.thread_id) - 1] != '\0' ||
        stored.approval_id[sizeof(stored.approval_id) - 1] != '\0' ||
        stored.idempotency_key[
            sizeof(stored.idempotency_key) - 1] != '\0' ||
        stored.record_crc32 != pending_record_crc(stored)) {
        return false;
    }
    *pending = stored;
    return true;
}

bool save_stored_pending(StoredPendingUpload *pending)
{
    if (pending == nullptr) {
        return false;
    }
    pending->magic = kPendingMagic;
    pending->schema = kPendingSchema;
    pending->record_crc32 = pending_record_crc(*pending);
    Preferences preferences;
    if (!preferences.begin(kPendingNamespace, false)) {
        return false;
    }
    const size_t written =
        preferences.putBytes(kPendingKey, pending, sizeof(*pending));
    preferences.end();
    return written == sizeof(*pending);
}

void export_pending(
    const StoredPendingUpload &stored,
    PendingRecordingUpload *pending)
{
    if (pending == nullptr) {
        return;
    }
    memset(pending, 0, sizeof(*pending));
    pending->purpose =
        static_cast<BridgeAudioPurpose>(stored.purpose);
    pending->wav_size = stored.wav_size;
    pending->wav_crc32 = stored.wav_crc32;
    copy_text(pending->path, sizeof(pending->path), stored.path);
    copy_text(
        pending->thread_id,
        sizeof(pending->thread_id),
        stored.thread_id);
    copy_text(
        pending->approval_id,
        sizeof(pending->approval_id),
        stored.approval_id);
    copy_text(
        pending->idempotency_key,
        sizeof(pending->idempotency_key),
        stored.idempotency_key);
}

class SdLockGuard {
public:
    explicit SdLockGuard(bool locked) : locked_(locked) {}
    ~SdLockGuard()
    {
        if (locked_) {
            board_sd_unlock();
        }
    }

    SdLockGuard(const SdLockGuard &) = delete;
    SdLockGuard &operator=(const SdLockGuard &) = delete;

private:
    bool locked_;
};

}  // namespace

bool recording_store_stage(
    const char *path,
    StagedRecording *recording,
    char *error,
    size_t error_size)
{
    if (recording == nullptr) {
        copy_text(error, error_size, "Missing output buffer");
        return false;
    }
    memset(recording, 0, sizeof(*recording));

    if (!recording_path_is_safe(path)) {
        copy_text(error, error_size, "Invalid recording path");
        return false;
    }

    const RecordingStatus status = board_get_recording_status();
    if (status.starting || status.recording || status.stopping) {
        copy_text(error, error_size, "Recording is not finalized");
        return false;
    }
    if (!board_sd_lock(kSdLockTimeoutMs)) {
        copy_text(error, error_size, "TF card is busy");
        return false;
    }
    SdLockGuard lock_guard(true);

    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        copy_text(error, error_size, "Could not open recording");
        return false;
    }
    const size_t file_size = file.size();
    if (file_size < kWavHeaderBytes ||
        file_size > kMaximumRecordingBytes) {
        file.close();
        copy_text(error, error_size, "Recording size is invalid");
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(
            file_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        file.close();
        copy_text(error, error_size, "Not enough PSRAM for upload");
        return false;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        const size_t remaining = file_size - total_read;
        const size_t requested =
            remaining < kReadChunkBytes ? remaining : kReadChunkBytes;
        const size_t read = file.read(buffer + total_read, requested);
        if (read == 0) {
            break;
        }
        total_read += read;
    }
    file.close();

    size_t committed_size = 0;
    if (total_read != file_size ||
        !canonical_wav_header(
            buffer, file_size, &committed_size)) {
        heap_caps_free(buffer);
        copy_text(error, error_size, "Recording is incomplete or not WAV");
        return false;
    }

    recording->data = buffer;
    // A power-loss checkpoint can leave physical tail bytes beyond the last
    // committed RIFF/data lengths. Only committed bytes participate in CRC,
    // idempotency and upload.
    recording->size = committed_size;
    recording->crc32 = crc32_bytes(buffer, committed_size);
    copy_text(recording->path, sizeof(recording->path), path);
    if (error != nullptr && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

void recording_store_release(StagedRecording *recording)
{
    if (recording == nullptr) {
        return;
    }
    if (recording->data != nullptr) {
        heap_caps_free(recording->data);
    }
    memset(recording, 0, sizeof(*recording));
}

bool recording_store_get_or_create_pending(
    const char *device_id,
    const StagedRecording &recording,
    BridgeAudioPurpose purpose,
    const char *thread_id,
    const char *approval_id,
    PendingRecordingUpload *pending,
    char *error,
    size_t error_size)
{
    if (device_id == nullptr || device_id[0] == '\0' ||
        recording.data == nullptr || recording.size == 0 ||
        pending == nullptr) {
        copy_text(error, error_size, "Invalid upload identity");
        return false;
    }

    const char *safe_thread = thread_id == nullptr ? "" : thread_id;
    const char *safe_approval =
        approval_id == nullptr ? "" : approval_id;
    StoredPendingUpload stored = {};
    if (load_stored_pending(&stored)) {
        const bool matches =
            stored.purpose == static_cast<uint8_t>(purpose) &&
            stored.wav_size == recording.size &&
            stored.wav_crc32 == recording.crc32 &&
            strcmp(stored.path, recording.path) == 0 &&
            strcmp(stored.thread_id, safe_thread) == 0 &&
            strcmp(stored.approval_id, safe_approval) == 0;
        if (!matches) {
            copy_text(
                error,
                error_size,
                "Another recording is pending upload");
            return false;
        }
        export_pending(stored, pending);
        if (error != nullptr && error_size > 0) {
            error[0] = '\0';
        }
        return true;
    }

    stored.magic = kPendingMagic;
    stored.schema = kPendingSchema;
    stored.purpose = static_cast<uint8_t>(purpose);
    stored.wav_size = static_cast<uint32_t>(recording.size);
    stored.wav_crc32 = recording.crc32;
    copy_text(stored.path, sizeof(stored.path), recording.path);
    copy_text(stored.thread_id, sizeof(stored.thread_id), safe_thread);
    copy_text(
        stored.approval_id,
        sizeof(stored.approval_id),
        safe_approval);
    snprintf(
        stored.idempotency_key,
        sizeof(stored.idempotency_key),
        "wav-v1-%s-%08lx-%08lx-%08lx",
        device_id,
        static_cast<unsigned long>(recording.size),
        static_cast<unsigned long>(recording.crc32),
        static_cast<unsigned long>(fnv1a_text(recording.path)));
    if (!save_stored_pending(&stored)) {
        copy_text(error, error_size, "Could not persist upload identity");
        return false;
    }
    export_pending(stored, pending);
    if (error != nullptr && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

bool recording_store_load_pending(PendingRecordingUpload *pending)
{
    StoredPendingUpload stored = {};
    if (!load_stored_pending(&stored)) {
        return false;
    }
    export_pending(stored, pending);
    return true;
}

bool recording_store_complete_pending(const char *idempotency_key)
{
    if (idempotency_key == nullptr ||
        idempotency_key[0] == '\0') {
        return false;
    }
    StoredPendingUpload stored = {};
    if (!load_stored_pending(&stored)) {
        return true;
    }
    if (strcmp(stored.idempotency_key, idempotency_key) != 0) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(kPendingNamespace, false)) {
        return false;
    }
    const bool removed = preferences.remove(kPendingKey);
    preferences.end();
    return removed;
}
