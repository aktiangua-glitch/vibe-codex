#ifndef VELA_BRIDGE_MODELS_H
#define VELA_BRIDGE_MODELS_H

#include <stddef.h>
#include <stdint.h>

constexpr size_t VELA_WIFI_SSID_BYTES = 33;
constexpr size_t VELA_WIFI_PASSWORD_BYTES = 65;
constexpr size_t VELA_BRIDGE_HOST_BYTES = 96;
constexpr size_t VELA_BRIDGE_TOKEN_BYTES = 192;
constexpr size_t VELA_THREAD_ID_BYTES = 64;
constexpr size_t VELA_APPROVAL_ID_BYTES = 64;
constexpr size_t VELA_NONCE_BYTES = 64;
constexpr size_t VELA_ACTION_DIGEST_BYTES = 65;
constexpr size_t VELA_OPERATION_ID_BYTES = 64;
constexpr size_t VELA_RECORDING_PATH_BYTES = 64;
constexpr size_t VELA_ERROR_TEXT_BYTES = 128;
constexpr size_t VELA_MAX_BRIDGE_SESSIONS = 5;
constexpr size_t VELA_MAX_QUOTA_WINDOWS = 2;

enum class ConnectivityPhase : uint8_t {
    Uninitialized,
    LoadingConfig,
    AccessPoint,
    Connecting,
    Online,
    Error,
};

enum class BridgeSessionState : uint8_t {
    Unknown,
    WaitingApproval,
    Running,
    Complete,
    Failed,
};

enum class BridgeOperationKind : uint8_t {
    None,
    RefreshSnapshot,
    UploadRecording,
    ResolveApproval,
    StartProvisioning,
    ForgetConfiguration,
};

enum class BridgeOperationState : uint8_t {
    Idle,
    Queued,
    Running,
    Accepted,
    Failed,
};

enum class BridgeAudioPurpose : uint8_t {
    NewSession,
    RejectReason,
};

enum class BridgeApprovalDecision : uint8_t {
    Reject,
    Allow,
};

struct ConnectivitySnapshot {
    uint32_t revision;
    ConnectivityPhase phase;
    bool configured;
    bool wifi_connected;
    bool access_point_active;
    int16_t rssi_dbm;
    char station_ssid[VELA_WIFI_SSID_BYTES];
    char station_ip[16];
    char access_point_ssid[VELA_WIFI_SSID_BYTES];
    char access_point_password[24];
    char access_point_ip[16];
    char bridge_host[VELA_BRIDGE_HOST_BYTES];
    uint16_t bridge_port;
    char error[VELA_ERROR_TEXT_BYTES];
};

struct BridgeQuotaWindow {
    // A missing quota window is unknown, not zero usage.
    bool valid;
    uint32_t window_minutes;
    uint8_t used_percent;
    uint8_t remaining_percent;
    char key[12];
    char label[16];
    char reset_label[40];
};

struct BridgeAccountTokens {
    bool valid;
    uint64_t lifetime_tokens;
    uint64_t latest_day_tokens;
    uint64_t peak_daily_tokens;
    uint16_t current_streak_days;
    char latest_day_label[16];
};

struct BridgeApproval {
    bool present;
    uint64_t expires_at_ms;
    char thread_id[VELA_THREAD_ID_BYTES];
    // Opaque, Bridge-signed token. The device must not interpret this as a
    // Codex RPC identifier.
    char approval_id[VELA_APPROVAL_ID_BYTES];
    char nonce[VELA_NONCE_BYTES];
    char action_digest[VELA_ACTION_DIGEST_BYTES];
    char title[48];
    char detail[112];
};

struct BridgeSession {
    char thread_id[VELA_THREAD_ID_BYTES];
    char title[48];
    char summary[112];
    BridgeSessionState state;
    uint64_t total_tokens;
    uint64_t last_tokens;
    uint64_t context_window_tokens;
    uint8_t context_used_percent;
    bool context_usage_valid;
    bool needs_feedback;
    BridgeApproval approval;
};

struct BridgeOperation {
    BridgeOperationKind kind;
    BridgeOperationState state;
    int16_t http_status;
    uint32_t updated_ms;
    char operation_id[VELA_OPERATION_ID_BYTES];
    // Filled by an accepted new-session recording once the Bridge has mapped
    // it to a Codex thread. Empty while the operation is still asynchronous.
    char result_thread_id[VELA_THREAD_ID_BYTES];
    char message[VELA_ERROR_TEXT_BYTES];
};

struct BridgeSnapshot {
    uint32_t generation;
    uint64_t remote_revision;
    uint32_t updated_ms;
    ConnectivitySnapshot connectivity;
    bool bridge_online;
    int16_t last_http_status;
    char bridge_error[VELA_ERROR_TEXT_BYTES];
    uint8_t quota_window_count;
    BridgeQuotaWindow quota_windows[VELA_MAX_QUOTA_WINDOWS];
    BridgeAccountTokens account_tokens;
    // Legacy aliases retained while older Bridge responses are still in use.
    BridgeQuotaWindow quota_5h;
    BridgeQuotaWindow quota_7d;
    uint16_t total_session_count;
    uint8_t session_count;
    BridgeSession sessions[VELA_MAX_BRIDGE_SESSIONS];
    uint16_t pending_approval_count;
    BridgeApproval current_approval;
    BridgeOperation operation;
};

struct BridgeCommand {
    BridgeOperationKind kind;
    BridgeAudioPurpose audio_purpose;
    BridgeApprovalDecision approval_decision;
    char recording_path[VELA_RECORDING_PATH_BYTES];
    char thread_id[VELA_THREAD_ID_BYTES];
    char approval_id[VELA_APPROVAL_ID_BYTES];
    char nonce[VELA_NONCE_BYTES];
    char action_digest[VELA_ACTION_DIGEST_BYTES];
};

#endif
