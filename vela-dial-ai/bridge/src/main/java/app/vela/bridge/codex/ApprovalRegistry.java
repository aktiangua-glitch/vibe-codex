package app.vela.bridge.codex;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Component
public class ApprovalRegistry {

    public enum ApprovalKind {
        COMMAND_EXECUTION,
        FILE_CHANGE
    }

    public record ApprovalSnapshot(
            String id,
            ApprovalKind kind,
            String threadId,
            String turnId,
            String itemId,
            String title,
            String summary,
            String cwd,
            String reason,
            String nonce,
            String actionDigest,
            List<String> availableDecisions,
            Instant createdAt,
            Instant expiresAt) {
    }

    public record ApprovalQueueSnapshot(
            int pendingCount,
            ApprovalSnapshot currentApproval) {
    }

    record PendingApproval(
            String deviceId,
            JsonNode codexRequestId,
            ApprovalKind kind,
            String threadId,
            String turnId,
            String itemId,
            String title,
            String summary,
            String cwd,
            String reason,
            String nonce,
            String actionDigest,
            List<String> availableDecisions,
            long sequence,
            Instant createdAt,
            Instant expiresAt) {

        ApprovalSnapshot snapshot() {
            return new ApprovalSnapshot(
                    deviceId,
                    kind,
                    threadId,
                    turnId,
                    itemId,
                    title,
                    summary,
                    cwd,
                    reason,
                    nonce,
                    actionDigest,
                    availableDecisions,
                    createdAt,
                    expiresAt);
        }
    }

    public enum ResolutionState {
        NEW,
        SENT,
        CONFIRMED
    }

    public record ResolutionAttempt(
            PendingApproval approval,
            String decision,
            String wireDecision,
            ResolutionState state) {

        boolean isNew() {
            return state == ResolutionState.NEW;
        }
    }

    private static final int COMPLETED_LIMIT = 256;
    private static final Base64.Encoder BASE64_URL = Base64.getUrlEncoder().withoutPadding();

    private final byte[] signingKey;
    private final java.time.Duration approvalTtl;
    private final SecureRandom secureRandom = new SecureRandom();
    private final Map<String, PendingApproval> pending = new LinkedHashMap<>();
    private final Map<String, String> inFlight = new LinkedHashMap<>();
    private final Map<String, PendingApproval> sent = new LinkedHashMap<>();
    private final Map<String, String> sentDecisions = new LinkedHashMap<>();
    private final LinkedHashMap<String, String> completed =
            new LinkedHashMap<>(COMPLETED_LIMIT + 1, 0.75f, true);
    private long sequence;

    public ApprovalRegistry(BridgeProperties properties) {
        this.signingKey = properties.getSecurity().getBearerToken().getBytes(StandardCharsets.UTF_8);
        this.approvalTtl = properties.getCodex().getApprovalTtl();
    }

    public synchronized ApprovalSnapshot capture(String method, JsonNode requestId, JsonNode params) {
        ApprovalKind kind = switch (method) {
            case "item/commandExecution/requestApproval" -> ApprovalKind.COMMAND_EXECUTION;
            case "item/fileChange/requestApproval" -> ApprovalKind.FILE_CHANGE;
            default -> throw new IllegalArgumentException("Unsupported approval method: " + method);
        };

        byte[] nonceBytes = new byte[18];
        secureRandom.nextBytes(nonceBytes);
        String nonce = BASE64_URL.encodeToString(nonceBytes);
        String deviceId = signedOpaqueToken(nonceBytes);
        String command = textOrNull(params, "command");
        String reason = textOrNull(params, "reason");
        String cwd = textOrNull(params, "cwd");
        String title = kind == ApprovalKind.COMMAND_EXECUTION ? "运行命令" : "修改文件";
        String summary = firstNonBlank(command, reason, kind == ApprovalKind.COMMAND_EXECUTION
                ? "Codex 请求执行命令"
                : "Codex 请求写入文件");

        List<String> decisions = new ArrayList<>();
        JsonNode available = params.path("availableDecisions");
        if (available.isArray()) {
            available.forEach(value -> {
                if (value.isTextual()) {
                    decisions.add(value.asText());
                }
            });
        }
        if (decisions.isEmpty()) {
            decisions.add("accept");
            decisions.add("decline");
        }

        Instant createdAt = Instant.now();
        String actionDigest = actionDigest(
                kind, requiredText(params, "threadId"), requiredText(params, "turnId"),
                requiredText(params, "itemId"), summary, cwd, reason);
        PendingApproval approval = new PendingApproval(
                deviceId,
                requestId.deepCopy(),
                kind,
                requiredText(params, "threadId"),
                requiredText(params, "turnId"),
                requiredText(params, "itemId"),
                title,
                summary,
                cwd,
                reason,
                nonce,
                actionDigest,
                List.copyOf(decisions),
                ++sequence,
                createdAt,
                createdAt.plus(approvalTtl));
        pending.put(deviceId, approval);
        return approval.snapshot();
    }

    public synchronized ApprovalQueueSnapshot queueSnapshot() {
        ApprovalSnapshot head = pending.values().stream()
                .min(Comparator.comparingLong(PendingApproval::sequence))
                .map(PendingApproval::snapshot)
                .orElse(null);
        return new ApprovalQueueSnapshot(pending.size(), head);
    }

    public synchronized List<String> pendingThreadIds() {
        return pending.values().stream()
                .sorted(Comparator.comparingLong(PendingApproval::sequence))
                .map(PendingApproval::threadId)
                .distinct()
                .toList();
    }

    public synchronized Optional<ApprovalSnapshot> find(String deviceId) {
        return Optional.ofNullable(pending.get(deviceId)).map(PendingApproval::snapshot);
    }

    public synchronized ResolutionAttempt beginResolution(
            String deviceId,
            String rawDecision,
            String nonce,
            String actionDigest,
            boolean requireBinding) {
        String decision = normalizeDecision(rawDecision);
        String completedDecision = completed.get(deviceId);
        if (completedDecision != null) {
            if (!completedDecision.equals(decision)) {
                throw new BridgeApiException(HttpStatus.CONFLICT, "approval_already_resolved",
                        "Approval was already resolved with a different decision");
            }
            return new ResolutionAttempt(
                    null,
                    decision,
                    decision,
                    ResolutionState.CONFIRMED);
        }

        String sentDecision = sentDecisions.get(deviceId);
        if (sentDecision != null) {
            if (!sentDecision.equals(decision)) {
                throw new BridgeApiException(HttpStatus.CONFLICT, "approval_already_resolved",
                        "Approval response was already sent with a different decision");
            }
            PendingApproval sentApproval = sent.get(deviceId);
            return new ResolutionAttempt(
                    sentApproval,
                    decision,
                    wireDecision(sentApproval, decision),
                    ResolutionState.SENT);
        }
        if (inFlight.containsKey(deviceId)) {
            throw new BridgeApiException(HttpStatus.CONFLICT, "approval_resolution_in_progress",
                    "Approval resolution is being written to Codex");
        }

        PendingApproval approval = pending.get(deviceId);
        if (approval == null) {
            throw new BridgeApiException(HttpStatus.NOT_FOUND, "approval_not_found",
                    "Approval is missing, expired, or was resolved elsewhere");
        }
        String wireDecision = wireDecision(approval, decision);
        if (requireBinding
                && (!MessageDigest.isEqual(
                        approval.nonce().getBytes(StandardCharsets.UTF_8),
                        nullToEmpty(nonce).getBytes(StandardCharsets.UTF_8))
                || !MessageDigest.isEqual(
                        approval.actionDigest().getBytes(StandardCharsets.UTF_8),
                        nullToEmpty(actionDigest).getBytes(StandardCharsets.UTF_8)))) {
            throw new BridgeApiException(
                    HttpStatus.CONFLICT,
                    "approval_binding_mismatch",
                    "Approval nonce or actionDigest no longer matches the queued action");
        }
        inFlight.put(deviceId, decision);
        return new ResolutionAttempt(
                approval,
                decision,
                wireDecision,
                ResolutionState.NEW);
    }

    public synchronized void markSent(ResolutionAttempt attempt) {
        if (!attempt.isNew()) {
            return;
        }
        String id = attempt.approval().deviceId();
        pending.remove(id);
        inFlight.remove(id);
        sent.put(id, attempt.approval());
        sentDecisions.put(id, attempt.decision());
    }

    private void rememberCompleted(String id, String decision) {
        completed.put(id, decision);
        while (completed.size() > COMPLETED_LIMIT) {
            String oldest = completed.keySet().iterator().next();
            completed.remove(oldest);
        }
    }

    public synchronized void rollbackResolution(ResolutionAttempt attempt) {
        if (attempt.isNew() && attempt.approval() != null) {
            inFlight.remove(attempt.approval().deviceId());
        }
    }

    public synchronized void dismissByCodexRequestId(JsonNode requestId) {
        String matchingPending = pending.values().stream()
                .filter(value -> value.codexRequestId().equals(requestId))
                .map(PendingApproval::deviceId)
                .findFirst()
                .orElse(null);
        if (matchingPending != null) {
            pending.remove(matchingPending);
            inFlight.remove(matchingPending);
            return;
        }
        String matchingSent = sent.values().stream()
                .filter(value -> value.codexRequestId().equals(requestId))
                .map(PendingApproval::deviceId)
                .findFirst()
                .orElse(null);
        if (matchingSent != null) {
            sent.remove(matchingSent);
            String decision = sentDecisions.remove(matchingSent);
            rememberCompleted(matchingSent, decision);
        }
    }

    public synchronized List<String> expiredPendingIds(Instant now) {
        return pending.values().stream()
                .filter(value -> !value.expiresAt().isAfter(now))
                .map(PendingApproval::deviceId)
                .toList();
    }

    public synchronized void clearPending() {
        pending.clear();
        inFlight.clear();
        sent.clear();
        sentDecisions.clear();
    }

    private String signedOpaqueToken(byte[] nonce) {
        byte[] signature;
        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(signingKey, "HmacSHA256"));
            signature = mac.doFinal(nonce);
        } catch (GeneralSecurityException exception) {
            throw new IllegalStateException("HMAC-SHA256 is unavailable", exception);
        }
        byte[] truncated = new byte[16];
        System.arraycopy(signature, 0, truncated, 0, truncated.length);
        return "apv_" + BASE64_URL.encodeToString(nonce) + "." + BASE64_URL.encodeToString(truncated);
    }

    private static String actionDigest(
            ApprovalKind kind,
            String threadId,
            String turnId,
            String itemId,
            String summary,
            String cwd,
            String reason) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            String canonical = String.join("\u0000",
                    kind.name(),
                    nullToEmpty(threadId),
                    nullToEmpty(turnId),
                    nullToEmpty(itemId),
                    nullToEmpty(summary),
                    nullToEmpty(cwd),
                    nullToEmpty(reason));
            return java.util.HexFormat.of().formatHex(
                    digest.digest(canonical.getBytes(StandardCharsets.UTF_8)));
        } catch (GeneralSecurityException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        }
    }

    private static String normalizeDecision(String decision) {
        if (decision == null) {
            throw new BridgeApiException(HttpStatus.BAD_REQUEST, "invalid_decision",
                    "Decision must be accept or decline");
        }
        String normalized = decision.trim().toLowerCase();
        normalized = switch (normalized) {
            case "allow" -> "accept";
            case "reject" -> "decline";
            default -> normalized;
        };
        if (!normalized.equals("accept") && !normalized.equals("decline")) {
            throw new BridgeApiException(HttpStatus.BAD_REQUEST, "invalid_decision",
                    "Decision must be accept or decline");
        }
        return normalized;
    }

    private static String wireDecision(
            PendingApproval approval,
            String semanticDecision) {
        if (approval == null) {
            return semanticDecision;
        }
        List<String> available = approval.availableDecisions();
        if ("accept".equals(semanticDecision)) {
            // A one-shot device approval must never silently become a
            // session-wide grant.
            if (available.contains("accept")) {
                return "accept";
            }
        } else if ("decline".equals(semanticDecision)) {
            if (available.contains("decline")) {
                return "decline";
            }
            // Some Codex approval requests expose cancel rather than decline.
            // Both safely deny the pending action, while the device-facing
            // semantic remains "decline" for idempotency and feedback.
            if (available.contains("cancel")) {
                return "cancel";
            }
        }
        throw new BridgeApiException(
                HttpStatus.CONFLICT,
                "approval_decision_unavailable",
                "Codex did not offer a safe wire decision for "
                        + semanticDecision);
    }

    private static String requiredText(JsonNode node, String field) {
        String value = textOrNull(node, field);
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("Codex approval is missing " + field);
        }
        return value;
    }

    private static String textOrNull(JsonNode node, String field) {
        JsonNode value = node.path(field);
        return value.isTextual() ? value.asText() : null;
    }

    private static String firstNonBlank(String... values) {
        for (String value : values) {
            if (value != null && !value.isBlank()) {
                return value;
            }
        }
        return "";
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }
}
