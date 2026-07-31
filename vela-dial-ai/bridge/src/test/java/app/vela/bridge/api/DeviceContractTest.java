package app.vela.bridge.api;

import app.vela.bridge.audio.FeedbackRegistry;
import app.vela.bridge.audio.RecordingLedger;
import app.vela.bridge.audio.RecordingService;
import app.vela.bridge.codex.ApprovalRegistry;
import app.vela.bridge.codex.CodexAppServerClient;
import app.vela.bridge.codex.CodexStateStore;
import app.vela.bridge.config.BridgeProperties;
import org.junit.jupiter.api.Test;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

import java.time.Instant;
import java.util.List;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

class DeviceContractTest {

    private final JsonMapper jsonMapper = JsonMapper.builder().findAndAddModules().build();

    @Test
    void snapshotMatchesFrozenFirmwareFieldNames() throws Exception {
        BridgeProperties properties = new BridgeProperties();
        CodexAppServerClient codex = mock(CodexAppServerClient.class);
        CodexStateStore stateStore = mock(CodexStateStore.class);
        ApprovalRegistry approvals = mock(ApprovalRegistry.class);
        FeedbackRegistry feedback = mock(FeedbackRegistry.class);
        RecordingLedger ledger = mock(RecordingLedger.class);
        RecordingService recordings = mock(RecordingService.class);

        Instant now = Instant.now();
        ApprovalRegistry.ApprovalSnapshot approval = new ApprovalRegistry.ApprovalSnapshot(
                "apv_device",
                ApprovalRegistry.ApprovalKind.COMMAND_EXECUTION,
                "thread-20",
                "turn-1",
                "item-1",
                "运行命令",
                "rm a.tmp",
                "/workspace",
                "Needs permission",
                "nonce-1",
                "a".repeat(64),
                List.of("accept", "decline"),
                now,
                now.plusSeconds(120));
        CodexStateStore.SessionSnapshot session = new CodexStateStore.SessionSnapshot(
                "thread-20",
                "Build firmware",
                "compile",
                "/workspace",
                "active",
                List.of("waitingOnApproval"),
                "turn-1",
                "commandExecution",
                "rm a.tmp",
                500_000L,
                100_000L,
                200_000L,
                40,
                now.getEpochSecond(),
                true,
                false);
        CodexStateStore.StateSnapshot state = new CodexStateStore.StateSnapshot(
                9,
                List.of(new CodexStateStore.QuotaWindow(
                        true,
                        "primary",
                        "7D",
                        10_080L,
                        57,
                        43,
                        "2天后")),
                new CodexStateStore.AccountTokenUsage(
                        true,
                        987_654_321L,
                        333_333L,
                        "2026-07-30",
                        7_654_321L,
                        23),
                20,
                List.of(session));
        when(codex.status()).thenReturn(new CodexAppServerClient.StatusSnapshot(
                true, true, CodexAppServerClient.ConnectionState.CONNECTED,
                123L, "codex-test", now, null));
        when(approvals.queueSnapshot()).thenReturn(
                new ApprovalRegistry.ApprovalQueueSnapshot(1, approval));
        when(approvals.pendingThreadIds()).thenReturn(List.of("thread-20"));
        when(feedback.threadIdsNeedingFeedback()).thenReturn(List.of());
        when(feedback.findByThreadId("thread-20")).thenReturn(Optional.empty());
        when(stateStore.snapshot(List.of("thread-20"), List.of())).thenReturn(state);
        when(ledger.latest()).thenReturn(Optional.empty());

        DeviceController controller = new DeviceController(
                properties, codex, stateStore, approvals, feedback, ledger, recordings);
        JsonNode json = jsonMapper.readTree(jsonMapper.writeValueAsString(controller.snapshot()));

        assertThat(json.path("revision").asLong()).isEqualTo(9);
        assertThat(json.path("quota").path("windows")).hasSize(1);
        assertThat(json.path("quota").path("windows").get(0).path("key").asText())
                .isEqualTo("primary");
        assertThat(json.path("quota").path("windows").get(0).path("label").asText())
                .isEqualTo("7D");
        assertThat(json.path("quota").path("windows").get(0).path("window_minutes").asLong())
                .isEqualTo(10_080);
        assertThat(json.path("quota").path("five_hour").path("valid").asBoolean()).isFalse();
        assertThat(json.path("quota").path("seven_day").path("used_percent").asInt()).isEqualTo(57);
        assertThat(json.path("quota").path("tokens").path("lifetime_tokens").asLong())
                .isEqualTo(987_654_321L);
        assertThat(json.path("quota").path("tokens").path("latest_day_tokens").asLong())
                .isEqualTo(333_333L);
        assertThat(json.path("quota").path("tokens").path("latest_day_label").asText())
                .isEqualTo("2026-07-30");
        assertThat(json.path("quota").path("tokens").path("current_streak_days").asInt())
                .isEqualTo(23);
        assertThat(json.path("total_session_count").asInt()).isEqualTo(20);
        assertThat(json.path("sessions").get(0).path("thread_id").asText()).isEqualTo("thread-20");
        assertThat(json.path("sessions").get(0).path("total_tokens").asLong())
                .isEqualTo(500_000);
        assertThat(json.path("sessions").get(0).path("last_tokens").asLong())
                .isEqualTo(100_000);
        assertThat(json.path("sessions").get(0).path("context_window_tokens").asLong())
                .isEqualTo(200_000);
        assertThat(json.path("sessions").get(0).path("context_used_percent").asInt())
                .isEqualTo(40);
        assertThat(json.path("sessions").get(0).path("state").asText())
                .isEqualTo("waiting_approval");
        assertThat(json.path("current_approval").path("approval_id").asText())
                .isEqualTo("apv_device");
        assertThat(json.path("current_approval").path("action_digest").asText()).hasSize(64);
        assertThat(json.path("current_approval").path("expires_at_ms").isIntegralNumber()).isTrue();
        assertThat(json.path("pending_approval_count").asInt()).isEqualTo(1);
        assertThat(json.has("device_operation")).isTrue();
    }

    @Test
    void recordingAndApprovalInputsAcceptFirmwareVocabulary() throws Exception {
        assertThat(RecordingService.Purpose.parse("new_session"))
                .isEqualTo(RecordingService.Purpose.NEW_THREAD);
        assertThat(RecordingService.Purpose.parse("reject_reason"))
                .isEqualTo(RecordingService.Purpose.APPROVAL_REASON);

        DeviceController.ResolveApprovalRequest request = jsonMapper.readValue(
                """
                {"decision":"allow","nonce":"n","action_digest":"%s"}
                """.formatted("b".repeat(64)),
                DeviceController.ResolveApprovalRequest.class);
        assertThat(request.actionDigest()).hasSize(64);

        DeviceController.RecordingResponse response = new DeviceController.RecordingResponse(
                "op-1",
                "completed",
                "录音已提交到 Codex",
                "thread-new",
                "turn-new",
                false,
                "codex",
                null,
                "audio.wav",
                100,
                "c".repeat(64),
                "deadbeef");
        JsonNode responseJson = jsonMapper.readTree(jsonMapper.writeValueAsString(response));
        assertThat(responseJson.path("operation_id").asText()).isEqualTo("op-1");
        assertThat(responseJson.path("result_thread_id").asText()).isEqualTo("thread-new");
        assertThat(responseJson.path("message").asText()).contains("Codex");
    }

    @Test
    void approvalWriteCompletesDeviceRequestEvenBeforeResolvedNotification() {
        BridgeProperties properties = new BridgeProperties();
        CodexAppServerClient codex = mock(CodexAppServerClient.class);
        CodexStateStore stateStore = mock(CodexStateStore.class);
        ApprovalRegistry approvals = mock(ApprovalRegistry.class);
        FeedbackRegistry feedback = mock(FeedbackRegistry.class);
        RecordingLedger ledger = mock(RecordingLedger.class);
        RecordingService recordings = mock(RecordingService.class);
        when(codex.resolveApproval("apv-1", "allow", "nonce-1", "a".repeat(64)))
                .thenReturn(new CodexAppServerClient.ApprovalResolution(
                        "apv-1", "accept", false, false, Instant.now()));

        DeviceController controller = new DeviceController(
                properties, codex, stateStore, approvals, feedback, ledger, recordings);
        var response = controller.resolveApproval(
                "apv-1",
                new DeviceController.ResolveApprovalRequest(
                        "allow", "nonce-1", "a".repeat(64)));

        assertThat(response.getStatusCode().value()).isEqualTo(200);
        assertThat(response.getBody()).isNotNull();
        assertThat(response.getBody().codexConfirmed()).isFalse();
    }
}
