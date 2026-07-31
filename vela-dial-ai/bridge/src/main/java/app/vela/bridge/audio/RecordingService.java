package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.codex.ApprovalRegistry;
import app.vela.bridge.codex.CodexAppServerClient;
import app.vela.bridge.codex.CodexStateStore;
import app.vela.bridge.config.BridgeProperties;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.io.InputStream;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Locale;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletionException;

@Service
public class RecordingService {

    public enum Purpose {
        NEW_THREAD,
        PROMPT,
        STEER,
        APPROVAL_REASON,
        STORE_ONLY;

        public static Purpose parse(String value) {
            if (value == null || value.isBlank()) {
                throw new BridgeApiException(
                        HttpStatus.BAD_REQUEST,
                        "missing_recording_purpose",
                        "X-Vela-Purpose is required");
            }
            String normalized = value.trim()
                    .replace('-', '_')
                    .toUpperCase(Locale.ROOT);
            try {
                return switch (normalized) {
                    case "NEW_SESSION" -> NEW_THREAD;
                    case "REJECT_REASON" -> APPROVAL_REASON;
                    default -> valueOf(normalized);
                };
            } catch (IllegalArgumentException exception) {
                throw new BridgeApiException(
                        HttpStatus.BAD_REQUEST,
                        "invalid_recording_purpose",
                        "X-Vela-Purpose must be new-thread, prompt, steer, approval-reason, or store-only");
            }
        }
    }

    public record RecordingCommand(
            String deviceId,
            String idempotencyKey,
            Purpose purpose,
            String threadId,
            String approvalId,
            String crc32) {
    }

    public record ProcessingResult(
            RecordingLedger.Operation operation,
            boolean replay,
            boolean accepted) {
    }

    private record DispatchResult(
            String threadId,
            String turnId,
            String mode) {
    }

    private final BridgeProperties properties;
    private final JsonMapper jsonMapper;
    private final RecordingStorage storage;
    private final RecordingLedger ledger;
    private final VolcengineAsrClient volcengine;
    private final CodexAppServerClient codex;
    private final CodexStateStore stateStore;
    private final ApprovalRegistry approvals;
    private final FeedbackRegistry feedback;

    public RecordingService(
            BridgeProperties properties,
            JsonMapper jsonMapper,
            RecordingStorage storage,
            RecordingLedger ledger,
            VolcengineAsrClient volcengine,
            CodexAppServerClient codex,
            CodexStateStore stateStore,
            ApprovalRegistry approvals,
            FeedbackRegistry feedback) {
        this.properties = properties;
        this.jsonMapper = jsonMapper;
        this.storage = storage;
        this.ledger = ledger;
        this.volcengine = volcengine;
        this.codex = codex;
        this.stateStore = stateStore;
        this.approvals = approvals;
        this.feedback = feedback;
    }

    public ProcessingResult process(
            RecordingCommand rawCommand,
            InputStream body,
            long contentLength) {
        RecordingCommand command = normalize(rawCommand);
        String ledgerKey = ledger.ledgerKey(
                command.deviceId(),
                command.idempotencyKey(),
                command.purpose().name());

        synchronized (ledger.lock(ledgerKey)) {
            RecordingLedger.Operation existing = ledger.load(ledgerKey).orElse(null);
            if (existing != null) {
                verifyReplay(existing, command, contentLength);
                if (RecordingLedger.COMPLETED.equals(existing.state())
                        || RecordingLedger.CODEX_SUBMITTED.equals(existing.state())) {
                    return new ProcessingResult(
                            existing,
                            true,
                            !RecordingLedger.COMPLETED.equals(existing.state()));
                }
            }

            RecordingLedger.Operation operation = existing;
            if (operation == null) {
                RecordingStorage.StoredRecording saved =
                        storage.saveWav(body, contentLength, command.crc32());
                Instant now = Instant.now();
                operation = new RecordingLedger.Operation(
                        UUID.randomUUID().toString(),
                        ledgerKey,
                        command.deviceId(),
                        command.idempotencyKey(),
                        command.purpose().name(),
                        command.threadId(),
                        command.approvalId(),
                        RecordingLedger.RECEIVED,
                        saved.fileName(),
                        saved.sizeBytes(),
                        saved.sha256(),
                        saved.crc32(),
                        properties.getAsr().getProvider().name().toLowerCase(Locale.ROOT),
                        null,
                        null,
                        null,
                        null,
                        "WAV 已安全保存",
                        now,
                        now);
                ledger.save(operation);
            }

            if (command.purpose() == Purpose.STORE_ONLY) {
                operation = operation.advance(
                        RecordingLedger.COMPLETED,
                        operation.transcript(),
                        operation.resultThreadId(),
                        operation.resultTurnId(),
                        "storeOnly",
                        "录音已保存，未发送到 Codex");
                ledger.save(operation);
                return new ProcessingResult(operation, existing != null, false);
            }

            Path wav = storage.resolve(operation.fileName());
            if (RecordingLedger.RECEIVED.equals(operation.state())) {
                String transcript = operation.transcript();
                if (provider(operation) == BridgeProperties.AsrProvider.VOLCENGINE) {
                    transcript = volcengine.transcribe(wav).text();
                }
                operation = operation.advance(
                        RecordingLedger.TRANSCRIBED,
                        transcript,
                        operation.resultThreadId(),
                        operation.resultTurnId(),
                        operation.dispatchMode(),
                        provider(operation) == BridgeProperties.AsrProvider.CODEX
                                ? "录音将作为 Codex localAudio 输入"
                                : "火山引擎识别完成");
                ledger.save(operation);
            }

            ArrayNode input = inputFor(operation, wav);
            try {
                if (operation.resultThreadId() == null) {
                    String targetThread = resolveTargetThread(command, operation);
                    operation = operation.advance(
                            RecordingLedger.TRANSCRIBED,
                            operation.transcript(),
                            targetThread,
                            operation.resultTurnId(),
                            operation.dispatchMode(),
                            "Codex 会话已就绪");
                    ledger.save(operation);
                }
                DispatchResult dispatched = dispatch(command, operation, input);
                operation = operation.advance(
                        RecordingLedger.CODEX_SUBMITTED,
                        operation.transcript(),
                        dispatched.threadId(),
                        dispatched.turnId(),
                        dispatched.mode(),
                        "Codex 已接受输入");
                ledger.save(operation);
            } catch (CompletionException exception) {
                Throwable cause = unwrap(exception);
                if (cause instanceof CodexAppServerClient.CodexRequestTimeoutException timeout
                        && timeout.unknownOutcome()) {
                    operation = operation.advance(
                            RecordingLedger.CODEX_SUBMITTED,
                            operation.transcript(),
                            operation.resultThreadId(),
                            operation.resultTurnId(),
                            operation.dispatchMode(),
                            "Codex 请求超时，结果未知；Bridge 不会自动重复提交");
                    ledger.save(operation);
                    return new ProcessingResult(operation, existing != null, true);
                }
                throw mapDispatchFailure(cause);
            } catch (RuntimeException exception) {
                if (exception instanceof CodexAppServerClient.CodexRequestTimeoutException timeout
                        && timeout.unknownOutcome()) {
                    operation = operation.advance(
                            RecordingLedger.CODEX_SUBMITTED,
                            operation.transcript(),
                            operation.resultThreadId(),
                            operation.resultTurnId(),
                            operation.dispatchMode(),
                            "Codex 请求超时，结果未知；Bridge 不会自动重复提交");
                    ledger.save(operation);
                    return new ProcessingResult(operation, existing != null, true);
                }
                throw exception;
            }

            operation = operation.advance(
                    RecordingLedger.COMPLETED,
                    operation.transcript(),
                    operation.resultThreadId(),
                    operation.resultTurnId(),
                    operation.dispatchMode(),
                    "录音已提交到 Codex");
            ledger.save(operation);
            if (command.purpose() == Purpose.APPROVAL_REASON) {
                feedback.complete(requireNonBlank(command.approvalId(), "X-Vela-Approval-Id"));
            }
            return new ProcessingResult(operation, existing != null, false);
        }
    }

    private DispatchResult dispatch(
            RecordingCommand command,
            RecordingLedger.Operation operation,
            ArrayNode input) {
        String targetThread = operation.resultThreadId();
        if (targetThread == null) {
            throw new IllegalStateException("Codex target thread was not prepared");
        }

        String activeTurn = stateStore.currentTurnId(targetThread).orElse(null);
        if (command.purpose() == Purpose.STEER && activeTurn == null) {
            throw new BridgeApiException(
                    HttpStatus.CONFLICT,
                    "turn_not_active",
                    "X-Vela-Purpose=steer requires an active Codex turn");
        }

        if (activeTurn != null) {
            JsonNode result = codex.steerTurn(
                            targetThread,
                            activeTurn,
                            input,
                            operation.id())
                    .join();
            return new DispatchResult(
                    targetThread,
                    textOrNull(result, "turnId"),
                    "turnSteer");
        }

        JsonNode result = codex.startTurn(targetThread, input, operation.id()).join();
        return new DispatchResult(
                targetThread,
                textOrNull(result.path("turn"), "id"),
                "turnStart");
    }

    private String resolveTargetThread(
            RecordingCommand command,
            RecordingLedger.Operation operation) {
        if (command.purpose() == Purpose.APPROVAL_REASON) {
            FeedbackRegistry.FeedbackTask task = feedback.find(
                            requireNonBlank(command.approvalId(), "X-Vela-Approval-Id"))
                    .orElseThrow(() -> new BridgeApiException(
                            HttpStatus.CONFLICT,
                            "approval_feedback_not_needed",
                            "This approval does not currently need a rejection reason"));
            if (command.threadId() != null && !command.threadId().equals(task.threadId())) {
                throw new BridgeApiException(
                        HttpStatus.CONFLICT,
                        "approval_thread_mismatch",
                        "Recording thread does not match the rejected approval");
            }
            resumeIfNeeded(task.threadId());
            return task.threadId();
        }

        if (command.purpose() != Purpose.NEW_THREAD && command.threadId() != null) {
            resumeIfNeeded(command.threadId());
            return command.threadId();
        }

        JsonNode result = codex.startThread(properties.getCodex().getDefaultCwd()).join();
        String threadId = textOrNull(result.path("thread"), "id");
        if (threadId == null) {
            throw new BridgeApiException(
                    HttpStatus.BAD_GATEWAY,
                    "codex_invalid_response",
                    "thread/start response did not include thread.id");
        }
        return threadId;
    }

    private void resumeIfNeeded(String threadId) {
        if (stateStore.findSession(threadId).isEmpty()) {
            codex.resumeThread(threadId).join();
        }
    }

    private ArrayNode inputFor(RecordingLedger.Operation operation, Path wav) {
        ArrayNode input = jsonMapper.createArrayNode();
        ObjectNode value = input.addObject();
        if (provider(operation) == BridgeProperties.AsrProvider.CODEX) {
            value.put("type", "localAudio");
            value.put("path", wav.toAbsolutePath().normalize().toString());
        } else {
            value.put("type", "text");
            value.put("text", requireNonBlank(operation.transcript(), "ASR transcript"));
            value.putArray("text_elements");
        }
        return input;
    }

    private BridgeProperties.AsrProvider provider(RecordingLedger.Operation operation) {
        try {
            return BridgeProperties.AsrProvider.valueOf(operation.provider().toUpperCase(Locale.ROOT));
        } catch (IllegalArgumentException exception) {
            throw new BridgeApiException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "unknown_asr_provider",
                    "Recording ledger contains an unsupported ASR provider");
        }
    }

    private static RecordingCommand normalize(RecordingCommand command) {
        Objects.requireNonNull(command, "command");
        String deviceId = command.deviceId() == null || command.deviceId().isBlank()
                ? "default"
                : command.deviceId().trim();
        if (deviceId.length() > 128) {
            throw new BridgeApiException(
                    HttpStatus.BAD_REQUEST, "invalid_device_id", "X-Vela-Device-Id is too long");
        }
        String key = requireNonBlank(command.idempotencyKey(), "Idempotency-Key").trim();
        if (key.length() > 200) {
            throw new BridgeApiException(
                    HttpStatus.BAD_REQUEST, "invalid_idempotency_key", "Idempotency-Key is too long");
        }
        String threadId = blankToNull(command.threadId());
        String approvalId = blankToNull(command.approvalId());
        String crc32 = normalizeCrc(command.crc32());
        if (command.purpose() == Purpose.STEER && threadId == null) {
            throw new BridgeApiException(
                    HttpStatus.BAD_REQUEST, "missing_thread_id",
                    "X-Vela-Thread-Id is required for steer");
        }
        if (command.purpose() == Purpose.APPROVAL_REASON && approvalId == null) {
            throw new BridgeApiException(
                    HttpStatus.BAD_REQUEST, "missing_approval_id",
                    "X-Vela-Approval-Id is required for approval-reason");
        }
        return new RecordingCommand(
                deviceId,
                key,
                command.purpose(),
                threadId,
                approvalId,
                crc32);
    }

    private static void verifyReplay(
            RecordingLedger.Operation operation,
            RecordingCommand command,
            long contentLength) {
        boolean metadataMatches =
                Objects.equals(operation.requestedThreadId(), command.threadId())
                        && Objects.equals(operation.approvalId(), command.approvalId());
        boolean sizeMatches = contentLength < 0 || contentLength == operation.sizeBytes();
        boolean crcMatches = Objects.equals(operation.crc32(), command.crc32());
        if (!metadataMatches || !sizeMatches || !crcMatches) {
            throw new BridgeApiException(
                    HttpStatus.CONFLICT,
                    "idempotency_conflict",
                    "Idempotency-Key was already used with different recording metadata");
        }
    }

    private static RuntimeException mapDispatchFailure(Throwable cause) {
        if (cause instanceof RuntimeException runtimeException) {
            return runtimeException;
        }
        return new BridgeApiException(
                HttpStatus.BAD_GATEWAY,
                "codex_dispatch_failed",
                "Unable to submit recording to Codex",
                cause);
    }

    private static Throwable unwrap(Throwable throwable) {
        Throwable current = throwable;
        while (current instanceof CompletionException && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static String textOrNull(JsonNode node, String field) {
        JsonNode value = node.get(field);
        return value != null && value.isTextual() ? value.asText() : null;
    }

    private static String requireNonBlank(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new BridgeApiException(
                    HttpStatus.BAD_REQUEST,
                    "missing_value",
                    name + " is required");
        }
        return value;
    }

    private static String blankToNull(String value) {
        return value == null || value.isBlank() ? null : value.trim();
    }

    private static String normalizeCrc(String value) {
        String normalized = requireNonBlank(value, "X-Vela-Wav-Crc32")
                .trim()
                .toLowerCase(Locale.ROOT);
        if (normalized.startsWith("0x")) {
            normalized = normalized.substring(2);
        }
        try {
            return String.format("%08x", Long.parseUnsignedLong(normalized, 16));
        } catch (NumberFormatException exception) {
            throw new BridgeApiException(
                    HttpStatus.UNPROCESSABLE_ENTITY,
                    "invalid_wav_crc32",
                    "X-Vela-Wav-Crc32 must be an unsigned hexadecimal CRC32");
        }
    }
}
