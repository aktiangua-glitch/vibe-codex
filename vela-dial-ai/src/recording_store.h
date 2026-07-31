#ifndef VELA_RECORDING_STORE_H
#define VELA_RECORDING_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "bridge_models.h"

constexpr size_t VELA_IDEMPOTENCY_KEY_BYTES = 96;

struct StagedRecording {
    uint8_t *data;
    size_t size;
    uint32_t crc32;
    char path[VELA_RECORDING_PATH_BYTES];
};

struct PendingRecordingUpload {
    BridgeAudioPurpose purpose;
    uint32_t wav_size;
    uint32_t wav_crc32;
    char path[VELA_RECORDING_PATH_BYTES];
    char thread_id[VELA_THREAD_ID_BYTES];
    char approval_id[VELA_APPROVAL_ID_BYTES];
    char idempotency_key[VELA_IDEMPOTENCY_KEY_BYTES];
};

// Copies a finalized WAV into PSRAM while holding the shared TF-card lock.
// The lock is released before this function returns, so callers may perform
// arbitrarily slow network I/O with the returned buffer.
bool recording_store_stage(
    const char *path,
    StagedRecording *recording,
    char *error,
    size_t error_size);
void recording_store_release(StagedRecording *recording);
bool recording_store_get_or_create_pending(
    const char *device_id,
    const StagedRecording &recording,
    BridgeAudioPurpose purpose,
    const char *thread_id,
    const char *approval_id,
    PendingRecordingUpload *pending,
    char *error,
    size_t error_size);
bool recording_store_load_pending(PendingRecordingUpload *pending);
bool recording_store_complete_pending(const char *idempotency_key);

#endif
