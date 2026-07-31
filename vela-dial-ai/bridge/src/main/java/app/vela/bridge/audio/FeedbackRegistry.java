package app.vela.bridge.audio;

import app.vela.bridge.config.BridgeProperties;
import jakarta.annotation.PostConstruct;
import org.springframework.stereotype.Component;
import tools.jackson.core.type.TypeReference;
import tools.jackson.databind.json.JsonMapper;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Component
public class FeedbackRegistry {

    public record FeedbackTask(
            String approvalId,
            String threadId,
            boolean needsFeedback,
            Instant createdAt,
            Instant updatedAt) {
    }

    private final JsonMapper jsonMapper;
    private final Path stateFile;
    private final Map<String, FeedbackTask> tasks = new LinkedHashMap<>();

    public FeedbackRegistry(BridgeProperties properties, JsonMapper jsonMapper) {
        Path directory = properties.getRecordings().getDirectory().toAbsolutePath().normalize();
        this.jsonMapper = jsonMapper;
        this.stateFile = directory.resolve("feedback-state.json");
    }

    @PostConstruct
    synchronized void load() {
        if (!Files.isRegularFile(stateFile)) {
            return;
        }
        try {
            List<FeedbackTask> saved = jsonMapper.readValue(
                    Files.readString(stateFile),
                    new TypeReference<List<FeedbackTask>>() {
                    });
            saved.stream()
                    .filter(FeedbackTask::needsFeedback)
                    .forEach(task -> tasks.put(task.approvalId(), task));
        } catch (Exception exception) {
            throw new IllegalStateException("Unable to read persisted approval feedback state", exception);
        }
    }

    public synchronized void markNeedsFeedback(String approvalId, String threadId) {
        Instant now = Instant.now();
        FeedbackTask existing = tasks.get(approvalId);
        tasks.put(approvalId, new FeedbackTask(
                approvalId,
                threadId,
                true,
                existing == null ? now : existing.createdAt(),
                now));
        persist();
    }

    public synchronized void complete(String approvalId) {
        if (tasks.remove(approvalId) != null) {
            persist();
        }
    }

    public synchronized Optional<FeedbackTask> find(String approvalId) {
        return Optional.ofNullable(tasks.get(approvalId));
    }

    public synchronized List<String> threadIdsNeedingFeedback() {
        return tasks.values().stream()
                .filter(FeedbackTask::needsFeedback)
                .map(FeedbackTask::threadId)
                .distinct()
                .toList();
    }

    public synchronized Optional<FeedbackTask> findByThreadId(String threadId) {
        return tasks.values().stream()
                .filter(task -> task.threadId().equals(threadId))
                .findFirst();
    }

    private void persist() {
        try {
            Files.createDirectories(stateFile.getParent());
            Path temporary = Files.createTempFile(stateFile.getParent(), "feedback-", ".tmp");
            Files.writeString(
                    temporary,
                    jsonMapper.writeValueAsString(tasks.values()),
                    StandardCharsets.UTF_8);
            try {
                Files.move(
                        temporary,
                        stateFile,
                        StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException exception) {
                Files.move(temporary, stateFile, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException exception) {
            throw new IllegalStateException("Unable to persist approval feedback state", exception);
        }
    }
}
