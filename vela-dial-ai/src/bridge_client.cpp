#include "bridge_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include "connectivity_service.h"
#include "device_config.h"
#include "recording_store.h"
#include "text_utils.h"

namespace {

constexpr UBaseType_t kCommandQueueDepth = 8;
constexpr uint32_t kNetworkTaskStackBytes = 18U * 1024U;
constexpr UBaseType_t kNetworkTaskPriority = 1;
constexpr BaseType_t kNetworkTaskCore = 0;
constexpr uint32_t kWorkerTickMs = 20;
constexpr uint32_t kSnapshotPollMs = 1500;
constexpr uint32_t kBusySnapshotPollMs = 700;
constexpr uint32_t kInitialFailureBackoffMs = 1000;
constexpr uint32_t kMaximumFailureBackoffMs = 15000;
constexpr uint32_t kUploadRetryMs = 15000;
constexpr int32_t kHttpConnectTimeoutMs = 3000;
constexpr uint16_t kHttpReadTimeoutMs = 7000;
constexpr uint16_t kRecordingHttpReadTimeoutMs = 60000;

QueueHandle_t s_command_queue = nullptr;
SemaphoreHandle_t s_snapshot_mutex = nullptr;
TaskHandle_t s_network_task = nullptr;
bool s_started = false;

BridgeSnapshot s_worker_snapshot = {};
BridgeSnapshot s_published_snapshot = {};
uint32_t s_connectivity_revision = 0;
uint32_t s_next_snapshot_ms = 0;
uint32_t s_failure_backoff_ms = kInitialFailureBackoffMs;
uint32_t s_pending_upload_retry_ms = 0;
String s_snapshot_etag;

void copy_text(char *destination, size_t capacity, const char *source)
{
    vela_copy_utf8(destination, capacity, source);
}

bool copy_checked(
    char *destination,
    size_t capacity,
    const char *source,
    bool required)
{
    if (destination == nullptr || capacity == 0) {
        return false;
    }
    const char *safe_source = source == nullptr ? "" : source;
    const size_t length = strlen(safe_source);
    if ((required && length == 0) || length >= capacity) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (safe_source[index] == '\r' ||
            safe_source[index] == '\n') {
            return false;
        }
    }
    memcpy(destination, safe_source, length + 1U);
    return true;
}

bool due(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t fnv1a_text(const char *text, uint32_t hash = 2166136261UL)
{
    if (text == nullptr) {
        return hash;
    }
    while (*text != '\0') {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 16777619UL;
    }
    return hash;
}

void publish_snapshot()
{
    if (s_snapshot_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        s_worker_snapshot.generation =
            s_published_snapshot.generation + 1U;
        s_published_snapshot = s_worker_snapshot;
        xSemaphoreGive(s_snapshot_mutex);
    }
}

void set_operation(
    BridgeOperationKind kind,
    BridgeOperationState state,
    int http_status,
    const char *message,
    const char *operation_id = nullptr,
    const char *result_thread_id = nullptr)
{
    BridgeOperation &operation = s_worker_snapshot.operation;
    operation.kind = kind;
    operation.state = state;
    operation.http_status = static_cast<int16_t>(http_status);
    operation.updated_ms = millis();
    copy_text(
        operation.operation_id,
        sizeof(operation.operation_id),
        operation_id);
    copy_text(
        operation.result_thread_id,
        sizeof(operation.result_thread_id),
        result_thread_id);
    copy_text(operation.message, sizeof(operation.message), message);
    publish_snapshot();
}

void mark_bridge_online(int http_status)
{
    s_worker_snapshot.bridge_online = true;
    s_worker_snapshot.last_http_status =
        static_cast<int16_t>(http_status);
    s_worker_snapshot.bridge_error[0] = '\0';
    s_worker_snapshot.updated_ms = millis();
    s_failure_backoff_ms = kInitialFailureBackoffMs;
}

void mark_bridge_failure(int http_status, const char *message)
{
    s_worker_snapshot.bridge_online = false;
    s_worker_snapshot.last_http_status =
        static_cast<int16_t>(http_status);
    copy_text(
        s_worker_snapshot.bridge_error,
        sizeof(s_worker_snapshot.bridge_error),
        message);
    s_worker_snapshot.updated_ms = millis();
    publish_snapshot();

    s_next_snapshot_ms = millis() + s_failure_backoff_ms;
    s_failure_backoff_ms =
        s_failure_backoff_ms >= kMaximumFailureBackoffMs / 2U
            ? kMaximumFailureBackoffMs
            : s_failure_backoff_ms * 2U;
}

const char *object_text(
    JsonObjectConst object,
    const char *snake_name,
    const char *camel_name)
{
    JsonVariantConst value = object[snake_name];
    if (value.isNull() && camel_name != nullptr) {
        value = object[camel_name];
    }
    return value.is<const char *>() ? value.as<const char *>() : "";
}

uint64_t object_u64(
    JsonObjectConst object,
    const char *snake_name,
    const char *camel_name,
    uint64_t fallback = 0)
{
    JsonVariantConst value = object[snake_name];
    if (value.isNull() && camel_name != nullptr) {
        value = object[camel_name];
    }
    return value.isNull() ? fallback : value.as<uint64_t>();
}

bool object_bool(
    JsonObjectConst object,
    const char *snake_name,
    const char *camel_name,
    bool fallback)
{
    JsonVariantConst value = object[snake_name];
    if (value.isNull() && camel_name != nullptr) {
        value = object[camel_name];
    }
    return value.isNull() ? fallback : value.as<bool>();
}

JsonObjectConst object_member(
    JsonObjectConst object,
    const char *first,
    const char *second = nullptr,
    const char *third = nullptr)
{
    JsonVariantConst value = object[first];
    if (value.isNull() && second != nullptr) {
        value = object[second];
    }
    if (value.isNull() && third != nullptr) {
        value = object[third];
    }
    return value.as<JsonObjectConst>();
}

uint8_t clamp_percent(uint64_t value)
{
    return static_cast<uint8_t>(value > 100U ? 100U : value);
}

void parse_quota_window(
    JsonObjectConst object,
    BridgeQuotaWindow *window)
{
    if (window == nullptr) {
        return;
    }
    memset(window, 0, sizeof(*window));
    if (object.isNull()) {
        window->valid = false;
        return;
    }
    JsonVariantConst used = object["used_percent"];
    if (used.isNull()) {
        used = object["usedPercent"];
    }
    JsonVariantConst explicit_valid = object["valid"];
    window->valid = explicit_valid.isNull()
        ? !used.isNull()
        : explicit_valid.as<bool>();
    if (!window->valid) {
        return;
    }
    window->window_minutes = static_cast<uint32_t>(
        object_u64(object, "window_minutes", "windowMinutes"));
    copy_text(
        window->key,
        sizeof(window->key),
        object_text(object, "key", nullptr));
    copy_text(
        window->label,
        sizeof(window->label),
        object_text(object, "label", nullptr));
    window->used_percent =
        clamp_percent(used.as<uint64_t>());
    JsonVariantConst remaining = object["remaining_percent"];
    if (remaining.isNull()) {
        remaining = object["remainingPercent"];
    }
    window->remaining_percent = remaining.isNull()
        ? static_cast<uint8_t>(100U - window->used_percent)
        : clamp_percent(remaining.as<uint64_t>());
    copy_text(
        window->reset_label,
        sizeof(window->reset_label),
        object_text(object, "reset_label", "resetLabel"));
}

BridgeSessionState parse_session_state(const char *text)
{
    if (text == nullptr) {
        return BridgeSessionState::Unknown;
    }
    if (strcasecmp(text, "waiting_approval") == 0 ||
        strcasecmp(text, "waitingApproval") == 0 ||
        strcasecmp(text, "approval") == 0 ||
        strcasecmp(text, "wait") == 0) {
        return BridgeSessionState::WaitingApproval;
    }
    if (strcasecmp(text, "running") == 0 ||
        strcasecmp(text, "live") == 0) {
        return BridgeSessionState::Running;
    }
    if (strcasecmp(text, "complete") == 0 ||
        strcasecmp(text, "completed") == 0 ||
        strcasecmp(text, "done") == 0) {
        return BridgeSessionState::Complete;
    }
    if (strcasecmp(text, "failed") == 0 ||
        strcasecmp(text, "error") == 0) {
        return BridgeSessionState::Failed;
    }
    return BridgeSessionState::Unknown;
}

void parse_approval(
    JsonObjectConst object,
    const char *fallback_thread_id,
    BridgeApproval *approval)
{
    if (approval == nullptr) {
        return;
    }
    memset(approval, 0, sizeof(*approval));
    if (object.isNull()) {
        return;
    }
    copy_text(
        approval->thread_id,
        sizeof(approval->thread_id),
        object_text(object, "thread_id", "threadId"));
    if (approval->thread_id[0] == '\0') {
        copy_text(
            approval->thread_id,
            sizeof(approval->thread_id),
            fallback_thread_id);
    }
    copy_text(
        approval->approval_id,
        sizeof(approval->approval_id),
        object_text(object, "approval_id", "approvalId"));
    if (approval->approval_id[0] == '\0') {
        copy_text(
            approval->approval_id,
            sizeof(approval->approval_id),
            object_text(object, "token", nullptr));
    }
    approval->present =
        approval->approval_id[0] != '\0' &&
        object_bool(object, "present", nullptr, true);
    approval->expires_at_ms =
        object_u64(object, "expires_at_ms", "expiresAtMs");
    copy_text(
        approval->nonce,
        sizeof(approval->nonce),
        object_text(object, "nonce", nullptr));
    copy_text(
        approval->action_digest,
        sizeof(approval->action_digest),
        object_text(object, "action_digest", "actionDigest"));
    copy_text(
        approval->title,
        sizeof(approval->title),
        object_text(object, "title", nullptr));
    copy_text(
        approval->detail,
        sizeof(approval->detail),
        object_text(object, "detail", nullptr));
}

BridgeOperationState parse_operation_state(const char *text)
{
    if (text == nullptr) {
        return BridgeOperationState::Running;
    }
    if (strcasecmp(text, "queued") == 0) {
        return BridgeOperationState::Queued;
    }
    if (strcasecmp(text, "running") == 0 ||
        strcasecmp(text, "processing") == 0) {
        return BridgeOperationState::Running;
    }
    if (strcasecmp(text, "accepted") == 0 ||
        strcasecmp(text, "success") == 0 ||
        strcasecmp(text, "succeeded") == 0 ||
        strcasecmp(text, "complete") == 0 ||
        strcasecmp(text, "completed") == 0 ||
        strcasecmp(text, "done") == 0) {
        return BridgeOperationState::Accepted;
    }
    if (strcasecmp(text, "failed") == 0 ||
        strcasecmp(text, "error") == 0) {
        return BridgeOperationState::Failed;
    }
    return BridgeOperationState::Running;
}

void update_mutation_from_snapshot(JsonObjectConst root)
{
    if (s_worker_snapshot.operation.kind !=
            BridgeOperationKind::UploadRecording &&
        s_worker_snapshot.operation.kind !=
            BridgeOperationKind::ResolveApproval) {
        return;
    }
    JsonObjectConst remote = object_member(
        root,
        "device_operation",
        "deviceOperation",
        "lastDeviceOperation");
    if (remote.isNull()) {
        return;
    }
    const char *remote_id =
        object_text(remote, "operation_id", "operationId");
    BridgeOperation &local = s_worker_snapshot.operation;
    if (local.operation_id[0] == '\0' ||
        remote_id[0] == '\0' ||
        strcmp(local.operation_id, remote_id) != 0) {
        return;
    }
    const char *thread_id =
        object_text(remote, "result_thread_id", "resultThreadId");
    if (thread_id[0] == '\0') {
        thread_id = object_text(remote, "thread_id", "threadId");
    }
    if (thread_id[0] != '\0') {
        copy_text(
            local.result_thread_id,
            sizeof(local.result_thread_id),
            thread_id);
    }
    local.state = parse_operation_state(
        object_text(remote, "state", "status"));
    const char *message = object_text(remote, "message", nullptr);
    if (message[0] != '\0') {
        copy_text(local.message, sizeof(local.message), message);
    }
    local.updated_ms = millis();
}

bool parse_snapshot_payload(
    const String &payload,
    char *error,
    size_t error_size)
{
    JsonDocument document;
    const DeserializationError parse_error =
        deserializeJson(document, payload);
    if (parse_error) {
        copy_text(error, error_size, parse_error.c_str());
        return false;
    }
    const JsonObjectConst root = document.as<JsonObjectConst>();
    if (root.isNull()) {
        copy_text(error, error_size, "Snapshot JSON is not an object");
        return false;
    }

    s_worker_snapshot.remote_revision =
        object_u64(root, "revision", nullptr);
    JsonObjectConst quota =
        object_member(root, "quota", "usage");

    memset(
        s_worker_snapshot.quota_windows,
        0,
        sizeof(s_worker_snapshot.quota_windows));
    s_worker_snapshot.quota_window_count = 0;
    JsonArrayConst quota_windows =
        quota["windows"].as<JsonArrayConst>();
    for (JsonObjectConst object : quota_windows) {
        if (s_worker_snapshot.quota_window_count >=
            VELA_MAX_QUOTA_WINDOWS) {
            break;
        }
        BridgeQuotaWindow &window =
            s_worker_snapshot.quota_windows[
                s_worker_snapshot.quota_window_count];
        parse_quota_window(object, &window);
        if (window.valid) {
            ++s_worker_snapshot.quota_window_count;
        }
    }

    parse_quota_window(
        object_member(quota, "five_hour", "fiveHour", "5h"),
        &s_worker_snapshot.quota_5h);
    parse_quota_window(
        object_member(quota, "seven_day", "sevenDay", "7d"),
        &s_worker_snapshot.quota_7d);
    if (s_worker_snapshot.quota_5h.valid) {
        s_worker_snapshot.quota_5h.window_minutes = 300;
        if (s_worker_snapshot.quota_5h.label[0] == '\0') {
            copy_text(
                s_worker_snapshot.quota_5h.label,
                sizeof(s_worker_snapshot.quota_5h.label),
                "5H");
        }
    }
    if (s_worker_snapshot.quota_7d.valid) {
        s_worker_snapshot.quota_7d.window_minutes = 10080;
        if (s_worker_snapshot.quota_7d.label[0] == '\0') {
            copy_text(
                s_worker_snapshot.quota_7d.label,
                sizeof(s_worker_snapshot.quota_7d.label),
                "7D");
        }
    }
    if (s_worker_snapshot.quota_window_count == 0) {
        if (s_worker_snapshot.quota_5h.valid) {
            s_worker_snapshot.quota_windows[
                s_worker_snapshot.quota_window_count++] =
                s_worker_snapshot.quota_5h;
        }
        if (s_worker_snapshot.quota_7d.valid &&
            s_worker_snapshot.quota_window_count <
                VELA_MAX_QUOTA_WINDOWS) {
            s_worker_snapshot.quota_windows[
                s_worker_snapshot.quota_window_count++] =
                s_worker_snapshot.quota_7d;
        }
    }

    memset(
        &s_worker_snapshot.account_tokens,
        0,
        sizeof(s_worker_snapshot.account_tokens));
    JsonObjectConst tokens =
        object_member(quota, "tokens", "tokenUsage");
    if (!tokens.isNull()) {
        s_worker_snapshot.account_tokens.valid =
            object_bool(tokens, "valid", nullptr, true);
        s_worker_snapshot.account_tokens.lifetime_tokens =
            object_u64(tokens, "lifetime_tokens", "lifetimeTokens");
        s_worker_snapshot.account_tokens.latest_day_tokens =
            object_u64(tokens, "latest_day_tokens", "latestDayTokens");
        s_worker_snapshot.account_tokens.peak_daily_tokens =
            object_u64(tokens, "peak_daily_tokens", "peakDailyTokens");
        const uint64_t streak_days =
            object_u64(
                tokens,
                "current_streak_days",
                "currentStreakDays");
        s_worker_snapshot.account_tokens.current_streak_days =
            static_cast<uint16_t>(
                streak_days > UINT16_MAX ? UINT16_MAX : streak_days);
        copy_text(
            s_worker_snapshot.account_tokens.latest_day_label,
            sizeof(s_worker_snapshot.account_tokens.latest_day_label),
            object_text(tokens, "latest_day_label", "latestDayLabel"));
    }

    memset(
        s_worker_snapshot.sessions,
        0,
        sizeof(s_worker_snapshot.sessions));
    s_worker_snapshot.session_count = 0;
    JsonArrayConst sessions = root["sessions"].as<JsonArrayConst>();
    for (JsonObjectConst object : sessions) {
        if (s_worker_snapshot.session_count >=
            VELA_MAX_BRIDGE_SESSIONS) {
            break;
        }
        BridgeSession &session =
            s_worker_snapshot.sessions[
                s_worker_snapshot.session_count];
        copy_text(
            session.thread_id,
            sizeof(session.thread_id),
            object_text(object, "thread_id", "threadId"));
        if (session.thread_id[0] == '\0') {
            continue;
        }
        copy_text(
            session.title,
            sizeof(session.title),
            object_text(object, "title", nullptr));
        copy_text(
            session.summary,
            sizeof(session.summary),
            object_text(object, "summary", nullptr));
        session.state = parse_session_state(
            object_text(object, "state", "status"));
        session.total_tokens =
            object_u64(object, "total_tokens", "totalTokens");
        session.last_tokens =
            object_u64(object, "last_tokens", "lastTokens");
        session.context_window_tokens =
            object_u64(
                object,
                "context_window_tokens",
                "contextWindowTokens");
        JsonVariantConst context_percent =
            object["context_used_percent"];
        if (context_percent.isNull()) {
            context_percent = object["contextUsedPercent"];
        }
        session.context_usage_valid =
            !context_percent.isNull() &&
            session.context_window_tokens > 0;
        session.context_used_percent =
            session.context_usage_valid
                ? clamp_percent(context_percent.as<uint64_t>())
                : 0;
        session.needs_feedback =
            object_bool(
                object,
                "needs_feedback",
                "needsFeedback",
                false);
        parse_approval(
            object_member(object, "approval", "currentApproval"),
            session.thread_id,
            &session.approval);
        ++s_worker_snapshot.session_count;
    }
    s_worker_snapshot.total_session_count =
        static_cast<uint16_t>(
            object_u64(
                root,
                "total_session_count",
                "totalSessionCount",
                sessions.size()));

    s_worker_snapshot.pending_approval_count =
        static_cast<uint16_t>(
            object_u64(
                root,
                "pending_approval_count",
                "pendingApprovalCount"));
    JsonObjectConst current_approval = object_member(
        root,
        "current_approval",
        "currentApproval",
        "globalApproval");
    parse_approval(
        current_approval,
        nullptr,
        &s_worker_snapshot.current_approval);
    if (s_worker_snapshot.current_approval.present &&
        s_worker_snapshot.pending_approval_count == 0) {
        s_worker_snapshot.pending_approval_count = 1;
    }

    update_mutation_from_snapshot(root);
    if (error != nullptr && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

String percent_encode(const char *text)
{
    static const char kHex[] = "0123456789ABCDEF";
    String encoded;
    if (text == nullptr) {
        return encoded;
    }
    encoded.reserve(strlen(text) * 3U);
    for (const uint8_t *cursor =
             reinterpret_cast<const uint8_t *>(text);
         *cursor != 0;
         ++cursor) {
        const uint8_t value = *cursor;
        const bool unreserved =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '-' || value == '_' || value == '.' || value == '~';
        if (unreserved) {
            encoded += static_cast<char>(value);
        } else {
            encoded += '%';
            encoded += kHex[(value >> 4U) & 0x0FU];
            encoded += kHex[value & 0x0FU];
        }
    }
    return encoded;
}

bool prepare_http(
    HTTPClient *http,
    WiFiClient *client,
    const DeviceConfig &config,
    const char *path)
{
    if (http == nullptr || client == nullptr || path == nullptr) {
        return false;
    }
    if (!http->begin(
            *client,
            String(config.bridge_host),
            config.bridge_port,
            String(path),
            false)) {
        return false;
    }
    http->setConnectTimeout(kHttpConnectTimeoutMs);
    http->setTimeout(kHttpReadTimeoutMs);
    http->setReuse(false);
    http->setUserAgent(F("Vela-Dial/1"));
    String authorization(F("Bearer "));
    authorization += config.bridge_token;
    http->addHeader(F("Authorization"), authorization);
    char device_id[20] = {};
    if (connectivity_service_get_device_id(
            device_id, sizeof(device_id))) {
        http->addHeader(F("X-Vela-Device-Id"), device_id);
    }
    return true;
}

void parse_operation_response(
    const String &payload,
    char *operation_id,
    size_t operation_id_size,
    char *result_thread_id,
    size_t result_thread_id_size,
    char *message,
    size_t message_size)
{
    if (payload.length() == 0) {
        return;
    }
    JsonDocument document;
    if (deserializeJson(document, payload)) {
        return;
    }
    JsonObjectConst root = document.as<JsonObjectConst>();
    copy_text(
        operation_id,
        operation_id_size,
        object_text(root, "operation_id", "operationId"));
    const char *thread_id =
        object_text(root, "result_thread_id", "resultThreadId");
    if (thread_id[0] == '\0') {
        thread_id = object_text(root, "thread_id", "threadId");
    }
    copy_text(
        result_thread_id,
        result_thread_id_size,
        thread_id);
    copy_text(
        message,
        message_size,
        object_text(root, "message", nullptr));
}

bool fetch_snapshot(uint32_t now_ms)
{
    DeviceConfig config = {};
    if (!connectivity_service_get_config(&config)) {
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    if (!prepare_http(
            &http,
            &client,
            config,
            "/api/v1/device/snapshot")) {
        mark_bridge_failure(-1, "Could not create snapshot request");
        return false;
    }
    static const char *kResponseHeaders[] = {"ETag"};
    http.collectHeaders(kResponseHeaders, 1);
    http.addHeader(F("Accept"), F("application/json"));
    if (s_snapshot_etag.length() != 0) {
        http.addHeader(F("If-None-Match"), s_snapshot_etag);
    }

    const int status = http.GET();
    Serial.printf(
        "[BRIDGE] Snapshot GET status=%d wifi=%d host=%s\n",
        status,
        WiFi.status() == WL_CONNECTED ? 1 : 0,
        config.bridge_host);
    if (status == HTTP_CODE_NOT_MODIFIED) {
        mark_bridge_online(status);
        http.end();
        publish_snapshot();
        s_next_snapshot_ms = now_ms + kSnapshotPollMs;
        return true;
    }
    if (status != HTTP_CODE_OK) {
        char message[96];
        snprintf(
            message,
            sizeof(message),
            "Snapshot request failed (%d)",
            status);
        http.end();
        mark_bridge_failure(status, message);
        return false;
    }

    const String payload = http.getString();
    Serial.printf(
        "[BRIDGE] Snapshot payload=%u bytes\n",
        static_cast<unsigned>(payload.length()));
    const String etag = http.header("ETag");
    http.end();
    char parse_error[96] = {};
    if (!parse_snapshot_payload(
            payload, parse_error, sizeof(parse_error))) {
        Serial.printf(
            "[BRIDGE] Snapshot parse failed: %s\n",
            parse_error);
        char message[128];
        snprintf(
            message,
            sizeof(message),
            "Invalid snapshot: %.92s",
            parse_error);
        mark_bridge_failure(status, message);
        return false;
    }
    if (etag.length() != 0) {
        s_snapshot_etag = etag;
    }
    mark_bridge_online(status);
    Serial.printf(
        "[BRIDGE] Snapshot ready quota=%u tokens=%d sessions=%u\n",
        static_cast<unsigned>(s_worker_snapshot.quota_window_count),
        s_worker_snapshot.account_tokens.valid ? 1 : 0,
        static_cast<unsigned>(s_worker_snapshot.session_count));
    publish_snapshot();

    const BridgeOperationState operation_state =
        s_worker_snapshot.operation.state;
    const bool operation_busy =
        operation_state == BridgeOperationState::Queued ||
        operation_state == BridgeOperationState::Running;
    s_next_snapshot_ms =
        now_ms +
        (operation_busy ? kBusySnapshotPollMs : kSnapshotPollMs);
    return true;
}

bool accepted_http_status(int status)
{
    return status == HTTP_CODE_OK ||
           status == HTTP_CODE_CREATED ||
           status == HTTP_CODE_ACCEPTED ||
           status == HTTP_CODE_NO_CONTENT;
}

void execute_upload(
    const BridgeCommand &command,
    uint32_t now_ms)
{
    set_operation(
        BridgeOperationKind::UploadRecording,
        BridgeOperationState::Running,
        0,
        "Preparing recording");

    StagedRecording staged = {};
    char error[VELA_ERROR_TEXT_BYTES] = {};
    if (!recording_store_stage(
            command.recording_path,
            &staged,
            error,
            sizeof(error))) {
        set_operation(
            BridgeOperationKind::UploadRecording,
            BridgeOperationState::Failed,
            0,
            error);
        s_pending_upload_retry_ms = now_ms + kUploadRetryMs;
        return;
    }

    char device_id[20] = {};
    PendingRecordingUpload pending = {};
    const bool identity_ready =
        connectivity_service_get_device_id(
            device_id, sizeof(device_id)) &&
        recording_store_get_or_create_pending(
            device_id,
            staged,
            command.audio_purpose,
            command.thread_id,
            command.approval_id,
            &pending,
            error,
            sizeof(error));
    if (!identity_ready) {
        recording_store_release(&staged);
        set_operation(
            BridgeOperationKind::UploadRecording,
            BridgeOperationState::Failed,
            0,
            error[0] == '\0'
                ? "Could not create upload identity"
                : error);
        s_pending_upload_retry_ms = now_ms + kUploadRetryMs;
        return;
    }

    DeviceConfig config = {};
    if (!connectivity_service_get_config(&config)) {
        recording_store_release(&staged);
        set_operation(
            BridgeOperationKind::UploadRecording,
            BridgeOperationState::Failed,
            0,
            "Bridge configuration unavailable");
        s_pending_upload_retry_ms = now_ms + kUploadRetryMs;
        return;
    }

    WiFiClient client;
    HTTPClient http;
    if (!prepare_http(
            &http,
            &client,
            config,
            "/api/v1/device/recordings")) {
        recording_store_release(&staged);
        set_operation(
            BridgeOperationKind::UploadRecording,
            BridgeOperationState::Failed,
            -1,
            "Could not create upload request");
        s_pending_upload_retry_ms = now_ms + kUploadRetryMs;
        return;
    }
    // A Bridge may either return 202 immediately or synchronously wait for a
    // recording ASR result. Volcengine recognition can legitimately approach
    // 45 seconds, so only this endpoint receives the longer read timeout.
    http.setTimeout(kRecordingHttpReadTimeoutMs);

    http.addHeader(F("Content-Type"), F("audio/wav"));
    http.addHeader(
        F("Idempotency-Key"),
        pending.idempotency_key);
    http.addHeader(
        F("X-Vela-Purpose"),
        pending.purpose == BridgeAudioPurpose::NewSession
            ? F("new_session")
            : F("reject_reason"));
    if (pending.thread_id[0] != '\0') {
        http.addHeader(F("X-Vela-Thread-Id"), pending.thread_id);
    }
    if (pending.approval_id[0] != '\0') {
        http.addHeader(
            F("X-Vela-Approval-Id"), pending.approval_id);
    }
    char crc_text[12];
    snprintf(
        crc_text,
        sizeof(crc_text),
        "%08lx",
        static_cast<unsigned long>(pending.wav_crc32));
    http.addHeader(F("X-Vela-Wav-Crc32"), crc_text);

    const int status = http.POST(staged.data, staged.size);
    const String response =
        status > 0 && status != HTTP_CODE_NO_CONTENT
            ? http.getString()
            : String();
    http.end();
    recording_store_release(&staged);

    if (!accepted_http_status(status)) {
        char message[96];
        snprintf(
            message,
            sizeof(message),
            "Recording upload failed (%d)",
            status);
        mark_bridge_failure(status, message);
        set_operation(
            BridgeOperationKind::UploadRecording,
            BridgeOperationState::Failed,
            status,
            message);
        s_pending_upload_retry_ms = now_ms + kUploadRetryMs;
        return;
    }

    char operation_id[VELA_OPERATION_ID_BYTES] = {};
    char result_thread_id[VELA_THREAD_ID_BYTES] = {};
    char message[VELA_ERROR_TEXT_BYTES] = {};
    parse_operation_response(
        response,
        operation_id,
        sizeof(operation_id),
        result_thread_id,
        sizeof(result_thread_id),
        message,
        sizeof(message));
    const bool marker_removed =
        recording_store_complete_pending(
            pending.idempotency_key);
    mark_bridge_online(status);
    const BridgeOperationState operation_state =
        status == HTTP_CODE_ACCEPTED &&
                result_thread_id[0] == '\0'
            ? BridgeOperationState::Running
            : BridgeOperationState::Accepted;
    set_operation(
        BridgeOperationKind::UploadRecording,
        operation_state,
        status,
        message[0] != '\0'
            ? message
            : (marker_removed
                   ? "Recording accepted"
                   : "Recording accepted; cleanup pending"),
        operation_id,
        result_thread_id);
    s_pending_upload_retry_ms = 0;
    s_next_snapshot_ms = now_ms;
}

void execute_approval(
    const BridgeCommand &command,
    uint32_t now_ms)
{
    set_operation(
        BridgeOperationKind::ResolveApproval,
        BridgeOperationState::Running,
        0,
        "Submitting decision");

    DeviceConfig config = {};
    if (!connectivity_service_get_config(&config)) {
        set_operation(
            BridgeOperationKind::ResolveApproval,
            BridgeOperationState::Failed,
            0,
            "Bridge configuration unavailable");
        return;
    }

    const String encoded_approval =
        percent_encode(command.approval_id);
    String path(F("/api/v1/device/approvals/"));
    path += encoded_approval;
    path += F("/resolve");

    WiFiClient client;
    HTTPClient http;
    if (!prepare_http(
            &http, &client, config, path.c_str())) {
        set_operation(
            BridgeOperationKind::ResolveApproval,
            BridgeOperationState::Failed,
            -1,
            "Could not create approval request");
        return;
    }

    JsonDocument document;
    document["decision"] =
        command.approval_decision == BridgeApprovalDecision::Allow
            ? "allow"
            : "reject";
    document["thread_id"] = command.thread_id;
    document["approval_id"] = command.approval_id;
    if (command.nonce[0] != '\0') {
        document["nonce"] = command.nonce;
    }
    if (command.action_digest[0] != '\0') {
        document["action_digest"] = command.action_digest;
    }
    String body;
    serializeJson(document, body);

    char device_id[20] = {};
    (void)connectivity_service_get_device_id(
        device_id, sizeof(device_id));
    uint32_t decision_hash = fnv1a_text(command.approval_id);
    decision_hash = fnv1a_text(
        command.action_digest, decision_hash);
    decision_hash ^= static_cast<uint32_t>(
        command.approval_decision);
    char idempotency_key[96];
    snprintf(
        idempotency_key,
        sizeof(idempotency_key),
        "approval-v1-%s-%08lx",
        device_id,
        static_cast<unsigned long>(decision_hash));
    http.addHeader(F("Content-Type"), F("application/json"));
    http.addHeader(F("Idempotency-Key"), idempotency_key);

    const int status = http.POST(body);
    const String response =
        status > 0 && status != HTTP_CODE_NO_CONTENT
            ? http.getString()
            : String();
    http.end();

    if (!accepted_http_status(status)) {
        char message[96];
        if (status == HTTP_CODE_GONE) {
            copy_text(
                message,
                sizeof(message),
                "Approval expired");
        } else if (status == HTTP_CODE_CONFLICT) {
            copy_text(
                message,
                sizeof(message),
                "Approval is stale");
        } else {
            snprintf(
                message,
                sizeof(message),
                "Approval request failed (%d)",
                status);
        }
        if (status <= 0 || status >= 500) {
            mark_bridge_failure(status, message);
        }
        set_operation(
            BridgeOperationKind::ResolveApproval,
            BridgeOperationState::Failed,
            status,
            message);
        s_next_snapshot_ms = now_ms;
        return;
    }

    char operation_id[VELA_OPERATION_ID_BYTES] = {};
    char result_thread_id[VELA_THREAD_ID_BYTES] = {};
    char message[VELA_ERROR_TEXT_BYTES] = {};
    parse_operation_response(
        response,
        operation_id,
        sizeof(operation_id),
        result_thread_id,
        sizeof(result_thread_id),
        message,
        sizeof(message));
    mark_bridge_online(status);
    const BridgeOperationState operation_state =
        status == HTTP_CODE_ACCEPTED
            ? BridgeOperationState::Running
            : BridgeOperationState::Accepted;
    set_operation(
        BridgeOperationKind::ResolveApproval,
        operation_state,
        status,
        message[0] != '\0' ? message : "Decision accepted",
        operation_id,
        result_thread_id);
    s_next_snapshot_ms = now_ms;
}

void execute_command(
    const BridgeCommand &command,
    uint32_t now_ms)
{
    switch (command.kind) {
        case BridgeOperationKind::RefreshSnapshot:
            // Refresh is read-only and deliberately does not overwrite the
            // last device mutation in snapshot.operation.
            s_next_snapshot_ms = now_ms;
            break;
        case BridgeOperationKind::UploadRecording:
            execute_upload(command, now_ms);
            break;
        case BridgeOperationKind::ResolveApproval:
            execute_approval(command, now_ms);
            break;
        case BridgeOperationKind::StartProvisioning:
            connectivity_service_start_provisioning(
                "Configuration requested");
            set_operation(
                BridgeOperationKind::StartProvisioning,
                BridgeOperationState::Accepted,
                0,
                "Setup access point started");
            break;
        case BridgeOperationKind::ForgetConfiguration:
            connectivity_service_forget_configuration();
            s_snapshot_etag = "";
            set_operation(
                BridgeOperationKind::ForgetConfiguration,
                BridgeOperationState::Accepted,
                0,
                "Configuration cleared");
            break;
        case BridgeOperationKind::None:
            break;
    }
}

bool recover_pending_upload(uint32_t now_ms)
{
    if (s_pending_upload_retry_ms != 0 &&
        !due(now_ms, s_pending_upload_retry_ms)) {
        return false;
    }
    PendingRecordingUpload pending = {};
    if (!recording_store_load_pending(&pending)) {
        s_pending_upload_retry_ms = now_ms + 60000U;
        return false;
    }
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::UploadRecording;
    command.audio_purpose = pending.purpose;
    copy_text(
        command.recording_path,
        sizeof(command.recording_path),
        pending.path);
    copy_text(
        command.thread_id,
        sizeof(command.thread_id),
        pending.thread_id);
    copy_text(
        command.approval_id,
        sizeof(command.approval_id),
        pending.approval_id);
    execute_upload(command, now_ms);
    return true;
}

void update_connectivity()
{
    ConnectivitySnapshot connectivity = {};
    if (!connectivity_service_get_snapshot(&connectivity) ||
        connectivity.revision == s_connectivity_revision) {
        return;
    }
    s_connectivity_revision = connectivity.revision;
    s_worker_snapshot.connectivity = connectivity;
    if (!connectivity.wifi_connected) {
        s_worker_snapshot.bridge_online = false;
        s_worker_snapshot.last_http_status = 0;
        copy_text(
            s_worker_snapshot.bridge_error,
            sizeof(s_worker_snapshot.bridge_error),
            connectivity.phase == ConnectivityPhase::AccessPoint
                ? "Waiting for device configuration"
                : "Wi-Fi unavailable");
    }
    publish_snapshot();
}

void network_task(void *)
{
    if (!connectivity_service_begin()) {
        copy_text(
            s_worker_snapshot.bridge_error,
            sizeof(s_worker_snapshot.bridge_error),
            "Connectivity service failed to start");
        publish_snapshot();
        vTaskDelete(nullptr);
    }

    for (;;) {
        const uint32_t now = millis();
        connectivity_service_step(now);
        update_connectivity();

        BridgeCommand command = {};
        const bool have_command =
            xQueueReceive(s_command_queue, &command, 0) == pdTRUE;
        const bool online =
            s_worker_snapshot.connectivity.phase ==
                ConnectivityPhase::Online &&
            s_worker_snapshot.connectivity.wifi_connected;
        if (have_command) {
            // Uploads always run through staging first, even while offline.
            // That creates the persistent idempotency record needed for an
            // automatic retry after reconnect or reboot. Approvals fail
            // closed because their nonce/expiry may become stale offline.
            if (command.kind ==
                    BridgeOperationKind::ResolveApproval &&
                !online) {
                set_operation(
                    command.kind,
                    BridgeOperationState::Failed,
                    0,
                    "Wi-Fi is offline");
            } else {
                execute_command(command, now);
            }
        } else if (online) {
            if (!recover_pending_upload(now) &&
                due(now, s_next_snapshot_ms)) {
                fetch_snapshot(now);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kWorkerTickMs));
    }
}

bool enqueue_command(const BridgeCommand &command)
{
    return s_started &&
           s_command_queue != nullptr &&
           xQueueSend(s_command_queue, &command, 0) == pdTRUE;
}

}  // namespace

bool bridge_client_begin()
{
    if (s_started) {
        return true;
    }
    s_snapshot_mutex = xSemaphoreCreateMutex();
    s_command_queue =
        xQueueCreate(kCommandQueueDepth, sizeof(BridgeCommand));
    if (s_snapshot_mutex == nullptr || s_command_queue == nullptr) {
        if (s_snapshot_mutex != nullptr) {
            vSemaphoreDelete(s_snapshot_mutex);
            s_snapshot_mutex = nullptr;
        }
        if (s_command_queue != nullptr) {
            vQueueDelete(s_command_queue);
            s_command_queue = nullptr;
        }
        return false;
    }

    memset(&s_worker_snapshot, 0, sizeof(s_worker_snapshot));
    memset(&s_published_snapshot, 0, sizeof(s_published_snapshot));
    s_worker_snapshot.connectivity.phase =
        ConnectivityPhase::Uninitialized;
    s_worker_snapshot.operation.kind = BridgeOperationKind::None;
    s_worker_snapshot.operation.state =
        BridgeOperationState::Idle;
    publish_snapshot();

    s_started = true;
    const BaseType_t created = xTaskCreatePinnedToCore(
        network_task,
        "vela_network",
        kNetworkTaskStackBytes,
        nullptr,
        kNetworkTaskPriority,
        &s_network_task,
        kNetworkTaskCore);
    if (created != pdPASS) {
        s_started = false;
        vQueueDelete(s_command_queue);
        s_command_queue = nullptr;
        vSemaphoreDelete(s_snapshot_mutex);
        s_snapshot_mutex = nullptr;
        return false;
    }
    return true;
}

bool bridge_client_get_snapshot(BridgeSnapshot *snapshot)
{
    if (snapshot == nullptr || s_snapshot_mutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(30)) != pdTRUE) {
        return false;
    }
    *snapshot = s_published_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
    return true;
}

bool bridge_client_request_refresh()
{
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::RefreshSnapshot;
    return enqueue_command(command);
}

bool bridge_client_upload_recording(
    const char *recording_path,
    BridgeAudioPurpose purpose,
    const char *thread_id,
    const char *approval_id)
{
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::UploadRecording;
    command.audio_purpose = purpose;
    if (!copy_checked(
            command.recording_path,
            sizeof(command.recording_path),
            recording_path,
            true) ||
        !copy_checked(
            command.thread_id,
            sizeof(command.thread_id),
            thread_id,
            purpose == BridgeAudioPurpose::RejectReason) ||
        !copy_checked(
            command.approval_id,
            sizeof(command.approval_id),
            approval_id,
            false)) {
        return false;
    }
    return enqueue_command(command);
}

bool bridge_client_resolve_approval(
    const char *thread_id,
    const char *approval_id,
    const char *nonce,
    const char *action_digest,
    BridgeApprovalDecision decision)
{
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::ResolveApproval;
    command.approval_decision = decision;
    if (!copy_checked(
            command.thread_id,
            sizeof(command.thread_id),
            thread_id,
            true) ||
        !copy_checked(
            command.approval_id,
            sizeof(command.approval_id),
            approval_id,
            true) ||
        !copy_checked(
            command.nonce,
            sizeof(command.nonce),
            nonce,
            false) ||
        !copy_checked(
            command.action_digest,
            sizeof(command.action_digest),
            action_digest,
            false)) {
        return false;
    }
    return enqueue_command(command);
}

bool bridge_client_start_provisioning()
{
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::StartProvisioning;
    return enqueue_command(command);
}

bool bridge_client_forget_configuration()
{
    BridgeCommand command = {};
    command.kind = BridgeOperationKind::ForgetConfiguration;
    return enqueue_command(command);
}
