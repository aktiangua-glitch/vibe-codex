package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ObjectNode;

import java.io.IOException;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Base64;
import java.util.UUID;

@Component
public class VolcengineAsrClient {

    public record Transcript(
            String text,
            String requestId,
            String logId) {
    }

    private static final String SUCCESS_STATUS = "20000000";

    private final BridgeProperties properties;
    private final JsonMapper jsonMapper;
    private final HttpClient httpClient;

    @Autowired
    public VolcengineAsrClient(BridgeProperties properties, JsonMapper jsonMapper) {
        this(
                properties,
                jsonMapper,
                HttpClient.newBuilder()
                        .connectTimeout(Duration.ofSeconds(10))
                        .build());
    }

    VolcengineAsrClient(
            BridgeProperties properties,
            JsonMapper jsonMapper,
            HttpClient httpClient) {
        this.properties = properties;
        this.jsonMapper = jsonMapper;
        this.httpClient = httpClient;
    }

    public Transcript transcribe(Path wavFile) {
        BridgeProperties.Volcengine config = properties.getAsr().getVolcengine();
        if (config.getApiKey() == null || config.getApiKey().isBlank()) {
            throw new BridgeApiException(
                    HttpStatus.SERVICE_UNAVAILABLE,
                    "volcengine_not_configured",
                    "VOLCENGINE_ASR_API_KEY is not configured");
        }

        String requestId = UUID.randomUUID().toString();
        ObjectNode body = jsonMapper.createObjectNode();
        body.putObject("user").put("uid", config.getUid());
        try {
            ObjectNode audio = body.putObject("audio");
            audio.put("format", "wav");
            audio.put(
                    "data",
                    Base64.getEncoder().encodeToString(Files.readAllBytes(wavFile)));
        } catch (IOException exception) {
            throw new BridgeApiException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "recording_read_failed",
                    "Unable to read persisted recording",
                    exception);
        }
        body.putObject("request").put("model_name", "bigmodel");

        HttpRequest request = HttpRequest.newBuilder(config.getEndpoint())
                .timeout(config.getRequestTimeout())
                .header("Content-Type", "application/json")
                .header("X-Api-Key", config.getApiKey())
                .header("X-Api-Resource-Id", config.getResourceId())
                .header("X-Api-Request-Id", requestId)
                .header("X-Api-Sequence", "-1")
                .POST(HttpRequest.BodyPublishers.ofString(jsonMapper.writeValueAsString(body)))
                .build();

        HttpResponse<String> response;
        try {
            response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new BridgeApiException(
                    HttpStatus.BAD_GATEWAY,
                    "volcengine_interrupted",
                    "Volcengine ASR request was interrupted",
                    exception);
        } catch (IOException exception) {
            throw new BridgeApiException(
                    HttpStatus.BAD_GATEWAY,
                    "volcengine_unreachable",
                    "Volcengine ASR request failed",
                    exception);
        }

        String apiStatus = response.headers().firstValue("X-Api-Status-Code").orElse("");
        String logId = response.headers().firstValue("X-Tt-Logid").orElse(null);
        if (response.statusCode() / 100 != 2
                || (!apiStatus.isBlank() && !SUCCESS_STATUS.equals(apiStatus))) {
            String message = response.headers().firstValue("X-Api-Message")
                    .orElse("Volcengine ASR rejected the recording");
            throw new BridgeApiException(
                    HttpStatus.BAD_GATEWAY,
                    "volcengine_rejected",
                    message + (logId == null ? "" : " (logId " + logId + ")"));
        }

        JsonNode responseJson;
        try {
            responseJson = jsonMapper.readTree(response.body());
        } catch (Exception exception) {
            throw new BridgeApiException(
                    HttpStatus.BAD_GATEWAY,
                    "volcengine_invalid_response",
                    "Volcengine ASR returned invalid JSON",
                    exception);
        }
        JsonNode textNode = responseJson.path("result").path("text");
        if (!textNode.isTextual() || textNode.asText().isBlank()) {
            throw new BridgeApiException(
                    HttpStatus.UNPROCESSABLE_ENTITY,
                    "speech_not_recognized",
                    "No speech was recognized in the recording");
        }
        return new Transcript(textNode.asText(), requestId, logId);
    }
}
