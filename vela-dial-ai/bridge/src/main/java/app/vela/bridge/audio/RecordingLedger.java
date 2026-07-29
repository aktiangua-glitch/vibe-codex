package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import jakarta.annotation.PostConstruct;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;
import tools.jackson.databind.json.JsonMapper;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Instant;
import java.util.Comparator;
import java.util.HexFormat;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Stream;

@Component
public class RecordingLedger {

    public static final String RECEIVED = "received";
    public static final String TRANSCRIBED = "transcribed";
    public static final String CODEX_SUBMITTED = "codexSubmitted";
    public static final String COMPLETED = "completed";

    public record Operation(
            String id,
            String ledgerKey,
            String deviceId,
            String idempotencyKey,
            String purpose,
            String requestedThreadId,
            String approvalId,
            String state,
            String fileName,
            long sizeBytes,
            String sha256,
            String crc32,
            String provider,
            String transcript,
            String resultThreadId,
            String resultTurnId,
            String dispatchMode,
            String message,
            Instant createdAt,
            Instant updatedAt) {

        Operation advance(
                String nextState,
                String nextTranscript,
                String nextThreadId,
                String nextTurnId,
                String nextDispatchMode,
                String nextMessage) {
            return new Operation(
                    id,
                    ledgerKey,
                    deviceId,
                    idempotencyKey,
                    purpose,
                    requestedThreadId,
                    approvalId,
                    nextState,
                    fileName,
                    sizeBytes,
                    sha256,
                    crc32,
                    provider,
                    nextTranscript,
                    nextThreadId,
                    nextTurnId,
                    nextDispatchMode,
                    nextMessage,
                    createdAt,
                    Instant.now());
        }
    }

    private final JsonMapper jsonMapper;
    private final Path operationsDirectory;
    private final ConcurrentHashMap<String, Object> locks = new ConcurrentHashMap<>();
    private final AtomicReference<Operation> latest = new AtomicReference<>();

    public RecordingLedger(BridgeProperties properties, JsonMapper jsonMapper) {
        this.jsonMapper = jsonMapper;
        this.operationsDirectory = properties.getRecordings().getDirectory()
                .toAbsolutePath()
                .normalize()
                .resolve("operations");
    }

    @PostConstruct
    void initialize() {
        try {
            Files.createDirectories(operationsDirectory);
            try (Stream<Path> files = Files.list(operationsDirectory)) {
                files.filter(path -> path.getFileName().toString().endsWith(".json"))
                        .map(this::readQuietly)
                        .flatMap(Optional::stream)
                        .max(Comparator.comparing(Operation::updatedAt))
                        .ifPresent(latest::set);
            }
        } catch (IOException exception) {
            throw new IllegalStateException("Unable to initialize recording ledger", exception);
        }
    }

    public String ledgerKey(String deviceId, String idempotencyKey, String purpose) {
        String canonical = deviceId + "\u0000" + idempotencyKey + "\u0000" + purpose;
        try {
            return HexFormat.of().formatHex(
                    MessageDigest.getInstance("SHA-256")
                            .digest(canonical.getBytes(StandardCharsets.UTF_8)));
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        }
    }

    public Object lock(String ledgerKey) {
        return locks.computeIfAbsent(ledgerKey, ignored -> new Object());
    }

    public Optional<Operation> load(String ledgerKey) {
        Path path = operationPath(ledgerKey);
        if (!Files.isRegularFile(path)) {
            return Optional.empty();
        }
        try {
            return Optional.of(jsonMapper.readValue(Files.readString(path), Operation.class));
        } catch (Exception exception) {
            throw new BridgeApiException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "recording_ledger_corrupt",
                    "Recording operation ledger is unreadable",
                    exception);
        }
    }

    public void save(Operation operation) {
        Path target = operationPath(operation.ledgerKey());
        try {
            Files.createDirectories(operationsDirectory);
            Path temporary = Files.createTempFile(operationsDirectory, operation.ledgerKey(), ".tmp");
            Files.writeString(
                    temporary,
                    jsonMapper.writeValueAsString(operation),
                    StandardCharsets.UTF_8);
            try {
                Files.move(
                        temporary,
                        target,
                        StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException exception) {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING);
            }
            latest.accumulateAndGet(operation, (current, candidate) ->
                    current == null || candidate.updatedAt().isAfter(current.updatedAt())
                            ? candidate
                            : current);
        } catch (IOException exception) {
            throw new BridgeApiException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "recording_ledger_write_failed",
                    "Unable to persist recording operation",
                    exception);
        }
    }

    public Optional<Operation> latest() {
        return Optional.ofNullable(latest.get());
    }

    private Path operationPath(String ledgerKey) {
        return operationsDirectory.resolve(ledgerKey + ".json");
    }

    private Optional<Operation> readQuietly(Path path) {
        try {
            return Optional.of(jsonMapper.readValue(Files.readString(path), Operation.class));
        } catch (Exception ignored) {
            return Optional.empty();
        }
    }
}
