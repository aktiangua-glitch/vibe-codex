package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import com.sun.net.httpserver.HttpServer;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.http.HttpStatus;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicReference;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

class VolcengineAsrClientTest {

    @TempDir
    Path temporaryDirectory;

    @Test
    void usesOfficialFlashHeadersAndReadsResultText() throws Exception {
        JsonMapper mapper = JsonMapper.builder().build();
        AtomicReference<String> apiKey = new AtomicReference<>();
        AtomicReference<JsonNode> requestBody = new AtomicReference<>();
        HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/recognize", exchange -> {
            apiKey.set(exchange.getRequestHeaders().getFirst("X-Api-Key"));
            requestBody.set(mapper.readTree(exchange.getRequestBody().readAllBytes()));
            byte[] response = """
                    {"result":{"text":"不要删除这个文件"}}
                    """.getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().add("Content-Type", "application/json");
            exchange.getResponseHeaders().add("X-Api-Status-Code", "20000000");
            exchange.getResponseHeaders().add("X-Tt-Logid", "test-log");
            exchange.sendResponseHeaders(200, response.length);
            exchange.getResponseBody().write(response);
            exchange.close();
        });
        server.start();
        try {
            BridgeProperties properties = new BridgeProperties();
            properties.getAsr().getVolcengine().setApiKey("secret-key");
            properties.getAsr().getVolcengine().setEndpoint(URI.create(
                    "http://127.0.0.1:" + server.getAddress().getPort() + "/recognize"));
            Path wav = temporaryDirectory.resolve("sample.wav");
            Files.write(wav, new byte[]{1, 2, 3, 4});

            VolcengineAsrClient.Transcript result =
                    new VolcengineAsrClient(properties, mapper).transcribe(wav);

            assertThat(result.text()).isEqualTo("不要删除这个文件");
            assertThat(result.logId()).isEqualTo("test-log");
            assertThat(apiKey.get()).isEqualTo("secret-key");
            assertThat(requestBody.get().path("audio").path("format").asText()).isEqualTo("wav");
            assertThat(requestBody.get().path("request").path("model_name").asText())
                    .isEqualTo("bigmodel");
        } finally {
            server.stop(0);
        }
    }

    @Test
    void mapsVolcengineSilentAudioToActionableDeviceFeedback() throws Exception {
        JsonMapper mapper = JsonMapper.builder().build();
        HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/recognize", exchange -> {
            byte[] response = """
                    {"audio_info":{"duration":3882}}
                    """.getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().add("Content-Type", "application/json");
            exchange.getResponseHeaders().add("X-Api-Status-Code", "20000003");
            exchange.getResponseHeaders().add("X-Api-Message", "Silent audio");
            exchange.sendResponseHeaders(200, response.length);
            exchange.getResponseBody().write(response);
            exchange.close();
        });
        server.start();
        try {
            BridgeProperties properties = new BridgeProperties();
            properties.getAsr().getVolcengine().setApiKey("secret-key");
            properties.getAsr().getVolcengine().setEndpoint(URI.create(
                    "http://127.0.0.1:" + server.getAddress().getPort() + "/recognize"));
            Path wav = temporaryDirectory.resolve("silent.wav");
            Files.write(wav, new byte[]{1, 2, 3, 4});

            assertThatThrownBy(() -> new VolcengineAsrClient(properties, mapper).transcribe(wav))
                    .isInstanceOfSatisfying(BridgeApiException.class, exception -> {
                        assertThat(exception.status()).isEqualTo(HttpStatus.UNPROCESSABLE_ENTITY);
                        assertThat(exception.code()).isEqualTo("speech_not_recognized");
                        assertThat(exception.getMessage()).isEqualTo("没有听到说话，请重试");
                    });
        } finally {
            server.stop(0);
        }
    }
}
