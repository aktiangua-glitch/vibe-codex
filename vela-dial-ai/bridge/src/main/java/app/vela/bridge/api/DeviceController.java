package app.vela.bridge.api;

import app.vela.bridge.audio.FeedbackRegistry;
import app.vela.bridge.audio.RecordingLedger;
import app.vela.bridge.audio.RecordingService;
import app.vela.bridge.codex.ApprovalRegistry;
import app.vela.bridge.codex.CodexAppServerClient;
import app.vela.bridge.codex.CodexStateStore;
import app.vela.bridge.config.BridgeProperties;
import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import jakarta.validation.constraints.NotBlank;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestPart;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.io.InputStream;
import java.time.Instant;
import java.util.List;

@RestController
@RequestMapping("/api/v1")
public class DeviceController {

    public record DeviceSnapshot(
            long revision,
            @JsonProperty("generated_at") Instant generatedAt,
            DeviceQuota quota,
            @JsonProperty("total_session_count") int totalSessionCount,
            List<DeviceSession> sessions,
            @JsonProperty("current_approval") DeviceApproval currentApproval,
            @JsonProperty("pending_approval_count") int pendingApprovalCount,
            @JsonProperty("device_operation") OperationView deviceOperation,
            CodexAppServerClient.StatusSnapshot bridge) {
    }

    public record DeviceQuota(
            @JsonProperty("five_hour") QuotaView fiveHour,
            @JsonProperty("seven_day") QuotaView sevenDay) {
    }

    public record QuotaView(
            boolean valid,
            @JsonProperty("used_percent") Integer usedPercent,
            @JsonProperty("remaining_percent") Integer remainingPercent,
            @JsonProperty("reset_label") String resetLabel) {

        static QuotaView from(CodexStateStore.QuotaWindow value) {
            return new QuotaView(
                    value.valid(),
                    value.usedPercent(),
                    value.remainingPercent(),
                    value.resetLabel());
        }
    }

    public record DeviceSession(
            @JsonProperty("thread_id") String threadId,
            String title,
            String summary,
            String state,
            @JsonProperty("needs_feedback") boolean needsFeedback,
            SessionApproval approval) {
    }

    public record SessionApproval(
            boolean present,
            @JsonProperty("approval_id") String approvalId,
            @JsonProperty("thread_id") String threadId) {
    }

    public record DeviceApproval(
            @JsonProperty("approval_id") String approvalId,
            @JsonProperty("thread_id") String threadId,
            String nonce,
            @JsonProperty("action_digest") String actionDigest,
            String title,
            String detail,
            @JsonProperty("expires_at_ms") long expiresAtMs) {

        static DeviceApproval from(ApprovalRegistry.ApprovalSnapshot value) {
            return value == null ? null : new DeviceApproval(
                    value.id(),
                    value.threadId(),
                    value.nonce(),
                    value.actionDigest(),
                    value.title(),
                    value.summary(),
                    value.expiresAt().toEpochMilli());
        }
    }

    public record OperationView(
            String kind,
            String state,
            String id,
            String message,
            @JsonProperty("result_thread_id") String resultThreadId) {

        static OperationView from(RecordingLedger.Operation operation) {
            return operation == null ? null : new OperationView(
                    operation.purpose(),
                    operation.state(),
                    operation.id(),
                    operation.message(),
                    operation.resultThreadId());
        }
    }

    public record RecordingResponse(
            @JsonProperty("operation_id") String operationId,
            String state,
            String message,
            @JsonProperty("result_thread_id") String resultThreadId,
            @JsonProperty("result_turn_id") String resultTurnId,
            boolean replay,
            String provider,
            String transcript,
            @JsonProperty("file_name") String fileName,
            @JsonProperty("size_bytes") long sizeBytes,
            String sha256,
            String crc32) {

        static RecordingResponse from(RecordingService.ProcessingResult result) {
            RecordingLedger.Operation operation = result.operation();
            return new RecordingResponse(
                    operation.id(),
                    operation.state(),
                    operation.message(),
                    operation.resultThreadId(),
                    operation.resultTurnId(),
                    result.replay(),
                    operation.provider(),
                    operation.transcript(),
                    operation.fileName(),
                    operation.sizeBytes(),
                    operation.sha256(),
                    operation.crc32());
        }
    }

    public record ResolveApprovalRequest(
            @NotBlank String decision,
            @NotBlank String nonce,
            @NotBlank
            @JsonProperty("action_digest")
            @JsonAlias("actionDigest")
            String actionDigest) {
    }

    public record HealthResponse(
            String status,
            Instant timestamp,
            CodexAppServerClient.StatusSnapshot codex,
            @JsonProperty("asr_provider") String asrProvider) {
    }

    private final BridgeProperties properties;
    private final CodexAppServerClient codex;
    private final CodexStateStore stateStore;
    private final ApprovalRegistry approvals;
    private final FeedbackRegistry feedback;
    private final RecordingLedger ledger;
    private final RecordingService recordings;

    public DeviceController(
            BridgeProperties properties,
            CodexAppServerClient codex,
            CodexStateStore stateStore,
            ApprovalRegistry approvals,
            FeedbackRegistry feedback,
            RecordingLedger ledger,
            RecordingService recordings) {
        this.properties = properties;
        this.codex = codex;
        this.stateStore = stateStore;
        this.approvals = approvals;
        this.feedback = feedback;
        this.ledger = ledger;
        this.recordings = recordings;
    }

    @GetMapping("/device/snapshot")
    public DeviceSnapshot snapshot() {
        ApprovalRegistry.ApprovalQueueSnapshot queue = approvals.queueSnapshot();
        CodexStateStore.StateSnapshot state = stateStore.snapshot(
                approvals.pendingThreadIds(),
                feedback.threadIdsNeedingFeedback());
        DeviceApproval currentApproval = DeviceApproval.from(queue.currentApproval());
        List<DeviceSession> sessions = state.sessions().stream()
                .map(session -> toDeviceSession(session, currentApproval))
                .toList();
        return new DeviceSnapshot(
                state.revision(),
                Instant.now(),
                new DeviceQuota(
                        QuotaView.from(state.quota5h()),
                        QuotaView.from(state.quota7d())),
                state.totalSessionCount(),
                sessions,
                currentApproval,
                queue.pendingCount(),
                ledger.latest().map(OperationView::from).orElse(null),
                codex.status());
    }

    @PostMapping(
            path = "/device/recordings",
            consumes = {"audio/wav", "audio/x-wav", MediaType.APPLICATION_OCTET_STREAM_VALUE},
            produces = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<RecordingResponse> uploadRaw(
            HttpServletRequest request,
            @RequestHeader("Idempotency-Key") String idempotencyKey,
            @RequestHeader("X-Vela-Purpose") String purpose,
            @RequestHeader(value = "X-Vela-Device-Id", required = false) String deviceId,
            @RequestHeader(value = "X-Vela-Thread-Id", required = false) String threadId,
            @RequestHeader(value = "X-Vela-Approval-Id", required = false) String approvalId,
            @RequestHeader("X-Vela-Wav-Crc32") String crc32)
            throws IOException {
        return processRecording(
                request.getInputStream(),
                request.getContentLengthLong(),
                idempotencyKey,
                purpose,
                deviceId,
                threadId,
                approvalId,
                crc32);
    }

    @PostMapping(
            path = "/device/recordings",
            consumes = MediaType.MULTIPART_FORM_DATA_VALUE,
            produces = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<RecordingResponse> uploadMultipart(
            @RequestPart("file") MultipartFile file,
            @RequestHeader("Idempotency-Key") String idempotencyKey,
            @RequestHeader("X-Vela-Purpose") String purpose,
            @RequestHeader(value = "X-Vela-Device-Id", required = false) String deviceId,
            @RequestHeader(value = "X-Vela-Thread-Id", required = false) String threadId,
            @RequestHeader(value = "X-Vela-Approval-Id", required = false) String approvalId,
            @RequestHeader("X-Vela-Wav-Crc32") String crc32)
            throws IOException {
        MediaType contentType = file.getContentType() == null
                ? MediaType.APPLICATION_OCTET_STREAM
                : MediaType.parseMediaType(file.getContentType());
        if (!contentType.isCompatibleWith(MediaType.parseMediaType("audio/wav"))
                && !contentType.isCompatibleWith(MediaType.parseMediaType("audio/x-wav"))
                && !contentType.isCompatibleWith(MediaType.APPLICATION_OCTET_STREAM)) {
            throw new BridgeApiException(
                    HttpStatus.UNSUPPORTED_MEDIA_TYPE,
                    "unsupported_recording_type",
                    "Multipart file must be audio/wav");
        }
        return processRecording(
                file.getInputStream(),
                file.getSize(),
                idempotencyKey,
                purpose,
                deviceId,
                threadId,
                approvalId,
                crc32);
    }

    @PostMapping(
            path = "/device/approvals/{id}/resolve",
            consumes = MediaType.APPLICATION_JSON_VALUE,
            produces = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<CodexAppServerClient.ApprovalResolution> resolveApproval(
            @PathVariable String id,
            @Valid @RequestBody ResolveApprovalRequest request) {
        CodexAppServerClient.ApprovalResolution resolution = codex.resolveApproval(
                id,
                request.decision(),
                request.nonce(),
                request.actionDigest());
        return ResponseEntity.ok(resolution);
    }

    @GetMapping("/health")
    public ResponseEntity<HealthResponse> health() {
        CodexAppServerClient.StatusSnapshot status = codex.status();
        boolean healthy = !status.enabled() || status.connected();
        return ResponseEntity
                .status(healthy ? HttpStatus.OK : HttpStatus.SERVICE_UNAVAILABLE)
                .body(new HealthResponse(
                        healthy ? "ok" : "degraded",
                        Instant.now(),
                        status,
                        properties.getAsr().getProvider().name().toLowerCase()));
    }

    private DeviceSession toDeviceSession(
            CodexStateStore.SessionSnapshot session,
            DeviceApproval currentApproval) {
        FeedbackRegistry.FeedbackTask feedbackTask =
                feedback.findByThreadId(session.id()).orElse(null);
        SessionApproval sessionApproval = null;
        if (currentApproval != null && currentApproval.threadId().equals(session.id())) {
            sessionApproval = new SessionApproval(
                    true,
                    currentApproval.approvalId(),
                    session.id());
        } else if (feedbackTask != null) {
            sessionApproval = new SessionApproval(
                    false,
                    feedbackTask.approvalId(),
                    session.id());
        }
        return new DeviceSession(
                session.id(),
                firstNonBlank(session.name(), session.preview(), "Codex 会话"),
                firstNonBlank(session.lastMessage(), session.preview(), ""),
                deviceState(session),
                session.needsFeedback(),
                sessionApproval);
    }

    private ResponseEntity<RecordingResponse> processRecording(
            InputStream input,
            long contentLength,
            String idempotencyKey,
            String purpose,
            String deviceId,
            String threadId,
            String approvalId,
            String crc32) {
        RecordingService.RecordingCommand command = new RecordingService.RecordingCommand(
                deviceId,
                idempotencyKey,
                RecordingService.Purpose.parse(purpose),
                threadId,
                approvalId,
                crc32);
        RecordingService.ProcessingResult result = recordings.process(command, input, contentLength);
        HttpStatus status = result.accepted() ? HttpStatus.ACCEPTED : HttpStatus.OK;
        return ResponseEntity.status(status).body(RecordingResponse.from(result));
    }

    private static String deviceState(CodexStateStore.SessionSnapshot session) {
        if (session.pendingApproval()) {
            return "waiting_approval";
        }
        if (session.needsFeedback()) {
            return "needs_feedback";
        }
        return switch (session.status()) {
            case "active" -> "running";
            case "systemError" -> "error";
            case "idle" -> "completed";
            default -> "idle";
        };
    }

    private static String firstNonBlank(String... values) {
        for (String value : values) {
            if (value != null && !value.isBlank()) {
                return value;
            }
        }
        return "";
    }
}
