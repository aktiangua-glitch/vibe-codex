#ifndef VELA_BRIDGE_CLIENT_H
#define VELA_BRIDGE_CLIENT_H

#include "bridge_models.h"

// Starts the single Core 0 network worker. Safe to call more than once.
bool bridge_client_begin();

// Copies the latest immutable snapshot. This is the only API the LVGL/main
// task needs for reading network state.
bool bridge_client_get_snapshot(BridgeSnapshot *snapshot);

// Queue operations are non-blocking and return only whether the command was
// accepted locally. Completion is reported through snapshot.operation.
bool bridge_client_request_refresh();
bool bridge_client_upload_recording(
    const char *recording_path,
    BridgeAudioPurpose purpose,
    const char *thread_id = nullptr,
    const char *approval_id = nullptr);
bool bridge_client_resolve_approval(
    const char *thread_id,
    const char *approval_id,
    const char *nonce,
    const char *action_digest,
    BridgeApprovalDecision decision);
bool bridge_client_start_provisioning();
bool bridge_client_forget_configuration();

#endif
