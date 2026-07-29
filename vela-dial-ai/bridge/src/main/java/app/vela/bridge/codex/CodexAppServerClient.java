package app.vela.bridge.codex;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.audio.FeedbackRegistry;
import app.vela.bridge.config.BridgeProperties;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

@Component
public class CodexAppServerClient implements AutoCloseable {

    public enum ConnectionState {
        DISABLED,
        STARTING,
        CONNECTED,
        DISCONNECTED,
        FAILED
    }

    public record StatusSnapshot(
            boolean enabled,
            boolean connected,
            ConnectionState state,
            Long processId,
            String userAgent,
            Instant connectedAt,
            String lastError) {
    }

    public record ApprovalResolution(
            String id,
            String decision,
            boolean alreadyResolved,
            boolean codexConfirmed,
            Instant resolvedAt) {
    }

    private record ThreadHydrationStamp(
            long updatedAt,
            long retryAfterEpochMillis,
            boolean hydrated) {
    }

    private static final Logger log = LoggerFactory.getLogger(CodexAppServerClient.class);
    private static final Duration THREAD_HYDRATION_RETRY_DELAY = Duration.ofMinutes(1);

    private final BridgeProperties properties;
    private final JsonMapper jsonMapper;
    private final CodexStateStore stateStore;
    private final ApprovalRegistry approvalRegistry;
    private final FeedbackRegistry feedbackRegistry;
    private final AtomicLong nextRequestId = new AtomicLong(1);
    private final ConcurrentHashMap<String, CompletableFuture<JsonNode>> pendingRequests =
            new ConcurrentHashMap<>();
    private final Object writerLock = new Object();
    private final AtomicBoolean processStarting = new AtomicBoolean();
    private final AtomicBoolean threadRefreshInFlight = new AtomicBoolean();
    private final ConcurrentHashMap<String, ThreadHydrationStamp> threadHydrations =
            new ConcurrentHashMap<>();
    private final ExecutorService ioExecutor = Executors.newVirtualThreadPerTaskExecutor();
    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor(
            runnable -> Thread.ofPlatform()
                    .daemon(true)
                    .name("vela-codex-scheduler")
                    .unstarted(runnable));

    private volatile Process process;
    private volatile BufferedWriter writer;
    private volatile ConnectionState connectionState = ConnectionState.DISCONNECTED;
    private volatile String lastError;
    private volatile String userAgent;
    private volatile Instant connectedAt;
    private volatile boolean closing;

    public CodexAppServerClient(
            BridgeProperties properties,
            JsonMapper jsonMapper,
            CodexStateStore stateStore,
            ApprovalRegistry approvalRegistry,
            FeedbackRegistry feedbackRegistry) {
        this.properties = properties;
        this.jsonMapper = jsonMapper;
        this.stateStore = stateStore;
        this.approvalRegistry = approvalRegistry;
        this.feedbackRegistry = feedbackRegistry;
    }

    @PostConstruct
    void start() {
        if (!properties.getCodex().isEnabled()) {
            connectionState = ConnectionState.DISABLED;
            return;
        }
        ioExecutor.submit(this::startProcess);
        scheduler.scheduleWithFixedDelay(
                this::refreshThreadsSafely,
                properties.getCodex().getThreadRefreshInterval().toMillis(),
                properties.getCodex().getThreadRefreshInterval().toMillis(),
                TimeUnit.MILLISECONDS);
        scheduler.scheduleWithFixedDelay(
                this::refreshRateLimitsSafely,
                properties.getCodex().getRateLimitRefreshInterval().toMillis(),
                properties.getCodex().getRateLimitRefreshInterval().toMillis(),
                TimeUnit.MILLISECONDS);
        scheduler.scheduleWithFixedDelay(this::expireApprovalsSafely, 1, 1, TimeUnit.SECONDS);
    }

    public StatusSnapshot status() {
        Process current = process;
        return new StatusSnapshot(
                properties.getCodex().isEnabled(),
                connectionState == ConnectionState.CONNECTED,
                connectionState,
                current != null && current.isAlive() ? current.pid() : null,
                userAgent,
                connectedAt,
                lastError);
    }

    public CompletableFuture<JsonNode> readRateLimits() {
        return request("account/rateLimits/read", null).thenApply(result -> {
            stateStore.replaceRateLimits(result);
            return result;
        });
    }

    public CompletableFuture<JsonNode> readAccountUsage() {
        return request("account/usage/read", null).thenApply(result -> {
            stateStore.replaceAccountUsage(result);
            return result;
        });
    }

    public CompletableFuture<JsonNode> listThreads(int limit, String cursor) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("limit", limit);
        params.put("sortKey", "updated_at");
        params.put("sortDirection", "desc");
        if (cursor != null && !cursor.isBlank()) {
            params.put("cursor", cursor);
        }
        return request("thread/list", params);
    }

    public CompletableFuture<JsonNode> readThread(String threadId) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", requireNonBlank(threadId, "threadId"));
        params.put("includeTurns", true);
        return request("thread/read", params);
    }

    public CompletableFuture<JsonNode> startThread(String cwd) {
        ObjectNode params = jsonMapper.createObjectNode();
        String effectiveCwd = firstNonBlank(cwd, properties.getCodex().getDefaultCwd());
        if (effectiveCwd != null) {
            params.put("cwd", Path.of(effectiveCwd).toAbsolutePath().normalize().toString());
        }
        params.put("approvalPolicy", "on-request");
        params.put("approvalsReviewer", "user");
        return request("thread/start", params).thenApply(result -> {
            JsonNode thread = result.path("thread");
            if (thread.isObject()) {
                ObjectNode notification = jsonMapper.createObjectNode();
                notification.set("thread", thread);
                stateStore.onNotification("thread/started", notification);
            }
            return result;
        });
    }

    public CompletableFuture<JsonNode> resumeThread(String threadId) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", requireNonBlank(threadId, "threadId"));
        params.put("approvalPolicy", "on-request");
        params.put("approvalsReviewer", "user");
        return request("thread/resume", params).thenApply(result -> {
            JsonNode thread = result.path("thread");
            if (thread.isObject()) {
                ObjectNode notification = jsonMapper.createObjectNode();
                notification.set("thread", thread);
                stateStore.onNotification("thread/started", notification);
            }
            return result;
        });
    }

    public CompletableFuture<JsonNode> startTurn(
            String threadId,
            ArrayNode input,
            String clientUserMessageId) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", requireNonBlank(threadId, "threadId"));
        params.set("input", Objects.requireNonNull(input, "input"));
        if (clientUserMessageId != null && !clientUserMessageId.isBlank()) {
            params.put("clientUserMessageId", clientUserMessageId);
        }
        params.put("approvalPolicy", "on-request");
        params.put("approvalsReviewer", "user");
        return request("turn/start", params);
    }

    public CompletableFuture<JsonNode> steerTurn(
            String threadId,
            String expectedTurnId,
            ArrayNode input,
            String clientUserMessageId) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", requireNonBlank(threadId, "threadId"));
        params.put("expectedTurnId", requireNonBlank(expectedTurnId, "expectedTurnId"));
        params.set("input", Objects.requireNonNull(input, "input"));
        if (clientUserMessageId != null && !clientUserMessageId.isBlank()) {
            params.put("clientUserMessageId", clientUserMessageId);
        }
        return request("turn/steer", params);
    }

    public ApprovalResolution resolveApproval(
            String deviceApprovalId,
            String decision,
            String nonce,
            String actionDigest) {
        return resolveApproval(deviceApprovalId, decision, nonce, actionDigest, true);
    }

    private ApprovalResolution resolveApproval(
            String deviceApprovalId,
            String decision,
            String nonce,
            String actionDigest,
            boolean captureFeedback) {
        requireConnected();
        ApprovalRegistry.ResolutionAttempt attempt =
                approvalRegistry.beginResolution(
                        deviceApprovalId,
                        decision,
                        nonce,
                        actionDigest,
                        captureFeedback);
        if (attempt.state() == ApprovalRegistry.ResolutionState.CONFIRMED) {
            return new ApprovalResolution(
                    deviceApprovalId, attempt.decision(), true, true, Instant.now());
        }
        if (attempt.state() == ApprovalRegistry.ResolutionState.SENT) {
            return new ApprovalResolution(
                    deviceApprovalId, attempt.decision(), true, false, Instant.now());
        }

        ObjectNode response = jsonMapper.createObjectNode();
        response.set("id", attempt.approval().codexRequestId());
        ObjectNode result = response.putObject("result");
        result.put("decision", attempt.wireDecision());

        boolean feedbackPersisted = false;
        try {
            if (captureFeedback && "decline".equals(attempt.decision())) {
                feedbackRegistry.markNeedsFeedback(
                        attempt.approval().deviceId(),
                        attempt.approval().threadId());
                feedbackPersisted = true;
            }
            send(response);
            approvalRegistry.markSent(attempt);
            return new ApprovalResolution(
                    deviceApprovalId, attempt.decision(), false, false, Instant.now());
        } catch (RuntimeException exception) {
            if (feedbackPersisted) {
                try {
                    feedbackRegistry.complete(attempt.approval().deviceId());
                } catch (RuntimeException rollbackFailure) {
                    exception.addSuppressed(rollbackFailure);
                }
            }
            approvalRegistry.rollbackResolution(attempt);
            throw exception;
        }
    }

    public CompletableFuture<JsonNode> request(String method, JsonNode params) {
        if (!properties.getCodex().isEnabled()) {
            return CompletableFuture.failedFuture(new BridgeApiException(
                    HttpStatus.SERVICE_UNAVAILABLE, "codex_disabled", "Codex integration is disabled"));
        }
        if (writer == null) {
            return CompletableFuture.failedFuture(new BridgeApiException(
                    HttpStatus.SERVICE_UNAVAILABLE, "codex_disconnected", "Codex app-server is not connected"));
        }

        long id = nextRequestId.getAndIncrement();
        ObjectNode message = jsonMapper.createObjectNode();
        message.put("id", id);
        message.put("method", method);
        if (params != null) {
            message.set("params", params);
        }

        CompletableFuture<JsonNode> future = new CompletableFuture<>();
        pendingRequests.put(requestKey(id), future);
        try {
            send(message);
        } catch (RuntimeException exception) {
            pendingRequests.remove(requestKey(id));
            future.completeExceptionally(exception);
        }
        String requestKey = requestKey(id);
        scheduler.schedule(() -> {
            if (pendingRequests.remove(requestKey, future)) {
                future.completeExceptionally(new CodexRequestTimeoutException(
                        method,
                        isMutation(method)));
            }
        }, properties.getCodex().getRequestTimeout().toMillis(), TimeUnit.MILLISECONDS);
        future.whenComplete((result, error) -> pendingRequests.remove(requestKey, future));
        return future;
    }

    private void startProcess() {
        if (closing || !processStarting.compareAndSet(false, true)) {
            return;
        }
        try {
            Process existing = process;
            if (existing != null && existing.isAlive()) {
                return;
            }
            connectionState = ConnectionState.STARTING;
            List<String> command = new ArrayList<>();
            command.add(resolveCodexExecutable());
            command.addAll(properties.getCodex().getArguments());
            ProcessBuilder builder = new ProcessBuilder(command);
            String configuredCwd = properties.getCodex().getDefaultCwd();
            if (configuredCwd != null && !configuredCwd.isBlank()) {
                Path cwd = Path.of(configuredCwd).toAbsolutePath().normalize();
                if (Files.isDirectory(cwd)) {
                    builder.directory(cwd.toFile());
                }
            }

            Process started = builder.start();
            process = started;
            writer = new BufferedWriter(
                    new OutputStreamWriter(started.getOutputStream(), StandardCharsets.UTF_8));
            ioExecutor.submit(() -> readStderr(started));
            ioExecutor.submit(() -> readStdout(started));
            initializeProtocol();
        } catch (Exception exception) {
            connectionState = ConnectionState.FAILED;
            lastError = safeMessage(exception);
            log.warn("Unable to start Codex app-server: {}", lastError);
            scheduleRestart();
        } finally {
            processStarting.set(false);
        }
    }

    private void initializeProtocol() {
        ObjectNode params = jsonMapper.createObjectNode();
        ObjectNode clientInfo = params.putObject("clientInfo");
        clientInfo.put("name", "vela-dial-bridge");
        clientInfo.put("title", "Vela Dial Bridge");
        clientInfo.put("version", "0.1.0");
        ObjectNode capabilities = params.putObject("capabilities");
        capabilities.put("experimentalApi", false);
        capabilities.put("requestAttestation", false);

        request("initialize", params)
                .orTimeout(
                        properties.getCodex().getInitializeTimeout().toMillis(),
                        TimeUnit.MILLISECONDS)
                .thenAccept(result -> {
                    userAgent = textOrNull(result, "userAgent");
                    connectedAt = Instant.now();
                    connectionState = ConnectionState.CONNECTED;
                    lastError = null;
                    ObjectNode initialized = jsonMapper.createObjectNode();
                    initialized.put("method", "initialized");
                    send(initialized);
                    refreshThreadsSafely();
                    refreshRateLimitsSafely();
                })
                .exceptionally(exception -> {
                    lastError = safeMessage(exception);
                    connectionState = ConnectionState.FAILED;
                    Process current = process;
                    if (current != null) {
                        current.destroy();
                    }
                    return null;
                });
    }

    private void readStdout(Process source) {
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(source.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isBlank()) {
                    continue;
                }
                try {
                    handleInbound(jsonMapper.readTree(line));
                } catch (Exception exception) {
                    log.warn("Ignoring malformed Codex app-server frame: {}", safeMessage(exception));
                }
            }
        } catch (IOException exception) {
            if (!closing) {
                log.warn("Codex app-server stdout closed: {}", safeMessage(exception));
            }
        } finally {
            onProcessEnded(source);
        }
    }

    private void readStderr(Process source) {
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(source.getErrorStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (!line.isBlank()) {
                    log.debug("codex app-server: {}", line);
                }
            }
        } catch (IOException exception) {
            if (!closing) {
                log.debug("Codex app-server stderr closed: {}", safeMessage(exception));
            }
        }
    }

    void handleInbound(JsonNode message) {
        JsonNode id = message.get("id");
        JsonNode methodNode = message.get("method");

        if (id != null && methodNode == null) {
            CompletableFuture<JsonNode> future = pendingRequests.remove(requestKey(id));
            if (future == null) {
                return;
            }
            JsonNode error = message.get("error");
            if (error != null && !error.isNull()) {
                future.completeExceptionally(new CodexRpcException(error));
            } else {
                JsonNode result = message.get("result");
                future.complete(result == null ? jsonMapper.createObjectNode() : result);
            }
            return;
        }

        if (methodNode == null || !methodNode.isTextual()) {
            return;
        }
        String method = methodNode.asText();
        JsonNode params = Optional.ofNullable(message.get("params"))
                .orElseGet(jsonMapper::createObjectNode);

        if (id != null) {
            handleServerRequest(id, method, params);
        } else {
            handleNotification(method, params);
        }
    }

    private void handleServerRequest(JsonNode requestId, String method, JsonNode params) {
        if (method.equals("item/commandExecution/requestApproval")
                || method.equals("item/fileChange/requestApproval")) {
            try {
                approvalRegistry.capture(method, requestId, params);
            } catch (RuntimeException exception) {
                log.warn("Rejecting malformed Codex approval request: {}", safeMessage(exception));
                ObjectNode response = jsonMapper.createObjectNode();
                response.set("id", requestId.deepCopy());
                response.putObject("result").put("decision", "decline");
                send(response);
            }
            return;
        }

        ObjectNode response = jsonMapper.createObjectNode();
        response.set("id", requestId.deepCopy());
        ObjectNode error = response.putObject("error");
        error.put("code", -32601);
        error.put("message", "Vela Bridge does not implement server request " + method);
        send(response);
    }

    private void handleNotification(String method, JsonNode params) {
        if (method.equals("serverRequest/resolved")) {
            JsonNode requestId = params.get("requestId");
            if (requestId != null) {
                approvalRegistry.dismissByCodexRequestId(requestId);
            }
            return;
        }
        if (method.startsWith("thread/")
                || method.startsWith("turn/")
                || method.startsWith("item/")
                || method.equals("account/rateLimits/updated")) {
            stateStore.onNotification(method, params);
        }
    }

    private void refreshThreadsSafely() {
        if (connectionState != ConnectionState.CONNECTED
                || !threadRefreshInFlight.compareAndSet(false, true)) {
            return;
        }
        fetchThreadPage(null, 0)
                .thenCompose(ignored -> hydrateDeviceThreads())
                .whenComplete((ignored, exception) -> {
                    threadRefreshInFlight.set(false);
                    if (exception != null) {
                        log.debug("Unable to refresh Codex threads: {}", safeMessage(exception));
                    }
                });
    }

    private CompletableFuture<Void> fetchThreadPage(String cursor, int page) {
        if (page >= 10) {
            return CompletableFuture.completedFuture(null);
        }
        return listThreads(properties.getCodex().getThreadPageSize(), cursor).thenCompose(result -> {
            stateStore.mergeThreadList(result);
            String next = textOrNull(result, "nextCursor");
            if (next == null || next.isBlank()) {
                return CompletableFuture.completedFuture(null);
            }
            return fetchThreadPage(next, page + 1);
        });
    }

    private CompletableFuture<Void> hydrateDeviceThreads() {
        List<CodexStateStore.ThreadHydrationCandidate> candidates =
                stateStore.hydrationCandidates(
                        approvalRegistry.pendingThreadIds(),
                        feedbackRegistry.threadIdsNeedingFeedback());
        Set<String> candidateIds = new HashSet<>();
        candidates.forEach(candidate -> candidateIds.add(candidate.id()));
        threadHydrations.keySet().removeIf(threadId -> !candidateIds.contains(threadId));

        long now = System.currentTimeMillis();
        List<CompletableFuture<Void>> reads = new ArrayList<>();
        for (CodexStateStore.ThreadHydrationCandidate candidate : candidates) {
            ThreadHydrationStamp previous = threadHydrations.get(candidate.id());
            if (previous != null
                    && previous.updatedAt() == candidate.updatedAt()
                    && (previous.hydrated() || now < previous.retryAfterEpochMillis())) {
                continue;
            }

            ThreadHydrationStamp attempted = new ThreadHydrationStamp(
                    candidate.updatedAt(),
                    now + THREAD_HYDRATION_RETRY_DELAY.toMillis(),
                    false);
            threadHydrations.put(candidate.id(), attempted);
            CompletableFuture<Void> read = readThread(candidate.id())
                    .thenAccept(result -> {
                        stateStore.mergeThreadRead(result);
                        threadHydrations.replace(
                                candidate.id(),
                                attempted,
                                new ThreadHydrationStamp(candidate.updatedAt(), Long.MAX_VALUE, true));
                    })
                    .exceptionally(exception -> {
                        log.debug(
                                "Unable to hydrate Codex thread {}: {}",
                                candidate.id(),
                                safeMessage(exception));
                        return null;
                    });
            reads.add(read);
        }
        return CompletableFuture.allOf(reads.toArray(CompletableFuture[]::new));
    }

    private void refreshRateLimitsSafely() {
        if (connectionState != ConnectionState.CONNECTED) {
            return;
        }
        readRateLimits().exceptionally(exception -> {
            log.debug("Unable to refresh Codex rate limits: {}", safeMessage(exception));
            return null;
        });
        readAccountUsage().exceptionally(exception -> {
            log.debug("Unable to refresh Codex token usage: {}", safeMessage(exception));
            return null;
        });
    }

    private void expireApprovalsSafely() {
        if (connectionState != ConnectionState.CONNECTED) {
            return;
        }
        for (String approvalId : approvalRegistry.expiredPendingIds(Instant.now())) {
            try {
                resolveApproval(approvalId, "decline", null, null, false);
                log.info("Expired approval {} was declined safely", approvalId);
            } catch (RuntimeException exception) {
                log.warn("Unable to expire approval {}: {}", approvalId, safeMessage(exception));
            }
        }
    }

    private void send(JsonNode message) {
        BufferedWriter current = writer;
        if (current == null) {
            throw new BridgeApiException(
                    HttpStatus.SERVICE_UNAVAILABLE, "codex_disconnected", "Codex app-server is not connected");
        }
        synchronized (writerLock) {
            try {
                current.write(jsonMapper.writeValueAsString(message));
                current.newLine();
                current.flush();
            } catch (IOException exception) {
                throw new BridgeApiException(
                        HttpStatus.BAD_GATEWAY,
                        "codex_write_failed",
                        "Failed to write to Codex app-server",
                        exception);
            }
        }
    }

    private void onProcessEnded(Process ended) {
        if (process != ended) {
            return;
        }
        writer = null;
        process = null;
        approvalRegistry.clearPending();
        RuntimeException disconnected = new BridgeApiException(
                HttpStatus.SERVICE_UNAVAILABLE,
                "codex_disconnected",
                "Codex app-server disconnected");
        pendingRequests.forEach((id, future) -> future.completeExceptionally(disconnected));
        pendingRequests.clear();
        if (!closing) {
            connectionState = ConnectionState.DISCONNECTED;
            lastError = "Codex app-server exited";
            scheduleRestart();
        }
    }

    private void scheduleRestart() {
        if (!closing && properties.getCodex().isEnabled()) {
            scheduler.schedule(
                    () -> ioExecutor.submit(this::startProcess),
                    properties.getCodex().getRestartDelay().toMillis(),
                    TimeUnit.MILLISECONDS);
        }
    }

    private void requireConnected() {
        if (connectionState != ConnectionState.CONNECTED || writer == null) {
            throw new BridgeApiException(
                    HttpStatus.SERVICE_UNAVAILABLE,
                    "codex_disconnected",
                    "Codex app-server is not connected");
        }
    }

    private static String requestKey(JsonNode id) {
        if (id.isTextual()) {
            return "s:" + id.asText();
        }
        return "n:" + id.toString();
    }

    private static String requestKey(long id) {
        return "n:" + id;
    }

    private static boolean isMutation(String method) {
        return method.equals("thread/start")
                || method.equals("turn/start")
                || method.equals("turn/steer");
    }

    private static String requireNonBlank(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(name + " is required");
        }
        return value;
    }

    private String resolveCodexExecutable() {
        String configured = properties.getCodex().getExecutable();
        if (configured != null && !configured.isBlank() && !"auto".equalsIgnoreCase(configured)) {
            return configured;
        }
        List<Path> appBundleCandidates = List.of(
                Path.of("/Applications/ChatGPT.app/Contents/Resources/codex"),
                Path.of("/Applications/Codex.app/Contents/Resources/codex"),
                Path.of(System.getProperty("user.home"), "Applications", "ChatGPT.app",
                        "Contents", "Resources", "codex"),
                Path.of(System.getProperty("user.home"), "Applications", "Codex.app",
                        "Contents", "Resources", "codex"));
        return appBundleCandidates.stream()
                .filter(Files::isExecutable)
                .map(Path::toString)
                .findFirst()
                .orElse("codex");
    }

    private static String firstNonBlank(String... values) {
        for (String value : values) {
            if (value != null && !value.isBlank()) {
                return value;
            }
        }
        return null;
    }

    private static String textOrNull(JsonNode node, String field) {
        JsonNode value = node.get(field);
        return value != null && value.isTextual() ? value.asText() : null;
    }

    private static String safeMessage(Throwable throwable) {
        Throwable current = throwable;
        while (current.getCause() != null
                && (current instanceof java.util.concurrent.CompletionException
                || current instanceof java.util.concurrent.ExecutionException)) {
            current = current.getCause();
        }
        if (current instanceof TimeoutException) {
            return "Codex request timed out";
        }
        return current.getMessage() == null ? current.getClass().getSimpleName() : current.getMessage();
    }

    @PreDestroy
    @Override
    public void close() {
        closing = true;
        connectionState = properties.getCodex().isEnabled()
                ? ConnectionState.DISCONNECTED
                : ConnectionState.DISABLED;
        scheduler.shutdownNow();
        Process current = process;
        if (current != null) {
            current.destroy();
        }
        ioExecutor.shutdownNow();
    }

    static final class CodexRpcException extends RuntimeException {
        CodexRpcException(JsonNode error) {
            super("Codex RPC error " + error);
        }
    }

    public static final class CodexRequestTimeoutException extends RuntimeException {
        private final String method;
        private final boolean unknownOutcome;

        CodexRequestTimeoutException(String method, boolean unknownOutcome) {
            super(unknownOutcome
                    ? "Codex " + method + " timed out; the mutation outcome is unknown"
                    : "Codex " + method + " timed out");
            this.method = method;
            this.unknownOutcome = unknownOutcome;
        }

        public String method() {
            return method;
        }

        public boolean unknownOutcome() {
            return unknownOutcome;
        }
    }
}
