#include "app_ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board_hardware.h"
#include "bridge_client.h"
#include "bridge_models.h"
#include "knob.h"

namespace {

constexpr int kScreenSize = 360;
constexpr uint32_t kDwellMs = 3000;
constexpr uint32_t kBackConfirmMs = 1400;
constexpr uint32_t kVoiceStartGraceMs = 650;
constexpr uint32_t kVoiceSilenceMs = 2000;
constexpr uint32_t kVoiceNoSpeechMs = 10000;
constexpr uint32_t kVoiceMaxMs = 30000;
constexpr uint32_t kRecordingStartTimeoutMs = 3000;
constexpr uint32_t kAudioStaleMs = 1800;
constexpr uint32_t kVoiceFinalizeMaxMs = 2500;
constexpr uint32_t kResultMs = 1250;
constexpr uint32_t kUiRefreshMs = 80;
constexpr float kSpeechThresholdDb = -38.0f;
constexpr float kSpeechContinueThresholdDb = -44.0f;
constexpr size_t kMaxSessions = 5;

const lv_color_t kBg = lv_color_hex(0x070B12);
const lv_color_t kPanel = lv_color_hex(0x101A27);
const lv_color_t kPanelRaised = lv_color_hex(0x172536);
const lv_color_t kBorder = lv_color_hex(0x26394D);
const lv_color_t kText = lv_color_hex(0xF4F8FC);
const lv_color_t kMuted = lv_color_hex(0x879CAF);
const lv_color_t kCyan = lv_color_hex(0x42DED7);
const lv_color_t kBlue = lv_color_hex(0x54A7FF);
const lv_color_t kGreen = lv_color_hex(0x56DFA5);
const lv_color_t kAmber = lv_color_hex(0xFFBE5C);
const lv_color_t kRed = lv_color_hex(0xFF6F7D);
const lv_color_t kPurple = lv_color_hex(0x9A83FF);

const lv_font_t *const kCjk = &lv_font_montserrat_16;

enum class Surface : uint8_t {
    Provisioning,
    Connecting,
    BridgeOffline,
    Quota,
    Sessions,
    Detail,
    Voice,
    Approval,
    Sending,
    Result,
    Error,
};

enum class SessionStatus : uint8_t {
    WaitingApproval,
    Running,
    Complete,
    Failed,
};

enum class DwellAction : uint8_t {
    None,
    NewSession,
    OpenSession,
    OpenApproval,
    OpenRejectVoice,
    SubmitAllow,
    SubmitReject,
};

enum class VoicePurpose : uint8_t {
    NewSession,
    RejectReason,
};

enum class VoiceStage : uint8_t {
    Listening,
    Finalizing,
};

enum class PendingMutation : uint8_t {
    None,
    UploadNewSession,
    UploadRejectReason,
    ResolveAllow,
    ResolveReject,
};

struct SessionRecord {
    char thread_id[VELA_THREAD_ID_BYTES];
    char title[32];
    char summary[72];
    SessionStatus status;
    bool pending_approval;
    bool needs_feedback;
    BridgeApproval approval;
};

struct DwellState {
    DwellAction action;
    uint32_t started_ms;
    uint32_t generation;
    char thread_id[VELA_THREAD_ID_BYTES];
};

SessionRecord s_sessions[kMaxSessions] = {};
size_t s_session_count = 0;

lv_obj_t *s_root = nullptr;
lv_obj_t *s_dwell_bar = nullptr;
lv_obj_t *s_voice_bars[5] = {};
lv_obj_t *s_voice_status = nullptr;
lv_obj_t *s_voice_silence_bar = nullptr;

Surface s_surface = Surface::Quota;
Surface s_result_next = Surface::Detail;
Surface s_error_next = Surface::Sessions;
SessionRecord *s_selected_session = nullptr;
char s_selected_thread_id[VELA_THREAD_ID_BYTES] = {};
uint8_t s_quota_index = 0;
int s_session_index = 0;
DwellState s_dwell = {};
uint32_t s_dwell_generation = 0;
bool s_back_armed = false;
uint32_t s_back_until_ms = 0;

VoicePurpose s_voice_purpose = VoicePurpose::NewSession;
VoiceStage s_voice_stage = VoiceStage::Listening;
uint32_t s_voice_started_ms = 0;
uint32_t s_last_speech_ms = 0;
uint32_t s_voice_finalize_started_ms = 0;
bool s_speech_seen = false;
bool s_recording_requested = false;
bool s_recording_started = false;
uint32_t s_last_audio_frame = 0;
uint32_t s_last_audio_frame_ms = 0;

BridgeSnapshot s_bridge_snapshot = {};
// BridgeSnapshot is several kilobytes. Keep the copy buffer out of Arduino's
// loopTask stack; nesting it with session rebuilding can otherwise overflow
// the default loop stack during the first UI render.
BridgeSnapshot s_bridge_snapshot_scratch = {};
uint32_t s_bridge_generation = 0;
uint32_t s_connectivity_revision = 0;
uint64_t s_remote_revision = 0;
PendingMutation s_pending_mutation = PendingMutation::None;
BridgeOperationKind s_pending_operation_kind = BridgeOperationKind::None;
uint32_t s_pending_operation_baseline_ms = 0;
bool s_pending_operation_seen = false;
char s_pending_operation_baseline_id[VELA_OPERATION_ID_BYTES] = {};
char s_pending_operation_id[VELA_OPERATION_ID_BYTES] = {};
char s_pending_approval_id[VELA_APPROVAL_ID_BYTES] = {};
char s_reject_thread_id[VELA_THREAD_ID_BYTES] = {};
char s_reject_approval_id[VELA_APPROVAL_ID_BYTES] = {};
char s_resolved_approval_id[VELA_APPROVAL_ID_BYTES] = {};
char s_presented_approval_id[VELA_APPROVAL_ID_BYTES] = {};
Surface s_network_return_surface = Surface::Quota;

char s_result_title[32] = "DONE";
char s_result_subtitle[72] = "";
lv_color_t s_result_color = kGreen;
uint32_t s_result_until_ms = 0;

char s_error_title[40] = "NO SPEECH";
char s_error_subtitle[80] = "";
uint32_t s_last_ui_refresh_ms = 0;

void render_surface();
void open_sessions(bool select_new, bool arm_selection);
void enter_detail(SessionRecord *session);
void enter_voice(VoicePurpose purpose);
void enter_approval();
void sync_bridge_snapshot(bool force_render);
void process_bridge_operation();
int list_item_count();
SessionRecord *session_for_list_index(int index);
SessionRecord *find_session(const char *thread_id);
int find_session_list_index(const char *thread_id);
bool same_text(const char *left, const char *right);
bool approval_is_resolved_locally(const BridgeApproval &approval);
void cancel_dwell();
void show_error(
    const char *title,
    const char *subtitle,
    Surface next);

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }
    snprintf(destination, capacity, "%s", source == nullptr ? "" : source);
}

int clamp_int(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

float clamp_float(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

void set_plain(lv_obj_t *object)
{
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *make_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

lv_obj_t *make_centered_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color,
    int y,
    int width)
{
    lv_obj_t *label = make_label(parent, text, font, color);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

lv_obj_t *make_panel(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    lv_color_t background,
    lv_color_t border,
    int border_width,
    int radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}

lv_obj_t *make_dot(
    lv_obj_t *parent,
    int x,
    int y,
    int diameter,
    lv_color_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

lv_obj_t *make_progress_bar(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    lv_color_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, kBorder, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

void render_header(const char *page_name)
{
    make_centered_label(
        s_root, "VELA / CODEX", &lv_font_montserrat_14, kMuted, 15, 180);
    make_centered_label(s_root, page_name, kCjk, kText, 40, 190);
}

const char *status_text(SessionStatus status)
{
    switch (status) {
        case SessionStatus::WaitingApproval:
            return "WAIT";
        case SessionStatus::Running:
            return "LIVE";
        case SessionStatus::Complete:
            return "DONE";
        case SessionStatus::Failed:
            return "ERROR";
    }
    return "";
}

lv_color_t status_color(SessionStatus status)
{
    switch (status) {
        case SessionStatus::WaitingApproval:
            return kAmber;
        case SessionStatus::Running:
            return kBlue;
        case SessionStatus::Complete:
            return kGreen;
        case SessionStatus::Failed:
            return kRed;
    }
    return kMuted;
}

SessionStatus session_status_from_bridge(BridgeSessionState status)
{
    switch (status) {
        case BridgeSessionState::WaitingApproval:
            return SessionStatus::WaitingApproval;
        case BridgeSessionState::Running:
            return SessionStatus::Running;
        case BridgeSessionState::Complete:
            return SessionStatus::Complete;
        case BridgeSessionState::Failed:
            return SessionStatus::Failed;
        case BridgeSessionState::Unknown:
            return SessionStatus::Running;
    }
    return SessionStatus::Running;
}

void import_session(
    SessionRecord *destination,
    const BridgeSession *source,
    const BridgeApproval *global_approval)
{
    if (destination == nullptr) {
        return;
    }
    memset(destination, 0, sizeof(*destination));

    if (source != nullptr) {
        copy_text(
            destination->thread_id,
            sizeof(destination->thread_id),
            source->thread_id);
        copy_text(
            destination->title,
            sizeof(destination->title),
            source->title[0] == '\0'
                ? "CODEX SESSION"
                : source->title);
        copy_text(
            destination->summary,
            sizeof(destination->summary),
            source->summary[0] == '\0'
                ? "Waiting for Codex activity"
                : source->summary);
        destination->status =
            session_status_from_bridge(source->state);
        destination->pending_approval =
            source->approval.present &&
            !approval_is_resolved_locally(source->approval);
        destination->needs_feedback = source->needs_feedback;
        destination->approval = source->approval;
    } else {
        destination->status = SessionStatus::WaitingApproval;
        copy_text(
            destination->title,
            sizeof(destination->title),
            "CODEX REQUEST");
        copy_text(
            destination->summary,
            sizeof(destination->summary),
            "Approval requires attention");
    }

    if (global_approval != nullptr &&
        global_approval->present &&
        !approval_is_resolved_locally(*global_approval)) {
        copy_text(
            destination->thread_id,
            sizeof(destination->thread_id),
            global_approval->thread_id);
        destination->status = SessionStatus::WaitingApproval;
        destination->pending_approval = true;
        destination->approval = *global_approval;
        if (source == nullptr) {
            copy_text(
                destination->title,
                sizeof(destination->title),
                global_approval->title[0] == '\0'
                    ? "CODEX REQUEST"
                    : global_approval->title);
            copy_text(
                destination->summary,
                sizeof(destination->summary),
                global_approval->detail[0] == '\0'
                    ? "Approval requires attention"
                    : global_approval->detail);
        }
    }
}

bool is_network_surface(Surface surface)
{
    return surface == Surface::Provisioning ||
           surface == Surface::Connecting ||
           surface == Surface::BridgeOffline;
}

bool may_show_network_surface()
{
    return s_surface != Surface::Voice &&
           s_surface != Surface::Sending &&
           s_surface != Surface::Result &&
           s_surface != Surface::Error;
}

Surface required_network_surface()
{
    const ConnectivitySnapshot &connectivity =
        s_bridge_snapshot.connectivity;
    if (connectivity.phase == ConnectivityPhase::Uninitialized ||
        connectivity.phase == ConnectivityPhase::LoadingConfig ||
        connectivity.phase == ConnectivityPhase::Connecting) {
        return Surface::Connecting;
    }
    if (connectivity.phase == ConnectivityPhase::AccessPoint ||
        !connectivity.configured) {
        return Surface::Provisioning;
    }
    if (connectivity.phase == ConnectivityPhase::Error ||
        !connectivity.wifi_connected ||
        !s_bridge_snapshot.bridge_online) {
        return Surface::BridgeOffline;
    }
    return Surface::Quota;
}

void rebuild_sessions_from_bridge()
{
    char highlighted_thread[VELA_THREAD_ID_BYTES] = {};
    SessionRecord *highlighted =
        session_for_list_index(s_session_index);
    if (highlighted != nullptr) {
        copy_text(
            highlighted_thread,
            sizeof(highlighted_thread),
            highlighted->thread_id);
    }

    // Rebuild directly into the persistent array. A second five-entry array
    // here adds several more kilobytes to loopTask's stack because every
    // SessionRecord embeds the signed approval payload.
    memset(s_sessions, 0, sizeof(s_sessions));
    size_t rebuilt_count = 0;
    const BridgeApproval *global = nullptr;
    if (s_bridge_snapshot.current_approval.present &&
        !approval_is_resolved_locally(
            s_bridge_snapshot.current_approval)) {
        global = &s_bridge_snapshot.current_approval;
        const BridgeSession *matching = nullptr;
        for (uint8_t index = 0;
             index < s_bridge_snapshot.session_count &&
             index < VELA_MAX_BRIDGE_SESSIONS;
             ++index) {
            if (same_text(
                    s_bridge_snapshot.sessions[index].thread_id,
                    global->thread_id)) {
                matching = &s_bridge_snapshot.sessions[index];
                break;
            }
        }
        import_session(&s_sessions[rebuilt_count++], matching, global);
    }

    for (uint8_t index = 0;
         index < s_bridge_snapshot.session_count &&
         index < VELA_MAX_BRIDGE_SESSIONS &&
         rebuilt_count < kMaxSessions;
         ++index) {
        const BridgeSession &source =
            s_bridge_snapshot.sessions[index];
        if (global != nullptr &&
            same_text(source.thread_id, global->thread_id)) {
            continue;
        }
        import_session(&s_sessions[rebuilt_count++], &source, nullptr);
    }

    s_session_count = rebuilt_count;

    s_selected_session = find_session(s_selected_thread_id);

    if (highlighted_thread[0] != '\0') {
        const int stable_index =
            find_session_list_index(highlighted_thread);
        if (stable_index >= 0) {
            s_session_index = stable_index;
        } else {
            s_session_index = clamp_int(
                s_session_index,
                0,
                list_item_count() - 1);
        }
    } else {
        s_session_index = clamp_int(
            s_session_index,
            0,
            list_item_count() - 1);
    }

    if ((s_dwell.action == DwellAction::OpenSession ||
         s_dwell.action == DwellAction::OpenApproval ||
         s_dwell.action == DwellAction::OpenRejectVoice) &&
        s_dwell.thread_id[0] != '\0' &&
        find_session(s_dwell.thread_id) == nullptr &&
        !(global != nullptr &&
          same_text(global->thread_id, s_dwell.thread_id))) {
        cancel_dwell();
    }
}

int list_item_count()
{
    return static_cast<int>(s_session_count) + 1;
}

SessionRecord *session_for_list_index(int index)
{
    if (index <= 0 || index > static_cast<int>(s_session_count)) {
        return nullptr;
    }
    return &s_sessions[index - 1];
}

SessionRecord *find_session(const char *thread_id)
{
    if (thread_id == nullptr || thread_id[0] == '\0') {
        return nullptr;
    }
    for (size_t index = 0; index < s_session_count; ++index) {
        if (strcmp(s_sessions[index].thread_id, thread_id) == 0) {
            return &s_sessions[index];
        }
    }
    return nullptr;
}

int find_session_list_index(const char *thread_id)
{
    SessionRecord *session = find_session(thread_id);
    if (session == nullptr) {
        return -1;
    }
    return static_cast<int>(session - s_sessions) + 1;
}

bool same_text(const char *left, const char *right)
{
    return strcmp(left == nullptr ? "" : left,
                  right == nullptr ? "" : right) == 0;
}

bool approval_is_resolved_locally(const BridgeApproval &approval)
{
    return approval.present &&
           s_resolved_approval_id[0] != '\0' &&
           same_text(approval.approval_id, s_resolved_approval_id);
}

const BridgeApproval *active_approval()
{
    if (s_bridge_snapshot.current_approval.present &&
        !approval_is_resolved_locally(
            s_bridge_snapshot.current_approval)) {
        return &s_bridge_snapshot.current_approval;
    }
    if (s_selected_session != nullptr &&
        s_selected_session->approval.present &&
        !approval_is_resolved_locally(
            s_selected_session->approval)) {
        return &s_selected_session->approval;
    }
    return nullptr;
}

void reset_back_hint()
{
    s_back_armed = false;
    s_back_until_ms = 0;
}

void cancel_dwell()
{
    ++s_dwell_generation;
    memset(&s_dwell, 0, sizeof(s_dwell));
    s_dwell.action = DwellAction::None;
    s_dwell.generation = s_dwell_generation;
    s_dwell_bar = nullptr;
}

void start_dwell(DwellAction action, const char *thread_id = nullptr)
{
    ++s_dwell_generation;
    memset(&s_dwell, 0, sizeof(s_dwell));
    s_dwell.action = action;
    s_dwell.started_ms = millis();
    s_dwell.generation = s_dwell_generation;
    copy_text(
        s_dwell.thread_id,
        sizeof(s_dwell.thread_id),
        thread_id);
}

bool dwell_active(DwellAction action)
{
    return s_dwell.action == action &&
           s_dwell.generation == s_dwell_generation;
}

void render_return_hint()
{
    if (!s_back_armed) {
        return;
    }

    lv_obj_t *rail = lv_arc_create(s_root);
    lv_obj_set_pos(rail, 12, 12);
    lv_obj_set_size(rail, 336, 336);
    lv_arc_set_bg_angles(rail, 0, 126);
    lv_arc_set_rotation(rail, 117);
    lv_obj_set_style_arc_width(rail, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rail, kBorder, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(rail, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rail, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(rail, kCyan, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(rail, true, LV_PART_INDICATOR);
    lv_arc_set_range(rail, 0, 100);
    lv_arc_set_value(rail, 58);
    lv_obj_remove_style(rail, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *arrow =
        make_label(s_root, LV_SYMBOL_LEFT, &lv_font_montserrat_20, kCyan);
    lv_obj_set_pos(arrow, 13, 166);
    lv_obj_move_foreground(arrow);
}

void apply_led_state()
{
    switch (s_surface) {
        case Surface::Provisioning:
            board_rgb_set_color(0, 14, 30);
            break;
        case Surface::Connecting:
        case Surface::Sending:
            board_rgb_set_color(0, 18, 28);
            break;
        case Surface::BridgeOffline:
            board_rgb_set_color(24, 8, 0);
            break;
        case Surface::Quota:
        case Surface::Sessions:
        case Surface::Detail:
            board_rgb_set_color(0, 8, 16);
            break;
        case Surface::Voice:
            board_rgb_set_color(0, 18, 34);
            break;
        case Surface::Approval:
            if (dwell_active(DwellAction::SubmitAllow)) {
                board_rgb_set_color(0, 24, 10);
            } else if (dwell_active(DwellAction::SubmitReject)) {
                board_rgb_set_color(26, 2, 4);
            } else {
                board_rgb_set_color(26, 10, 0);
            }
            break;
        case Surface::Result:
            board_rgb_set_color(0, 24, 10);
            break;
        case Surface::Error:
            board_rgb_set_color(28, 5, 0);
            break;
    }
}

void render_spinner(
    int x,
    int y,
    int size,
    lv_color_t color)
{
    lv_obj_t *spinner = lv_spinner_create(s_root, 900, 76);
    lv_obj_set_pos(spinner, x, y);
    lv_obj_set_size(spinner, size, size);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, kBorder, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, color, LV_PART_INDICATOR);
    lv_obj_remove_style(spinner, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
}

void render_provisioning()
{
    render_header("VELA LINK");
    make_centered_label(
        s_root, "SET UP WI-FI", &lv_font_montserrat_24, kText, 78, 240);
    make_centered_label(
        s_root,
        "Connect your phone to",
        &lv_font_montserrat_14,
        kMuted,
        113,
        240);

    const ConnectivitySnapshot &connectivity =
        s_bridge_snapshot.connectivity;
    lv_obj_t *credentials =
        make_panel(s_root, 64, 143, 232, 100, kPanel, kCyan, 1, 18);
    make_centered_label(
        credentials,
        connectivity.access_point_ssid[0] == '\0'
            ? "Vela setup network"
            : connectivity.access_point_ssid,
        &lv_font_montserrat_20,
        kText,
        13,
        204);
    make_centered_label(
        credentials,
        connectivity.access_point_password[0] == '\0'
            ? "Starting secure AP..."
            : connectivity.access_point_password,
        &lv_font_montserrat_16,
        kCyan,
        47,
        204);
    make_centered_label(
        credentials,
        connectivity.access_point_ip[0] == '\0'
            ? "192.168.4.1"
            : connectivity.access_point_ip,
        &lv_font_montserrat_14,
        kMuted,
        73,
        204);

    make_centered_label(
        s_root,
        connectivity.error[0] == '\0'
            ? "The setup page opens automatically"
            : connectivity.error,
        &lv_font_montserrat_14,
        connectivity.error[0] == '\0' ? kMuted : kAmber,
        270,
        268);
}

void render_connecting()
{
    render_header("CONNECTING");
    render_spinner(111, 83, 138, kCyan);
    make_centered_label(
        s_root, "LINKING VELA", &lv_font_montserrat_20, kText, 135, 210);

    const ConnectivitySnapshot &connectivity =
        s_bridge_snapshot.connectivity;
    const char *ssid = connectivity.station_ssid[0] == '\0'
        ? "Loading saved network"
        : connectivity.station_ssid;
    make_centered_label(
        s_root, ssid, &lv_font_montserrat_16, kMuted, 246, 250);
    make_centered_label(
        s_root,
        "Wi-Fi first, then local Bridge",
        &lv_font_montserrat_14,
        kMuted,
        279,
        250);
}

void render_bridge_offline()
{
    render_header("CONNECTION");
    const ConnectivitySnapshot &connectivity =
        s_bridge_snapshot.connectivity;
    const bool wifi_online = connectivity.wifi_connected;
    const lv_color_t accent = wifi_online ? kAmber : kRed;

    lv_obj_t *disc =
        make_panel(s_root, 128, 81, 104, 104, kPanel, accent, 2, 52);
    lv_obj_t *mark = make_label(
        disc,
        wifi_online ? "B" : "!",
        &lv_font_montserrat_32,
        accent);
    lv_obj_center(mark);

    make_centered_label(
        s_root,
        wifi_online ? "BRIDGE OFFLINE" : "WI-FI OFFLINE",
        &lv_font_montserrat_24,
        kText,
        210,
        260);
    make_centered_label(
        s_root,
        wifi_online
            ? (connectivity.bridge_host[0] == '\0'
                   ? "Local Bridge is unavailable"
                   : connectivity.bridge_host)
            : (connectivity.error[0] == '\0'
                   ? "Waiting for network"
                   : connectivity.error),
        &lv_font_montserrat_14,
        kMuted,
        250,
        260);
    make_centered_label(
        s_root,
        wifi_online ? "Start Vela Bridge on your Mac" : "Vela will retry",
        &lv_font_montserrat_14,
        accent,
        287,
        260);
}

const char *sending_title()
{
    switch (s_pending_mutation) {
        case PendingMutation::UploadNewSession:
            return "CREATING SESSION";
        case PendingMutation::UploadRejectReason:
            return "SENDING REASON";
        case PendingMutation::ResolveAllow:
            return "APPROVING";
        case PendingMutation::ResolveReject:
            return "DECLINING";
        case PendingMutation::None:
            return "SYNCING";
    }
    return "SYNCING";
}

void render_sending()
{
    render_header("CODEX");
    render_spinner(111, 82, 138, kCyan);
    make_centered_label(
        s_root,
        sending_title(),
        &lv_font_montserrat_20,
        kText,
        134,
        230);
    const bool upload_pending =
        s_pending_mutation == PendingMutation::UploadNewSession ||
        s_pending_mutation == PendingMutation::UploadRejectReason;
    const BridgeOperation &operation = s_bridge_snapshot.operation;
    const bool recording_saved_for_retry =
        upload_pending &&
        operation.kind == BridgeOperationKind::UploadRecording &&
        operation.state == BridgeOperationState::Failed &&
        (!s_bridge_snapshot.connectivity.wifi_connected ||
         !s_bridge_snapshot.bridge_online ||
         operation.http_status <= 0 ||
         operation.http_status >= 500);
    const char *status = "Waiting for Bridge ACK";
    if (recording_saved_for_retry) {
        status = "Saved on device · will retry";
    } else if (!s_bridge_snapshot.connectivity.wifi_connected) {
        status = "Wi-Fi interrupted";
    } else if (!s_bridge_snapshot.bridge_online) {
        status = "Bridge interrupted";
    } else if (s_pending_operation_seen &&
               s_bridge_snapshot.operation.state ==
                   BridgeOperationState::Running) {
        status = "Codex is processing";
    } else if (s_pending_operation_seen &&
               s_bridge_snapshot.operation.state ==
                   BridgeOperationState::Queued) {
        status = "Queued securely";
    }
    make_centered_label(
        s_root, status, &lv_font_montserrat_16, kMuted, 245, 250);
    make_centered_label(
        s_root,
        recording_saved_for_retry
            ? "Safe to reconnect later"
            : "Do not repeat the action",
        &lv_font_montserrat_14,
        kMuted,
        280,
        230);
}

void render_quota()
{
    render_header("USAGE");

    const BridgeQuotaWindow &quota =
        s_quota_index == 0
            ? s_bridge_snapshot.quota_5h
            : s_bridge_snapshot.quota_7d;
    const int value = quota.valid
        ? clamp_int(quota.used_percent, 0, 100)
        : 0;

    lv_obj_t *arc = lv_arc_create(s_root);
    lv_obj_set_pos(arc, 100, 72);
    lv_obj_set_size(arc, 160, 160);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, value);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, kBorder, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, kCyan, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    make_centered_label(s_root, "USED", &lv_font_montserrat_14, kMuted, 99, 90);

    char value_text[8] = "--";
    if (quota.valid) {
        snprintf(value_text, sizeof(value_text), "%d", value);
    }
    lv_obj_t *number =
        make_label(s_root, value_text, &lv_font_montserrat_48, kText);
    lv_obj_set_width(number, 98);
    lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(number, 105, 119);

    if (quota.valid) {
        lv_obj_t *percent =
            make_label(s_root, "%", &lv_font_montserrat_24, kCyan);
        lv_obj_set_pos(percent, 214, 127);
    }

    make_centered_label(
        s_root,
        quota.valid
            ? (quota.reset_label[0] == '\0'
                   ? "RESET TIME UNKNOWN"
                   : quota.reset_label)
            : "WAITING FOR USAGE",
        &lv_font_montserrat_14,
        kMuted,
        181,
        220);

    lv_obj_t *switcher =
        make_panel(s_root, 126, 210, 108, 28, kPanel, kBorder, 1, 14);
    lv_obj_t *five =
        make_label(switcher, "5H", &lv_font_montserrat_14,
                   s_quota_index == 0 ? kText : kMuted);
    lv_obj_set_pos(five, 20, 6);
    lv_obj_t *seven =
        make_label(switcher, "7D", &lv_font_montserrat_14,
                   s_quota_index == 1 ? kText : kMuted);
    lv_obj_set_pos(seven, 70, 6);
    make_dot(
        switcher,
        s_quota_index == 0 ? 8 : 58,
        11,
        5,
        kCyan);

    lv_obj_t *remaining =
        make_panel(s_root, 60, 250, 116, 52, kPanel, kBorder, 1, 13);
    char remaining_text[12] = "--";
    if (quota.valid) {
        snprintf(
            remaining_text,
            sizeof(remaining_text),
            "%u%%",
            static_cast<unsigned>(
                clamp_int(quota.remaining_percent, 0, 100)));
    }
    make_centered_label(
        remaining,
        remaining_text,
        &lv_font_montserrat_20,
        kText,
        7,
        90);
    make_centered_label(remaining, "LEFT", kCjk, kMuted, 30, 90);

    lv_obj_t *sessions =
        make_panel(s_root, 184, 250, 116, 52, kPanel, kBorder, 1, 13);
    char session_count[12];
    snprintf(
        session_count,
        sizeof(session_count),
        "%u",
        static_cast<unsigned>(
            s_bridge_snapshot.total_session_count));
    make_centered_label(
        sessions, session_count, &lv_font_montserrat_20, kText, 7, 90);
    make_centered_label(sessions, "SESSIONS", kCjk, kMuted, 30, 100);

    lv_obj_t *next =
        make_label(s_root, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, kCyan);
    lv_obj_set_pos(next, 328, 169);

    if (s_bridge_snapshot.pending_approval_count > 0) {
        char pending_text[24];
        snprintf(
            pending_text,
            sizeof(pending_text),
            "%u PENDING",
            static_cast<unsigned>(
                s_bridge_snapshot.pending_approval_count));
        make_centered_label(
            s_root,
            pending_text,
            &lv_font_montserrat_14,
            kAmber,
            320,
            130);
    }
}

void render_session_row(
    int list_index,
    int y,
    bool selected)
{
    const bool is_new = list_index == 0;
    SessionRecord *session = session_for_list_index(list_index);
    const lv_color_t accent =
        is_new ? kPurple : status_color(session->status);

    lv_obj_t *row = make_panel(
        s_root,
        selected ? 42 : 48,
        y,
        selected ? 276 : 264,
        60,
        selected ? kPanelRaised : kPanel,
        selected ? kCyan : kBorder,
        selected ? 2 : 1,
        14);

    make_dot(row, 12, 25, 8, selected ? accent : kMuted);

    lv_obj_t *title = make_label(
        row,
        is_new ? "NEW SESSION" : session->title,
        is_new ? kCjk : &lv_font_montserrat_16,
        selected ? kText : kMuted);
    lv_obj_set_pos(title, selected ? 32 : 28, 8);
    lv_obj_set_width(title, 155);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    lv_obj_t *subtitle = make_label(
        row,
        is_new ? "Create a Codex task by voice" : session->summary,
        is_new ? kCjk : &lv_font_montserrat_14,
        kMuted);
    lv_obj_set_pos(subtitle, selected ? 32 : 28, 34);
    lv_obj_set_width(subtitle, 155);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);

    lv_obj_t *pill = make_panel(
        row, selected ? 200 : 190, 18, 64, 24, kBg, accent, 1, 12);
    make_centered_label(
        pill,
        is_new ? "VOICE" : status_text(session->status),
        kCjk,
        accent,
        3,
        58);

    if (selected && s_dwell.action != DwellAction::None) {
        s_dwell_bar =
            make_progress_bar(row, 12, 56, selected ? 252 : 240, 3, accent);
    }
}

void render_sessions()
{
    render_header("SESSIONS");

    const int count = list_item_count();
    int first = s_session_index - 1;
    first = clamp_int(first, 0, count > 3 ? count - 3 : 0);

    for (int visible = 0; visible < 3; ++visible) {
        const int index = first + visible;
        if (index >= count) {
            break;
        }
        render_session_row(
            index,
            74 + visible * 67,
            index == s_session_index);
    }

    char index_text[20];
    snprintf(
        index_text,
        sizeof(index_text),
        "%02d / %02d",
        s_session_index + 1,
        count);
    make_centered_label(
        s_root, index_text, &lv_font_montserrat_14, kMuted, 310, 90);
}

void render_detail()
{
    render_header("SESSION DETAIL");
    if (s_selected_session == nullptr) {
        return;
    }

    const lv_color_t accent = status_color(s_selected_session->status);

    lv_obj_t *title = make_centered_label(
        s_root,
        s_selected_session->title,
        &lv_font_montserrat_24,
        kText,
        79,
        252);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    lv_obj_t *state = make_panel(
        s_root, 111, 119, 138, 28, kPanel, accent, 1, 14);
    make_dot(state, 12, 10, 8, accent);
    make_centered_label(
        state,
        status_text(s_selected_session->status),
        kCjk,
        accent,
        3,
        104);

    lv_obj_t *summary =
        make_panel(s_root, 52, 165, 256, 78, kPanel, kBorder, 1, 15);
    lv_obj_t *summary_text = make_label(
        summary,
        s_selected_session->summary,
        &lv_font_montserrat_16,
        kText);
    lv_obj_set_pos(summary_text, 16, 12);
    lv_obj_set_width(summary_text, 224);
    lv_label_set_long_mode(summary_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(summary_text, 4, LV_PART_MAIN);

    make_dot(summary, 16, 59, 6, kCyan);
    lv_obj_t *bridge =
        make_label(
            summary,
            s_bridge_snapshot.bridge_online
                ? "BRIDGE ONLINE"
                : "BRIDGE OFFLINE",
            &lv_font_montserrat_14,
            s_bridge_snapshot.bridge_online ? kMuted : kAmber);
    lv_obj_set_pos(bridge, 30, 54);

    if (s_selected_session->pending_approval ||
        s_selected_session->needs_feedback) {
        const bool approval_action =
            s_selected_session->pending_approval;
        const DwellAction action_kind =
            approval_action
                ? DwellAction::OpenApproval
                : DwellAction::OpenRejectVoice;
        const bool acquiring = dwell_active(action_kind);
        const lv_color_t action_color =
            approval_action ? kAmber : kPurple;
        lv_obj_t *action = make_panel(
            s_root,
            88,
            260,
            184,
            52,
            acquiring ? kPanelRaised : kPanel,
            acquiring ? kCyan : action_color,
            acquiring ? 2 : 1,
            16);
        make_centered_label(
            action,
            approval_action ? "REVIEW" : "ADD REASON",
            kCjk,
            acquiring ? kText : action_color,
            8,
            154);
        lv_obj_t *chevron = make_label(
            action, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, action_color);
        lv_obj_set_pos(chevron, 154, 16);
        if (acquiring) {
            s_dwell_bar = make_progress_bar(
                action, 14, 47, 156, 3, action_color);
        }
    } else {
        const char *footer =
            s_selected_session->status == SessionStatus::Complete
                ? "READY IN MAC"
                : "LIVE SESSION";
        make_centered_label(
            s_root, footer, &lv_font_montserrat_14, kMuted, 278, 160);
    }

    char id_text[32];
    snprintf(
        id_text,
        sizeof(id_text),
        "THREAD %.12s",
        s_selected_session->thread_id[0] == '\0'
            ? "PENDING"
            : s_selected_session->thread_id);
    make_centered_label(
        s_root, id_text, &lv_font_montserrat_14, kMuted, 324, 150);
}

void render_voice_listening()
{
    render_header(
        s_voice_purpose == VoicePurpose::NewSession
            ? "NEW SESSION"
            : "REJECT REASON");

    lv_obj_t *rail = lv_arc_create(s_root);
    lv_obj_set_pos(rail, 105, 75);
    lv_obj_set_size(rail, 150, 150);
    lv_arc_set_bg_angles(rail, 0, 360);
    lv_obj_set_style_arc_width(rail, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rail, kBorder, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(rail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rail, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(rail, kCyan, LV_PART_INDICATOR);
    lv_arc_set_range(rail, 0, 100);
    lv_arc_set_value(rail, 24);
    lv_obj_remove_style(rail, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *disc =
        make_panel(s_root, 138, 108, 84, 84, kPanelRaised, kCyan, 1, 42);
    lv_obj_t *mic =
        make_label(disc, "MIC", &lv_font_montserrat_20, kText);
    lv_obj_center(mic);

    for (int i = 0; i < 5; ++i) {
        s_voice_bars[i] =
            make_panel(s_root, 142 + i * 17, 246, 7, 10, kCyan, kCyan, 0, 4);
    }

    s_voice_status = make_centered_label(
        s_root, "LISTENING", kCjk, kText, 211, 200);
    s_voice_silence_bar =
        make_progress_bar(s_root, 100, 282, 160, 4, kCyan);
    make_centered_label(
        s_root, "SILENCE TO SEND", &lv_font_montserrat_14, kMuted, 296, 180);
}

void render_voice_finalizing()
{
    render_header(
        s_voice_purpose == VoicePurpose::NewSession
            ? "CREATE SESSION"
            : "SEND REASON");

    lv_obj_t *spinner = lv_spinner_create(s_root, 900, 84);
    lv_obj_set_pos(spinner, 111, 84);
    lv_obj_set_size(spinner, 138, 138);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, kBorder, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, kCyan, LV_PART_INDICATOR);
    lv_obj_remove_style(spinner, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

    make_centered_label(s_root, "AUDIO READY", &lv_font_montserrat_20, kText, 134, 180);
    make_centered_label(
        s_root, "Silence detected. Sending.", kCjk, kMuted, 246, 250);
    make_centered_label(
        s_root, "FINALIZING WAV", &lv_font_montserrat_14, kMuted, 284, 220);
}

void render_voice()
{
    if (s_voice_stage == VoiceStage::Listening) {
        render_voice_listening();
    } else {
        render_voice_finalizing();
    }
}

void render_approval()
{
    render_header("APPROVAL");

    const BridgeApproval *approval = active_approval();
    const char *title =
        approval != nullptr && approval->title[0] != '\0'
            ? approval->title
            : "CODEX REQUEST";
    const char *description =
        approval != nullptr && approval->detail[0] != '\0'
            ? approval->detail
            : "Request is no longer available";

    lv_obj_t *title_label = make_centered_label(
        s_root, title, &lv_font_montserrat_20, kText, 76, 250);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);

    lv_obj_t *detail =
        make_panel(s_root, 56, 111, 248, 82, kPanel, kBorder, 1, 15);
    lv_obj_t *detail_label = make_label(
        detail, description, &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(detail_label, 15, 13);
    lv_obj_set_width(detail_label, 218);
    lv_obj_set_height(detail_label, 58);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(
        detail_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(
        detail_label, 3, LV_PART_MAIN);

    const bool reject = dwell_active(DwellAction::SubmitReject);
    const bool allow = dwell_active(DwellAction::SubmitAllow);

    lv_obj_t *rail = make_panel(
        s_root, 78, 223, 204, 3, kBorder, kBorder, 0, 2);
    (void)rail;
    make_dot(s_root, 73, 216, 16, reject ? kRed : kBorder);
    make_dot(s_root, 172, 216, 16, (!reject && !allow) ? kAmber : kBorder);
    make_dot(s_root, 271, 216, 16, allow ? kGreen : kBorder);

    lv_obj_t *deny =
        make_label(s_root, "REJECT", kCjk, reject ? kRed : kMuted);
    lv_obj_set_pos(deny, 51, 247);
    lv_obj_t *approve =
        make_label(s_root, "ALLOW", kCjk, allow ? kGreen : kMuted);
    lv_obj_set_pos(approve, 267, 247);

    const char *decision = approval == nullptr
        ? "REQUEST EXPIRED"
        : (reject
               ? "CONFIRM REJECT"
               : (allow ? "CONFIRM ALLOW" : "SELECT"));
    const lv_color_t decision_color =
        approval == nullptr
            ? kMuted
            : (reject ? kRed : (allow ? kGreen : kAmber));
    make_centered_label(
        s_root, decision, kCjk, decision_color, 272, 160);

    if (approval != nullptr && (reject || allow)) {
        s_dwell_bar =
            make_progress_bar(s_root, 104, 306, 152, 5, decision_color);
    }

    char request_text[32] = "REQUEST";
    if (approval != nullptr && approval->approval_id[0] != '\0') {
        snprintf(
            request_text,
            sizeof(request_text),
            "REQUEST %.10s",
            approval->approval_id);
    }
    make_centered_label(
        s_root, request_text, &lv_font_montserrat_14, kMuted, 327, 190);
}

void render_result()
{
    make_centered_label(
        s_root, "VELA / CODEX", &lv_font_montserrat_14, kMuted, 23, 180);

    lv_obj_t *disc =
        make_panel(s_root, 128, 83, 104, 104, kPanel, s_result_color, 2, 52);
    lv_obj_t *mark =
        make_label(disc, LV_SYMBOL_OK, &lv_font_montserrat_32, s_result_color);
    lv_obj_center(mark);

    make_centered_label(
        s_root, s_result_title, &lv_font_montserrat_24, kText, 213, 240);
    make_centered_label(
        s_root, s_result_subtitle, kCjk, kMuted, 251, 250);
}

void render_error()
{
    make_centered_label(
        s_root, "VELA / CODEX", &lv_font_montserrat_14, kMuted, 23, 180);

    lv_obj_t *disc =
        make_panel(s_root, 132, 86, 96, 96, kPanel, kRed, 2, 48);
    lv_obj_t *mark =
        make_label(disc, "!", &lv_font_montserrat_32, kRed);
    lv_obj_center(mark);

    make_centered_label(
        s_root, s_error_title, &lv_font_montserrat_24, kText, 210, 260);
    make_centered_label(
        s_root, s_error_subtitle, kCjk, kMuted, 250, 250);
    make_centered_label(
        s_root, "LEFT TWICE TO RETURN", &lv_font_montserrat_14, kMuted, 298, 220);
}

void render_surface()
{
    if (s_root == nullptr) {
        return;
    }

    lv_obj_clean(s_root);
    s_dwell_bar = nullptr;
    s_voice_status = nullptr;
    s_voice_silence_bar = nullptr;
    for (lv_obj_t *&bar : s_voice_bars) {
        bar = nullptr;
    }

    switch (s_surface) {
        case Surface::Provisioning:
            render_provisioning();
            break;
        case Surface::Connecting:
            render_connecting();
            break;
        case Surface::BridgeOffline:
            render_bridge_offline();
            break;
        case Surface::Quota:
            render_quota();
            break;
        case Surface::Sessions:
            render_sessions();
            break;
        case Surface::Detail:
            render_detail();
            break;
        case Surface::Voice:
            render_voice();
            break;
        case Surface::Approval:
            render_approval();
            break;
        case Surface::Sending:
            render_sending();
            break;
        case Surface::Result:
            render_result();
            break;
        case Surface::Error:
            render_error();
            break;
    }

    render_return_hint();
    apply_led_state();
}

void show_result(
    const char *title,
    const char *subtitle,
    lv_color_t color,
    Surface next)
{
    cancel_dwell();
    reset_back_hint();
    snprintf(s_result_title, sizeof(s_result_title), "%s", title);
    snprintf(s_result_subtitle, sizeof(s_result_subtitle), "%s", subtitle);
    s_result_color = color;
    s_result_next = next;
    s_result_until_ms = millis() + kResultMs;
    s_surface = Surface::Result;
    board_play_haptic(1);
    render_surface();
}

void show_error(
    const char *title,
    const char *subtitle,
    Surface next)
{
    cancel_dwell();
    reset_back_hint();
    snprintf(s_error_title, sizeof(s_error_title), "%s", title);
    snprintf(s_error_subtitle, sizeof(s_error_subtitle), "%s", subtitle);
    s_error_next = next;
    s_surface = Surface::Error;
    board_play_haptic(2);
    render_surface();
}

void open_quota()
{
    cancel_dwell();
    reset_back_hint();
    s_surface = Surface::Quota;
    render_surface();
}

void open_sessions(bool select_new, bool arm_selection)
{
    cancel_dwell();
    reset_back_hint();
    s_surface = Surface::Sessions;
    if (select_new) {
        s_session_index = 0;
    } else {
        s_session_index = clamp_int(
            s_session_index, 0, list_item_count() - 1);
    }
    if (arm_selection) {
        SessionRecord *session =
            session_for_list_index(s_session_index);
        start_dwell(
            s_session_index == 0
                ? DwellAction::NewSession
                : DwellAction::OpenSession,
            session == nullptr ? nullptr : session->thread_id);
    }
    render_surface();
}

void enter_detail(SessionRecord *session)
{
    if (session == nullptr) {
        return;
    }
    cancel_dwell();
    reset_back_hint();
    s_selected_session = session;
    copy_text(
        s_selected_thread_id,
        sizeof(s_selected_thread_id),
        session->thread_id);
    s_surface = Surface::Detail;
    render_surface();
}

void enter_voice(VoicePurpose purpose)
{
    cancel_dwell();
    reset_back_hint();
    s_voice_purpose = purpose;
    s_voice_stage = VoiceStage::Listening;
    s_voice_started_ms = millis();
    s_last_speech_ms = s_voice_started_ms;
    s_speech_seen = false;
    s_recording_started = false;
    s_last_audio_frame = 0;
    s_last_audio_frame_ms = s_voice_started_ms;
    s_recording_requested = board_recording_start();
    if (!s_recording_requested) {
        const RecordingStatus recording = board_get_recording_status();
        Serial.printf(
            "[VOICE] Recording start rejected: %s\n",
            recording.error[0] != '\0'
                ? recording.error
                : "unknown error");
        show_error(
            "RECORD ERROR",
            "Recording unavailable. Not sent.",
            purpose == VoicePurpose::NewSession
                ? Surface::Sessions
                : Surface::Detail);
        return;
    }
    s_surface = Surface::Voice;
    board_play_haptic(0);
    render_surface();
}

void enter_approval()
{
    const BridgeApproval *approval = active_approval();
    if (approval == nullptr) {
        show_error(
            "REQUEST EXPIRED",
            "Refresh sessions before retrying.",
            Surface::Sessions);
        return;
    }
    cancel_dwell();
    reset_back_hint();
    if (approval->thread_id[0] != '\0') {
        copy_text(
            s_selected_thread_id,
            sizeof(s_selected_thread_id),
            approval->thread_id);
        s_selected_session = find_session(s_selected_thread_id);
    }
    s_surface = Surface::Approval;
    board_play_haptic(1);
    render_surface();
}

void arm_or_take_back(Surface destination)
{
    const uint32_t now = millis();
    if (s_back_armed &&
        static_cast<int32_t>(s_back_until_ms - now) > 0) {
        cancel_dwell();
        reset_back_hint();
        if (destination == Surface::Quota) {
            open_quota();
        } else if (destination == Surface::Sessions) {
            open_sessions(false, false);
        } else if (destination == Surface::Detail) {
            SessionRecord *session =
                find_session(s_selected_thread_id);
            if (session != nullptr) {
                enter_detail(session);
            } else {
                open_sessions(false, false);
            }
        } else if (destination == Surface::Approval) {
            enter_approval();
        }
        return;
    }

    cancel_dwell();
    s_back_armed = true;
    s_back_until_ms = now + kBackConfirmMs;
    render_surface();
}

void cancel_voice_and_return()
{
    const RecordingStatus recording = board_get_recording_status();
    if (recording.starting || recording.recording) {
        board_recording_stop();
    }
    if (s_voice_purpose == VoicePurpose::NewSession) {
        open_sessions(true, false);
    } else {
        SessionRecord *session = find_session(s_selected_thread_id);
        if (session != nullptr) {
            enter_detail(session);
        } else {
            open_sessions(false, false);
        }
    }
}

void handle_quota_detent(int direction)
{
    if (direction > 0) {
        if (s_quota_index == 0) {
            s_quota_index = 1;
            render_surface();
        } else {
            open_sessions(true, true);
        }
    } else if (s_quota_index == 1) {
        s_quota_index = 0;
        render_surface();
    }
}

void handle_sessions_detent(int direction)
{
    if (direction < 0 && s_session_index == 0) {
        arm_or_take_back(Surface::Quota);
        return;
    }

    const int next = clamp_int(
        s_session_index + direction,
        0,
        list_item_count() - 1);
    if (next == s_session_index) {
        if (s_dwell.action != DwellAction::None) {
            SessionRecord *session =
                session_for_list_index(s_session_index);
            start_dwell(
                s_session_index == 0
                    ? DwellAction::NewSession
                    : DwellAction::OpenSession,
                session == nullptr ? nullptr : session->thread_id);
            render_surface();
        }
        return;
    }

    reset_back_hint();
    s_session_index = next;
    SessionRecord *session =
        session_for_list_index(s_session_index);
    start_dwell(
        s_session_index == 0
            ? DwellAction::NewSession
            : DwellAction::OpenSession,
        session == nullptr ? nullptr : session->thread_id);
    render_surface();
}

void handle_detail_detent(int direction)
{
    if (direction < 0) {
        arm_or_take_back(Surface::Sessions);
        return;
    }

    reset_back_hint();
    if (s_selected_session != nullptr &&
        s_selected_session->pending_approval) {
        start_dwell(
            DwellAction::OpenApproval,
            s_selected_session->thread_id);
        render_surface();
    } else if (
        s_selected_session != nullptr &&
        s_selected_session->needs_feedback) {
        start_dwell(
            DwellAction::OpenRejectVoice,
            s_selected_session->thread_id);
        render_surface();
    }
}

void handle_approval_detent(int direction)
{
    if (active_approval() == nullptr) {
        show_error(
            "REQUEST EXPIRED",
            "Refresh sessions before retrying.",
            Surface::Sessions);
        return;
    }
    reset_back_hint();
    start_dwell(
        direction > 0
            ? DwellAction::SubmitAllow
            : DwellAction::SubmitReject);
    render_surface();
}

void handle_detent(int direction)
{
    switch (s_surface) {
        case Surface::Provisioning:
        case Surface::Connecting:
        case Surface::BridgeOffline:
        case Surface::Sending:
            break;
        case Surface::Quota:
            handle_quota_detent(direction);
            break;
        case Surface::Sessions:
            handle_sessions_detent(direction);
            break;
        case Surface::Detail:
            handle_detail_detent(direction);
            break;
        case Surface::Voice:
            if (direction < 0) {
                const uint32_t now = millis();
                if (s_back_armed &&
                    static_cast<int32_t>(s_back_until_ms - now) > 0) {
                    reset_back_hint();
                    cancel_voice_and_return();
                } else {
                    cancel_dwell();
                    s_back_armed = true;
                    s_back_until_ms = now + kBackConfirmMs;
                    render_surface();
                }
            } else if (s_back_armed) {
                reset_back_hint();
                render_surface();
            }
            break;
        case Surface::Approval:
            handle_approval_detent(direction);
            break;
        case Surface::Error:
            if (direction < 0) {
                arm_or_take_back(s_error_next);
            }
            break;
        case Surface::Result:
            break;
    }
}

void handle_knob(int32_t delta)
{
    if (delta == 0) {
        return;
    }
    delta = clamp_int(static_cast<int>(delta), -8, 8);
    const int direction = delta > 0 ? 1 : -1;
    const int steps = abs(static_cast<int>(delta));
    for (int step = 0; step < steps; ++step) {
        const Surface before = s_surface;
        handle_detent(direction);
        if (s_surface != before) {
            break;
        }
    }
}

void begin_pending_operation(
    PendingMutation mutation,
    BridgeOperationKind kind)
{
    s_pending_mutation = mutation;
    s_pending_operation_kind = kind;
    s_pending_operation_baseline_ms =
        s_bridge_snapshot.operation.updated_ms;
    copy_text(
        s_pending_operation_baseline_id,
        sizeof(s_pending_operation_baseline_id),
        s_bridge_snapshot.operation.operation_id);
    s_pending_operation_id[0] = '\0';
    s_pending_operation_seen = false;
    if (s_bridge_snapshot.operation.kind == kind &&
        (s_bridge_snapshot.operation.state ==
             BridgeOperationState::Queued ||
         s_bridge_snapshot.operation.state ==
             BridgeOperationState::Running)) {
        s_pending_operation_seen = true;
        copy_text(
            s_pending_operation_id,
            sizeof(s_pending_operation_id),
            s_bridge_snapshot.operation.operation_id);
    }
    cancel_dwell();
    reset_back_hint();
    s_surface = Surface::Sending;
    render_surface();
}

bool queue_recording_upload(
    const RecordingStatus &recording,
    VoicePurpose purpose)
{
    const bool is_new = purpose == VoicePurpose::NewSession;
    const bool queued = bridge_client_upload_recording(
        recording.path,
        is_new
            ? BridgeAudioPurpose::NewSession
            : BridgeAudioPurpose::RejectReason,
        is_new ? nullptr : s_reject_thread_id,
        is_new ? nullptr : s_reject_approval_id);
    if (!queued) {
        show_error(
            "SEND BUSY",
            "Bridge did not queue the recording.",
            is_new ? Surface::Sessions : Surface::Detail);
        return false;
    }

    begin_pending_operation(
        is_new
            ? PendingMutation::UploadNewSession
            : PendingMutation::UploadRejectReason,
        BridgeOperationKind::UploadRecording);
    return true;
}

bool queue_approval_resolution(BridgeApprovalDecision decision)
{
    const BridgeApproval *approval = active_approval();
    if (approval == nullptr) {
        show_error(
            "REQUEST EXPIRED",
            "Refresh sessions before retrying.",
            Surface::Sessions);
        return false;
    }
    if (!s_bridge_snapshot.bridge_online) {
        show_error(
            "BRIDGE OFFLINE",
            "Decision was not sent.",
            Surface::Approval);
        return false;
    }

    BridgeApproval captured = *approval;
    if (decision == BridgeApprovalDecision::Reject) {
        copy_text(
            s_reject_thread_id,
            sizeof(s_reject_thread_id),
            captured.thread_id);
        copy_text(
            s_reject_approval_id,
            sizeof(s_reject_approval_id),
            captured.approval_id);
    }
    copy_text(
        s_pending_approval_id,
        sizeof(s_pending_approval_id),
        captured.approval_id);

    const bool queued = bridge_client_resolve_approval(
        captured.thread_id,
        captured.approval_id,
        captured.nonce,
        captured.action_digest,
        decision);
    if (!queued) {
        show_error(
            "SEND BUSY",
            "Bridge did not queue the decision.",
            Surface::Approval);
        return false;
    }

    copy_text(
        s_selected_thread_id,
        sizeof(s_selected_thread_id),
        captured.thread_id);
    begin_pending_operation(
        decision == BridgeApprovalDecision::Allow
            ? PendingMutation::ResolveAllow
            : PendingMutation::ResolveReject,
        BridgeOperationKind::ResolveApproval);
    return true;
}

void begin_voice_finalize()
{
    if (s_voice_stage != VoiceStage::Listening) {
        return;
    }

    const RecordingStatus recording = board_get_recording_status();
    if (recording.starting || recording.recording) {
        board_recording_stop();
    }
    s_voice_stage = VoiceStage::Finalizing;
    s_voice_finalize_started_ms = millis();
    render_surface();
}

void update_voice_visual(const AudioSnapshot &audio, uint32_t now)
{
    if (s_voice_stage != VoiceStage::Listening) {
        return;
    }

    const float level = audio.valid
        ? clamp_float((audio.rms_db + 58.0f) / 30.0f, 0.04f, 1.0f)
        : 0.04f;
    const float weights[] = {0.55f, 0.82f, 1.0f, 0.78f, 0.5f};

    for (int i = 0; i < 5; ++i) {
        if (s_voice_bars[i] == nullptr) {
            continue;
        }
        const int height =
            6 + static_cast<int>(32.0f * level * weights[i]);
        lv_obj_set_pos(s_voice_bars[i], 142 + i * 17, 260 - height);
        lv_obj_set_size(s_voice_bars[i], 7, height);
    }

    if (s_voice_status != nullptr) {
        lv_label_set_text(
            s_voice_status,
            s_speech_seen ? "SILENCE TO SEND" : "LISTENING");
    }

    if (s_voice_silence_bar != nullptr) {
        uint32_t silence_ms = 0;
        if (s_speech_seen && now >= s_last_speech_ms) {
            silence_ms = now - s_last_speech_ms;
        }
        const int value = clamp_int(
            static_cast<int>(
                (silence_ms * 1000UL) / kVoiceSilenceMs),
            0,
            1000);
        lv_bar_set_value(s_voice_silence_bar, value, LV_ANIM_OFF);
    }
}

void process_voice(uint32_t now)
{
    if (s_surface != Surface::Voice) {
        return;
    }

    if (s_voice_stage == VoiceStage::Listening) {
        const RecordingStatus recording = board_get_recording_status();
        if (recording.recording) {
            s_recording_started = true;
        }

        if (recording.error[0] != '\0' &&
            !recording.starting &&
            !recording.recording) {
            Serial.printf("[VOICE] Recording failed: %s\n", recording.error);
            show_error(
                "RECORD ERROR",
                "Recording failed. Not sent.",
                s_voice_purpose == VoicePurpose::NewSession
                    ? Surface::Sessions
                    : Surface::Detail);
            return;
        }

        const AudioSnapshot audio = board_get_audio_snapshot();
        const uint32_t elapsed = now - s_voice_started_ms;
        const bool fresh_frame =
            audio.valid && audio.frame_count != s_last_audio_frame;
        if (fresh_frame) {
            s_last_audio_frame = audio.frame_count;
            s_last_audio_frame_ms = now;
        }

        if (!s_recording_started &&
            elapsed >= kRecordingStartTimeoutMs) {
            show_error(
                "RECORD TIMEOUT",
                "Recorder did not start. Not sent.",
                s_voice_purpose == VoicePurpose::NewSession
                    ? Surface::Sessions
                    : Surface::Detail);
            return;
        }

        if (elapsed >= kVoiceStartGraceMs &&
            now - s_last_audio_frame_ms >= kAudioStaleMs) {
            if (recording.starting || recording.recording) {
                board_recording_stop();
            }
            show_error(
                "MIC STALLED",
                "Microphone stream stopped. Not sent.",
                s_voice_purpose == VoicePurpose::NewSession
                    ? Surface::Sessions
                    : Surface::Detail);
            return;
        }

        const float active_threshold =
            s_speech_seen
                ? kSpeechContinueThresholdDb
                : kSpeechThresholdDb;
        const bool speech =
            elapsed >= kVoiceStartGraceMs &&
            fresh_frame &&
            audio.rms_db >= active_threshold;

        if (speech) {
            s_speech_seen = true;
            s_last_speech_ms = now;
        }

        if (s_speech_seen &&
            now - s_last_speech_ms >= kVoiceSilenceMs) {
            begin_voice_finalize();
            return;
        }

        if (!s_speech_seen && elapsed >= kVoiceNoSpeechMs) {
            const RecordingStatus recording = board_get_recording_status();
            if (recording.starting || recording.recording) {
                board_recording_stop();
            }
            show_error(
                "NO SPEECH",
                "Nothing heard. Not sent.",
                s_voice_purpose == VoicePurpose::NewSession
                    ? Surface::Sessions
                    : Surface::Detail);
            return;
        }

        if (s_speech_seen && elapsed >= kVoiceMaxMs) {
            begin_voice_finalize();
        }
        return;
    }

    const RecordingStatus recording = board_get_recording_status();
    const bool recording_finished =
        !recording.starting && !recording.recording && !recording.stopping;
    if (!recording_finished) {
        if (now - s_voice_finalize_started_ms >= kVoiceFinalizeMaxMs) {
            show_error(
                "SAVE TIMEOUT",
                "Recording is still saving. Not sent.",
                s_voice_purpose == VoicePurpose::NewSession
                    ? Surface::Sessions
                    : Surface::Detail);
        }
        return;
    }

    const bool recording_valid =
        s_recording_requested &&
        s_recording_started &&
        recording.error[0] == '\0' &&
        recording.path[0] != '\0' &&
        recording.data_bytes > 0;
    if (!recording_valid) {
        Serial.printf(
            "[VOICE] Invalid recording: path=%s bytes=%lu error=%s\n",
            recording.path,
            static_cast<unsigned long>(recording.data_bytes),
            recording.error);
        show_error(
            "SAVE FAILED",
            "Invalid recording. Not sent.",
            s_voice_purpose == VoicePurpose::NewSession
                ? Surface::Sessions
                : Surface::Detail);
        return;
    }

    (void)queue_recording_upload(recording, s_voice_purpose);
}

void process_dwell(uint32_t now)
{
    if (s_dwell.action == DwellAction::None ||
        s_dwell.generation != s_dwell_generation) {
        return;
    }

    const uint32_t elapsed = now - s_dwell.started_ms;
    if (s_dwell_bar != nullptr) {
        const int value = clamp_int(
            static_cast<int>((elapsed * 1000UL) / kDwellMs),
            0,
            1000);
        lv_bar_set_value(s_dwell_bar, value, LV_ANIM_OFF);
    }
    if (elapsed < kDwellMs) {
        return;
    }

    const DwellAction completed = s_dwell.action;
    char target_thread_id[VELA_THREAD_ID_BYTES] = {};
    copy_text(
        target_thread_id,
        sizeof(target_thread_id),
        s_dwell.thread_id);
    cancel_dwell();

    switch (completed) {
        case DwellAction::NewSession:
            enter_voice(VoicePurpose::NewSession);
            break;
        case DwellAction::OpenSession: {
            SessionRecord *session = find_session(target_thread_id);
            if (session == nullptr) {
                show_error(
                    "SESSION MOVED",
                    "The session list changed. Select again.",
                    Surface::Sessions);
            } else {
                enter_detail(session);
            }
            break;
        }
        case DwellAction::OpenApproval: {
            SessionRecord *session = find_session(target_thread_id);
            if (session != nullptr) {
                s_selected_session = session;
                copy_text(
                    s_selected_thread_id,
                    sizeof(s_selected_thread_id),
                    session->thread_id);
            }
            enter_approval();
            break;
        }
        case DwellAction::OpenRejectVoice: {
            SessionRecord *session = find_session(target_thread_id);
            if (session == nullptr ||
                session->approval.approval_id[0] == '\0') {
                show_error(
                    "REASON EXPIRED",
                    "Refresh the session before recording.",
                    session == nullptr
                        ? Surface::Sessions
                        : Surface::Detail);
                break;
            }
            s_selected_session = session;
            copy_text(
                s_reject_thread_id,
                sizeof(s_reject_thread_id),
                session->thread_id);
            copy_text(
                s_selected_thread_id,
                sizeof(s_selected_thread_id),
                session->thread_id);
            copy_text(
                s_reject_approval_id,
                sizeof(s_reject_approval_id),
                session->approval.approval_id);
            enter_voice(VoicePurpose::RejectReason);
            break;
        }
        case DwellAction::SubmitAllow:
            (void)queue_approval_resolution(
                BridgeApprovalDecision::Allow);
            break;
        case DwellAction::SubmitReject:
            (void)queue_approval_resolution(
                BridgeApprovalDecision::Reject);
            break;
        case DwellAction::None:
            break;
    }
}

void clear_pending_operation()
{
    s_pending_mutation = PendingMutation::None;
    s_pending_operation_kind = BridgeOperationKind::None;
    s_pending_operation_baseline_ms = 0;
    s_pending_operation_seen = false;
    s_pending_operation_baseline_id[0] = '\0';
    s_pending_operation_id[0] = '\0';
}

void process_bridge_operation()
{
    if (s_pending_mutation == PendingMutation::None) {
        return;
    }

    const BridgeOperation &operation =
        s_bridge_snapshot.operation;
    if (operation.kind != s_pending_operation_kind) {
        return;
    }

    if (!s_pending_operation_seen) {
        const bool id_changed =
            operation.operation_id[0] != '\0' &&
            !same_text(
                operation.operation_id,
                s_pending_operation_baseline_id);
        const bool timestamp_changed =
            operation.updated_ms != s_pending_operation_baseline_ms;
        if (!id_changed && !timestamp_changed) {
            return;
        }
        s_pending_operation_seen = true;
        copy_text(
            s_pending_operation_id,
            sizeof(s_pending_operation_id),
            operation.operation_id);
        if (s_surface == Surface::Sending) {
            render_surface();
        }
    } else if (
        s_pending_operation_id[0] != '\0' &&
        operation.operation_id[0] != '\0' &&
        !same_text(
            operation.operation_id,
            s_pending_operation_id)) {
        return;
    }

    if (operation.state != BridgeOperationState::Accepted &&
        operation.state != BridgeOperationState::Failed) {
        return;
    }

    const PendingMutation completed = s_pending_mutation;
    char result_thread_id[VELA_THREAD_ID_BYTES] = {};
    char operation_message[VELA_ERROR_TEXT_BYTES] = {};
    copy_text(
        result_thread_id,
        sizeof(result_thread_id),
        operation.result_thread_id);
    copy_text(
        operation_message,
        sizeof(operation_message),
        operation.message);
    const bool accepted =
        operation.state == BridgeOperationState::Accepted;
    const bool upload_mutation =
        completed == PendingMutation::UploadNewSession ||
        completed == PendingMutation::UploadRejectReason;
    const bool retryable_upload_failure =
        !accepted &&
        upload_mutation &&
        (!s_bridge_snapshot.connectivity.wifi_connected ||
         !s_bridge_snapshot.bridge_online ||
         operation.http_status <= 0 ||
         operation.http_status >= 500);

    if (retryable_upload_failure) {
        // The Bridge client has already staged the WAV and owns retrying it.
        // Move this failed attempt into the baseline so the same snapshot is
        // not handled again; a later retry will publish a newer operation.
        s_pending_operation_seen = false;
        s_pending_operation_baseline_ms = operation.updated_ms;
        copy_text(
            s_pending_operation_baseline_id,
            sizeof(s_pending_operation_baseline_id),
            operation.operation_id);
        s_pending_operation_id[0] = '\0';
        s_surface = Surface::Sending;
        render_surface();
        return;
    }

    if (accepted &&
        (completed == PendingMutation::ResolveAllow ||
         completed == PendingMutation::ResolveReject)) {
        copy_text(
            s_resolved_approval_id,
            sizeof(s_resolved_approval_id),
            s_pending_approval_id);
    }
    clear_pending_operation();

    if (!accepted) {
        const char *message = operation_message[0] == '\0'
            ? "Bridge rejected the operation."
            : operation_message;
        Surface next = Surface::Sessions;
        if (completed == PendingMutation::ResolveAllow ||
            completed == PendingMutation::ResolveReject) {
            next = Surface::Approval;
        } else if (
            completed == PendingMutation::UploadRejectReason) {
            next = Surface::Detail;
        }
        s_pending_approval_id[0] = '\0';
        show_error("NOT SENT", message, next);
        return;
    }

    rebuild_sessions_from_bridge();

    switch (completed) {
        case PendingMutation::UploadNewSession: {
            if (result_thread_id[0] != '\0') {
                copy_text(
                    s_selected_thread_id,
                    sizeof(s_selected_thread_id),
                    result_thread_id);
            }
            s_selected_session =
                find_session(s_selected_thread_id);
            (void)bridge_client_request_refresh();
            show_result(
                "SESSION CREATED",
                "Codex received your request",
                kGreen,
                s_selected_session == nullptr
                    ? Surface::Sessions
                    : Surface::Detail);
            break;
        }
        case PendingMutation::UploadRejectReason: {
            SessionRecord *session =
                find_session(s_reject_thread_id);
            if (session != nullptr) {
                session->needs_feedback = false;
                s_selected_session = session;
                copy_text(
                    s_selected_thread_id,
                    sizeof(s_selected_thread_id),
                    session->thread_id);
            }
            (void)bridge_client_request_refresh();
            show_result(
                "REASON SENT",
                "Codex received your feedback",
                kGreen,
                session == nullptr
                    ? Surface::Sessions
                    : Surface::Detail);
            s_reject_thread_id[0] = '\0';
            s_reject_approval_id[0] = '\0';
            break;
        }
        case PendingMutation::ResolveAllow: {
            SessionRecord *session =
                find_session(s_selected_thread_id);
            if (session != nullptr) {
                session->pending_approval = false;
                session->approval.present = false;
                session->status = SessionStatus::Running;
                s_selected_session = session;
            }
            (void)bridge_client_request_refresh();
            show_result(
                "APPROVED",
                "Codex is continuing",
                kGreen,
                session == nullptr
                    ? Surface::Sessions
                    : Surface::Detail);
            s_pending_approval_id[0] = '\0';
            break;
        }
        case PendingMutation::ResolveReject: {
            SessionRecord *session =
                find_session(s_reject_thread_id);
            if (session != nullptr) {
                session->pending_approval = false;
                session->approval.present = false;
                session->needs_feedback = true;
                session->status = SessionStatus::Running;
                s_selected_session = session;
                copy_text(
                    s_selected_thread_id,
                    sizeof(s_selected_thread_id),
                    session->thread_id);
            }
            s_pending_approval_id[0] = '\0';
            enter_voice(VoicePurpose::RejectReason);
            break;
        }
        case PendingMutation::None:
            break;
    }
}

void sync_bridge_snapshot(bool force_render)
{
    memset(
        &s_bridge_snapshot_scratch,
        0,
        sizeof(s_bridge_snapshot_scratch));
    BridgeSnapshot &latest = s_bridge_snapshot_scratch;
    if (!bridge_client_get_snapshot(&latest)) {
        return;
    }

    const bool data_changed =
        latest.generation != s_bridge_generation ||
        latest.remote_revision != s_remote_revision ||
        latest.connectivity.revision != s_connectivity_revision ||
        latest.bridge_online != s_bridge_snapshot.bridge_online ||
        latest.operation.updated_ms !=
            s_bridge_snapshot.operation.updated_ms ||
        latest.operation.state !=
            s_bridge_snapshot.operation.state ||
        latest.operation.kind !=
            s_bridge_snapshot.operation.kind;

    s_bridge_snapshot = latest;
    s_bridge_generation = latest.generation;
    s_remote_revision = latest.remote_revision;
    s_connectivity_revision = latest.connectivity.revision;

    if (!latest.current_approval.present ||
        !same_text(
            latest.current_approval.approval_id,
            s_resolved_approval_id)) {
        s_resolved_approval_id[0] = '\0';
    }
    if (!latest.current_approval.present) {
        s_presented_approval_id[0] = '\0';
    }

    if (data_changed || force_render) {
        rebuild_sessions_from_bridge();
    } else {
        s_selected_session =
            find_session(s_selected_thread_id);
    }

    bool surface_changed = false;
    const Surface required = required_network_surface();
    if (may_show_network_surface()) {
        if (required != Surface::Quota) {
            if (!is_network_surface(s_surface)) {
                s_network_return_surface = s_surface;
            }
            if (s_surface != required) {
                cancel_dwell();
                reset_back_hint();
                s_surface = required;
                surface_changed = true;
            }
        } else if (is_network_surface(s_surface)) {
            Surface destination = s_network_return_surface;
            if (destination == Surface::Detail &&
                s_selected_session == nullptr) {
                destination = Surface::Sessions;
            }
            if (destination == Surface::Approval &&
                active_approval() == nullptr) {
                destination = Surface::Sessions;
            }
            s_surface = destination;
            surface_changed = true;
        }
    }

    const bool has_new_global_approval =
        latest.current_approval.present &&
        !approval_is_resolved_locally(latest.current_approval) &&
        !same_text(
            latest.current_approval.approval_id,
            s_presented_approval_id);
    if (required == Surface::Quota &&
        has_new_global_approval &&
        may_show_network_surface()) {
        cancel_dwell();
        reset_back_hint();
        copy_text(
            s_selected_thread_id,
            sizeof(s_selected_thread_id),
            latest.current_approval.thread_id);
        s_selected_session = find_session(s_selected_thread_id);
        copy_text(
            s_presented_approval_id,
            sizeof(s_presented_approval_id),
            latest.current_approval.approval_id);
        s_surface = Surface::Approval;
        board_play_haptic(1);
        surface_changed = true;
    }

    if (s_surface == Surface::Detail &&
        s_selected_session == nullptr) {
        s_surface = Surface::Sessions;
        surface_changed = true;
    }
    if (s_surface == Surface::Approval &&
        active_approval() == nullptr &&
        s_pending_mutation == PendingMutation::None) {
        s_surface = Surface::Sessions;
        surface_changed = true;
    }

    if (force_render || surface_changed ||
        (data_changed &&
         s_surface != Surface::Voice &&
         s_surface != Surface::Result &&
         s_surface != Surface::Error)) {
        render_surface();
    }
}

void process_transient_states(uint32_t now)
{
    if (s_surface == Surface::Result &&
        static_cast<int32_t>(now - s_result_until_ms) >= 0) {
        if (s_result_next == Surface::Detail) {
            SessionRecord *session =
                find_session(s_selected_thread_id);
            if (session != nullptr) {
                enter_detail(session);
            } else {
                open_sessions(false, false);
            }
        } else if (s_result_next == Surface::Sessions) {
            open_sessions(false, false);
        } else {
            open_quota();
        }
    }

    if (s_back_armed &&
        static_cast<int32_t>(now - s_back_until_ms) >= 0) {
        reset_back_hint();
        render_surface();
    }
}

void root_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        s_root = nullptr;
        s_dwell_bar = nullptr;
        s_voice_status = nullptr;
        s_voice_silence_bar = nullptr;
        for (lv_obj_t *&bar : s_voice_bars) {
            bar = nullptr;
        }
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        board_mark_touch();
        return;
    }
    if (code != LV_EVENT_GESTURE) {
        return;
    }

    board_mark_touch();
    lv_indev_t *input = lv_indev_get_act();
    if (input == nullptr) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(input);

    if (s_surface == Surface::Quota && direction == LV_DIR_RIGHT) {
        open_sessions(true, true);
        return;
    }
    if (s_surface == Surface::Sessions && direction == LV_DIR_LEFT) {
        open_quota();
        return;
    }
    if (s_surface == Surface::Detail && direction == LV_DIR_LEFT) {
        open_sessions(false, false);
        return;
    }
    if (s_surface == Surface::Voice && direction == LV_DIR_LEFT) {
        cancel_voice_and_return();
    }
}

}  // namespace

void app_ui_begin()
{
    if (s_root != nullptr) {
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, kBg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_root = lv_obj_create(screen);
    lv_obj_set_size(s_root, kScreenSize, kScreenSize);
    lv_obj_center(s_root);
    lv_obj_set_style_bg_color(s_root, kBg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_root, kBorder, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_root, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_root, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, root_event, LV_EVENT_ALL, nullptr);

    s_surface = Surface::Quota;
    s_quota_index = 0;
    s_session_index = 0;
    s_selected_session = nullptr;
    s_selected_thread_id[0] = '\0';
    clear_pending_operation();
    s_last_ui_refresh_ms = millis();
    sync_bridge_snapshot(true);
    if (s_root != nullptr && lv_obj_get_child_cnt(s_root) == 0) {
        render_surface();
    }
}

void app_ui_poll()
{
    if (s_root == nullptr || !lv_obj_is_valid(s_root)) {
        s_root = nullptr;
        return;
    }

    const uint32_t now = millis();
    const bool ui_tick =
        now - s_last_ui_refresh_ms >= kUiRefreshMs;
    if (ui_tick) {
        s_last_ui_refresh_ms = now;
        sync_bridge_snapshot(false);
    }

    process_bridge_operation();
    handle_knob(knob_take_delta());
    process_dwell(now);
    process_voice(now);
    process_transient_states(now);

    if (ui_tick &&
        s_surface == Surface::Voice &&
        s_voice_stage == VoiceStage::Listening) {
        update_voice_visual(board_get_audio_snapshot(), now);
    }
}
