package app.vela.bridge.codex;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

import java.io.BufferedReader;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Reads the same local Codex rollout snapshots used by Open Island.
 *
 * The app-server is the preferred live source. Rollout files are the
 * important fallback because Codex versions can temporarily time out on
 * account/usage/read while still writing token_count events locally.
 */
@Component
public class CodexRolloutUsageReader {

    private static final Logger log = LoggerFactory.getLogger(CodexRolloutUsageReader.class);
    private static final Path DEFAULT_ROOT = Path.of(
            System.getProperty("user.home"), ".codex", "sessions");

    private final JsonMapper jsonMapper;
    private final CodexStateStore stateStore;

    public CodexRolloutUsageReader(JsonMapper jsonMapper, CodexStateStore stateStore) {
        this.jsonMapper = jsonMapper;
        this.stateStore = stateStore;
    }

    public void refresh() {
        Snapshot snapshot = findLatest();
        if (snapshot == null || !snapshot.rateLimits().isObject()) {
            return;
        }
        stateStore.mergeRateLimits(snapshot.rateLimits());
        log.debug("Merged Codex rollout rate limits from {}", snapshot.path());
    }

    private Snapshot findLatest() {
        if (!Files.isDirectory(DEFAULT_ROOT)) {
            return null;
        }

        try (Stream<Path> files = Files.walk(DEFAULT_ROOT)) {
            List<Path> candidates = files
                    .filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().startsWith("rollout-"))
                    .filter(path -> path.getFileName().toString().endsWith(".jsonl"))
                    .sorted(Comparator.comparingLong(
                            CodexRolloutUsageReader::modifiedAt).reversed())
                    .limit(24)
                    .toList();
            for (Path candidate : candidates) {
                Snapshot snapshot = readLatestSnapshot(candidate);
                if (snapshot != null) {
                    return snapshot;
                }
            }
        } catch (IOException exception) {
            log.debug("Unable to scan Codex rollout usage: {}", exception.getMessage());
        }
        return null;
    }

    private Snapshot readLatestSnapshot(Path path) {
        Snapshot latest = null;
        try (BufferedReader reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                try {
                    JsonNode root = jsonMapper.readTree(line);
                    if (!"event_msg".equals(root.path("type").asText())
                            || !"token_count".equals(root.path("payload").path("type").asText())) {
                        continue;
                    }
                    JsonNode limits = root.path("payload").path("rate_limits");
                    if (limits.isObject() && (limits.has("primary") || limits.has("secondary"))) {
                        latest = new Snapshot(path, limits);
                    }
                } catch (RuntimeException ignored) {
                    // Codex may append a partial final JSONL line while active.
                }
            }
        } catch (IOException exception) {
            log.debug("Unable to read Codex rollout {}: {}", path, exception.getMessage());
        }
        return latest;
    }

    private static long modifiedAt(Path path) {
        try {
            return Files.getLastModifiedTime(path).toMillis();
        } catch (IOException exception) {
            return Instant.EPOCH.toEpochMilli();
        }
    }

    private record Snapshot(Path path, JsonNode rateLimits) {
    }
}
