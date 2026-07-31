package app.vela.bridge.config;

import jakarta.annotation.PostConstruct;
import org.springframework.boot.context.properties.ConfigurationProperties;

import java.net.URI;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

@ConfigurationProperties(prefix = "vela")
public class BridgeProperties {

    private final Security security = new Security();
    private final Codex codex = new Codex();
    private final Recordings recordings = new Recordings();
    private final Asr asr = new Asr();

    public Security getSecurity() {
        return security;
    }

    public Codex getCodex() {
        return codex;
    }

    public Recordings getRecordings() {
        return recordings;
    }

    public Asr getAsr() {
        return asr;
    }

    @PostConstruct
    void validate() {
        if (security.bearerToken == null || security.bearerToken.isBlank()) {
            throw new IllegalStateException(
                    "VELA_BRIDGE_TOKEN is required; the device API never starts without authentication");
        }
        if (security.bearerToken.length() < 32) {
            throw new IllegalStateException(
                    "VELA_BRIDGE_TOKEN must contain at least 32 characters");
        }
        if (recordings.maxBytes < 44) {
            throw new IllegalStateException("vela.recordings.max-bytes must allow at least a WAV header");
        }
        if (recordings.idempotencyCapacity < 1) {
            throw new IllegalStateException("vela.recordings.idempotency-capacity must be positive");
        }
        if (asr.provider == AsrProvider.VOLCENGINE
                && (asr.volcengine.apiKey == null || asr.volcengine.apiKey.isBlank())) {
            throw new IllegalStateException(
                    "VOLCENGINE_ASR_API_KEY is required when VELA_ASR_PROVIDER=volcengine");
        }
    }

    public static final class Security {
        private String bearerToken;

        public String getBearerToken() {
            return bearerToken;
        }

        public void setBearerToken(String bearerToken) {
            this.bearerToken = bearerToken;
        }
    }

    public static final class Codex {
        private boolean enabled = true;
        private String executable = "auto";
        private List<String> arguments = new ArrayList<>(List.of("app-server", "--listen", "stdio://"));
        private String defaultCwd = "";
        private Duration requestTimeout = Duration.ofSeconds(30);
        private Duration initializeTimeout = Duration.ofSeconds(20);
        private Duration restartDelay = Duration.ofSeconds(3);
        private Duration threadRefreshInterval = Duration.ofSeconds(15);
        private Duration rateLimitRefreshInterval = Duration.ofMinutes(2);
        private int threadPageSize = 50;
        private Duration approvalTtl = Duration.ofMinutes(2);

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        public String getExecutable() {
            return executable;
        }

        public void setExecutable(String executable) {
            this.executable = executable;
        }

        public List<String> getArguments() {
            return arguments;
        }

        public void setArguments(List<String> arguments) {
            this.arguments = arguments;
        }

        public String getDefaultCwd() {
            return defaultCwd;
        }

        public void setDefaultCwd(String defaultCwd) {
            this.defaultCwd = defaultCwd;
        }

        public Duration getRequestTimeout() {
            return requestTimeout;
        }

        public void setRequestTimeout(Duration requestTimeout) {
            this.requestTimeout = requestTimeout;
        }

        public Duration getInitializeTimeout() {
            return initializeTimeout;
        }

        public void setInitializeTimeout(Duration initializeTimeout) {
            this.initializeTimeout = initializeTimeout;
        }

        public Duration getRestartDelay() {
            return restartDelay;
        }

        public void setRestartDelay(Duration restartDelay) {
            this.restartDelay = restartDelay;
        }

        public Duration getThreadRefreshInterval() {
            return threadRefreshInterval;
        }

        public void setThreadRefreshInterval(Duration threadRefreshInterval) {
            this.threadRefreshInterval = threadRefreshInterval;
        }

        public Duration getRateLimitRefreshInterval() {
            return rateLimitRefreshInterval;
        }

        public void setRateLimitRefreshInterval(Duration rateLimitRefreshInterval) {
            this.rateLimitRefreshInterval = rateLimitRefreshInterval;
        }

        public int getThreadPageSize() {
            return threadPageSize;
        }

        public void setThreadPageSize(int threadPageSize) {
            this.threadPageSize = threadPageSize;
        }

        public Duration getApprovalTtl() {
            return approvalTtl;
        }

        public void setApprovalTtl(Duration approvalTtl) {
            this.approvalTtl = approvalTtl;
        }
    }

    public static final class Recordings {
        private Path directory = Path.of(System.getProperty("user.home"), ".vela-dial", "recordings");
        private long maxBytes = 25L * 1024 * 1024;
        private int idempotencyCapacity = 512;

        public Path getDirectory() {
            return directory;
        }

        public void setDirectory(Path directory) {
            this.directory = directory;
        }

        public long getMaxBytes() {
            return maxBytes;
        }

        public void setMaxBytes(long maxBytes) {
            this.maxBytes = maxBytes;
        }

        public int getIdempotencyCapacity() {
            return idempotencyCapacity;
        }

        public void setIdempotencyCapacity(int idempotencyCapacity) {
            this.idempotencyCapacity = idempotencyCapacity;
        }
    }

    public enum AsrProvider {
        CODEX,
        VOLCENGINE
    }

    public static final class Asr {
        private AsrProvider provider = AsrProvider.CODEX;
        private final Volcengine volcengine = new Volcengine();

        public AsrProvider getProvider() {
            return provider;
        }

        public void setProvider(AsrProvider provider) {
            this.provider = provider;
        }

        public Volcengine getVolcengine() {
            return volcengine;
        }
    }

    public static final class Volcengine {
        private URI endpoint =
                URI.create("https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash");
        private String apiKey = "";
        private String resourceId = "volc.bigasr.auc_turbo";
        private String uid = "vela-dial";
        private Duration requestTimeout = Duration.ofSeconds(45);

        public URI getEndpoint() {
            return endpoint;
        }

        public void setEndpoint(URI endpoint) {
            this.endpoint = endpoint;
        }

        public String getApiKey() {
            return apiKey;
        }

        public void setApiKey(String apiKey) {
            this.apiKey = apiKey;
        }

        public String getResourceId() {
            return resourceId;
        }

        public void setResourceId(String resourceId) {
            this.resourceId = resourceId;
        }

        public String getUid() {
            return uid;
        }

        public void setUid(String uid) {
            this.uid = uid;
        }

        public Duration getRequestTimeout() {
            return requestTimeout;
        }

        public void setRequestTimeout(Duration requestTimeout) {
            this.requestTimeout = requestTimeout;
        }
    }
}
