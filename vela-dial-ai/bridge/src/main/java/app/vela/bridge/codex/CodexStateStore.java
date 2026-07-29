package app.vela.bridge.codex;

import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.node.NullNode;
import tools.jackson.databind.node.ObjectNode;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicLong;

@Component
public class CodexStateStore {

    public record QuotaWindow(
            boolean valid,
            Integer usedPercent,
            Integer remainingPercent,
            String resetLabel) {

        static QuotaWindow unknown() {
            return new QuotaWindow(false, null, null, null);
        }
    }

    public record SessionSnapshot(
            String id,
            String name,
            String preview,
            String cwd,
            String status,
            List<String> activeFlags,
            String currentTurnId,
            String lastItemType,
            String lastMessage,
            long updatedAt,
            boolean pendingApproval,
            boolean needsFeedback) {
    }

    public record StateSnapshot(
            long revision,
            QuotaWindow quota5h,
            QuotaWindow quota7d,
            int totalSessionCount,
            List<SessionSnapshot> sessions) {
    }

    private static final long FIVE_HOURS_MINUTES = 300;
    private static final long SEVEN_DAYS_MINUTES = 10_080;
    private static final int DEVICE_SESSION_LIMIT = 5;

    private final Map<String, MutableSession> sessions = new LinkedHashMap<>();
    private final AtomicLong revision = new AtomicLong();
    private JsonNode rateLimits = NullNode.getInstance();

    public synchronized void mergeThreadList(JsonNode result) {
        JsonNode data = result.path("data");
        if (!data.isArray()) {
            return;
        }
        for (JsonNode thread : data) {
            upsertThread(thread);
        }
        revision.incrementAndGet();
    }

    public synchronized void replaceRateLimits(JsonNode result) {
        JsonNode codexBucket = result.path("rateLimitsByLimitId").path("codex");
        JsonNode value = codexBucket.isObject() ? codexBucket : result.path("rateLimits");
        this.rateLimits = value.isMissingNode() ? NullNode.getInstance() : value.deepCopy();
        revision.incrementAndGet();
    }

    public synchronized void onNotification(String method, JsonNode params) {
        switch (method) {
            case "thread/started" -> upsertThread(params.path("thread"));
            case "thread/status/changed" -> {
                MutableSession session = session(params.path("threadId").asText());
                session.applyStatus(params.path("status"));
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "thread/name/updated" -> {
                MutableSession session = session(params.path("threadId").asText());
                session.name = nullableText(params, "name");
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "thread/closed" -> {
                MutableSession session = session(params.path("threadId").asText());
                session.status = "idle";
                session.activeFlags = List.of();
                session.currentTurnId = null;
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "thread/deleted" -> sessions.remove(params.path("threadId").asText());
            case "turn/started" -> {
                MutableSession session = session(params.path("threadId").asText());
                session.currentTurnId = nullableText(params.path("turn"), "id");
                session.status = "active";
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "turn/completed" -> {
                MutableSession session = session(params.path("threadId").asText());
                session.currentTurnId = null;
                String turnStatus = nullableText(params.path("turn"), "status");
                session.status = "failed".equals(turnStatus) ? "systemError" : "idle";
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "item/started", "item/completed" -> {
                MutableSession session = session(params.path("threadId").asText());
                JsonNode item = params.path("item");
                session.lastItemType = nullableText(item, "type");
                session.lastMessage = summarizeItem(item);
                session.updatedAt = Instant.now().getEpochSecond();
            }
            case "account/rateLimits/updated" -> {
                JsonNode value = params.path("rateLimits");
                if (!value.isMissingNode()) {
                    rateLimits = sparseMerge(rateLimits, value);
                }
            }
            default -> {
                // Other thread/turn/item deltas are deliberately ignored by the compact device model.
            }
        }
        revision.incrementAndGet();
    }

    public synchronized StateSnapshot snapshot(
            List<String> pendingThreadIds,
            List<String> feedbackThreadIds) {
        Set<String> pendingSet = new LinkedHashSet<>(pendingThreadIds);
        Set<String> feedbackSet = new LinkedHashSet<>(feedbackThreadIds);
        Set<String> prioritySet = new LinkedHashSet<>(pendingSet);
        prioritySet.addAll(feedbackSet);
        List<MutableSession> selected = new ArrayList<>();

        for (String threadId : prioritySet) {
            MutableSession session = sessions.get(threadId);
            if (session != null && selected.size() < DEVICE_SESSION_LIMIT) {
                selected.add(session);
            }
        }

        sessions.values().stream()
                .sorted(Comparator.comparingLong((MutableSession value) -> value.updatedAt).reversed())
                .filter(value -> !containsSession(selected, value.id))
                .limit(DEVICE_SESSION_LIMIT - selected.size())
                .forEach(selected::add);

        return new StateSnapshot(
                revision.get(),
                quotaForDuration(FIVE_HOURS_MINUTES),
                quotaForDuration(SEVEN_DAYS_MINUTES),
                sessions.size(),
                selected.stream().map(value -> value.snapshot(
                        pendingSet.contains(value.id),
                        feedbackSet.contains(value.id))).toList());
    }

    public synchronized Optional<String> currentTurnId(String threadId) {
        MutableSession session = sessions.get(threadId);
        return session == null ? Optional.empty() : Optional.ofNullable(session.currentTurnId);
    }

    public synchronized Optional<SessionSnapshot> findSession(String threadId) {
        MutableSession session = sessions.get(threadId);
        return session == null ? Optional.empty() : Optional.of(session.snapshot(false, false));
    }

    private void upsertThread(JsonNode thread) {
        String id = nullableText(thread, "id");
        if (id == null || id.isBlank()) {
            return;
        }
        MutableSession current = sessions.computeIfAbsent(id, MutableSession::new);
        current.name = nullableText(thread, "name");
        current.preview = nullableText(thread, "preview");
        current.cwd = nullableText(thread, "cwd");
        current.updatedAt = thread.path("updatedAt").asLong(Instant.now().getEpochSecond());
        current.applyStatus(thread.path("status"));
        JsonNode turns = thread.path("turns");
        if (turns.isArray() && turns.size() > 0) {
            JsonNode latest = turns.get(turns.size() - 1);
            String turnStatus = nullableText(latest, "status");
            current.currentTurnId = "inProgress".equals(turnStatus)
                    ? nullableText(latest, "id")
                    : current.currentTurnId;
        }
    }

    private MutableSession session(String id) {
        if (id == null || id.isBlank()) {
            throw new IllegalArgumentException("Codex notification is missing threadId");
        }
        return sessions.computeIfAbsent(id, MutableSession::new);
    }

    private QuotaWindow quotaForDuration(long durationMinutes) {
        for (JsonNode candidate : quotaCandidates(rateLimits)) {
            if (candidate.path("windowDurationMins").asLong(-1) == durationMinutes) {
                int used = clamp(candidate.path("usedPercent").asInt(0), 0, 100);
                Long resetsAt = candidate.path("resetsAt").isIntegralNumber()
                        ? candidate.path("resetsAt").asLong()
                        : null;
                return new QuotaWindow(true, used, 100 - used, resetLabel(resetsAt));
            }
        }
        return QuotaWindow.unknown();
    }

    private static Collection<JsonNode> quotaCandidates(JsonNode rateLimits) {
        List<JsonNode> candidates = new ArrayList<>();
        addWindows(candidates, rateLimits);
        if (rateLimits.isObject()) {
            rateLimits.properties().forEach(entry -> {
                JsonNode value = entry.getValue();
                if (value.isObject()) {
                    addWindows(candidates, value);
                }
            });
        }
        return candidates;
    }

    private static void addWindows(List<JsonNode> target, JsonNode source) {
        JsonNode primary = source.path("primary");
        JsonNode secondary = source.path("secondary");
        if (primary.isObject()) {
            target.add(primary);
        }
        if (secondary.isObject()) {
            target.add(secondary);
        }
    }

    private static JsonNode sparseMerge(JsonNode current, JsonNode patch) {
        if (!patch.isObject()) {
            return patch.deepCopy();
        }
        ObjectNode merged = current != null && current.isObject()
                ? (ObjectNode) current.deepCopy()
                : tools.jackson.databind.node.JsonNodeFactory.instance.objectNode();
        patch.properties().forEach(entry -> {
            JsonNode existing = merged.get(entry.getKey());
            JsonNode value = entry.getValue();
            if (value.isObject()) {
                merged.set(entry.getKey(), sparseMerge(existing, value));
            } else {
                merged.set(entry.getKey(), value.deepCopy());
            }
        });
        return merged;
    }

    private static String resetLabel(Long resetsAt) {
        if (resetsAt == null) {
            return null;
        }
        long remainingSeconds = Math.max(0, resetsAt - Instant.now().getEpochSecond());
        long minutes = remainingSeconds / 60;
        if (minutes < 60) {
            return minutes + "分钟后";
        }
        long hours = minutes / 60;
        if (hours < 48) {
            return hours + "小时后";
        }
        return (hours / 24) + "天后";
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    private static boolean containsSession(List<MutableSession> values, String id) {
        return values.stream().anyMatch(value -> value.id.equals(id));
    }

    private static String summarizeItem(JsonNode item) {
        String type = nullableText(item, "type");
        if ("agentMessage".equals(type)) {
            return abbreviate(nullableText(item, "text"), 160);
        }
        if ("commandExecution".equals(type)) {
            return abbreviate(nullableText(item, "command"), 160);
        }
        if ("fileChange".equals(type)) {
            int count = item.path("changes").isArray() ? item.path("changes").size() : 0;
            return count + " 个文件变更";
        }
        if ("userMessage".equals(type)) {
            JsonNode content = item.path("content");
            if (content.isArray()) {
                for (JsonNode input : content) {
                    if ("text".equals(nullableText(input, "type"))) {
                        return abbreviate(nullableText(input, "text"), 160);
                    }
                }
            }
        }
        return type;
    }

    private static String abbreviate(String value, int maxLength) {
        if (value == null || value.length() <= maxLength) {
            return value;
        }
        return value.substring(0, maxLength - 1) + "…";
    }

    private static String nullableText(JsonNode node, String field) {
        JsonNode value = node.path(field);
        return value.isTextual() ? value.asText() : null;
    }

    private static final class MutableSession {
        private final String id;
        private String name;
        private String preview;
        private String cwd;
        private String status = "notLoaded";
        private List<String> activeFlags = List.of();
        private String currentTurnId;
        private String lastItemType;
        private String lastMessage;
        private long updatedAt = Instant.now().getEpochSecond();

        private MutableSession(String id) {
            this.id = id;
        }

        void applyStatus(JsonNode statusNode) {
            if (statusNode.isTextual()) {
                status = statusNode.asText();
                activeFlags = List.of();
                return;
            }
            String type = nullableText(statusNode, "type");
            if (type != null) {
                status = type;
            }
            JsonNode flags = statusNode.path("activeFlags");
            if (flags.isArray()) {
                List<String> values = new ArrayList<>();
                flags.forEach(flag -> {
                    if (flag.isTextual()) {
                        values.add(flag.asText());
                    }
                });
                activeFlags = List.copyOf(values);
            } else {
                activeFlags = List.of();
            }
        }

        SessionSnapshot snapshot(boolean pendingApproval, boolean needsFeedback) {
            return new SessionSnapshot(
                    id,
                    name,
                    preview,
                    cwd,
                    status,
                    activeFlags,
                    currentTurnId,
                    lastItemType,
                    lastMessage,
                    updatedAt,
                    pendingApproval,
                    needsFeedback);
        }
    }
}
