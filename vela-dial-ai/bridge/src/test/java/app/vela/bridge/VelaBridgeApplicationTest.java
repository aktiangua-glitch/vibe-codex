package app.vela.bridge;

import app.vela.bridge.audio.VolcengineAsrClient;
import app.vela.bridge.codex.CodexAppServerClient;
import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.runner.WebApplicationContextRunner;

import static org.assertj.core.api.Assertions.assertThat;

class VelaBridgeApplicationTest {

    private final WebApplicationContextRunner contextRunner =
            new WebApplicationContextRunner()
                    .withUserConfiguration(VelaBridgeApplication.class)
                    .withPropertyValues(
                            "vela.security.bearer-token=test-token-with-at-least-32-bytes",
                            "vela.codex.enabled=false",
                            "vela.recordings.directory=target/test-runtime/recordings");

    @Test
    void applicationContextCreatesProductionBridgeComponents() {
        contextRunner.run(context -> {
            assertThat(context).hasNotFailed();
            assertThat(context).hasSingleBean(VolcengineAsrClient.class);
            assertThat(context).hasSingleBean(CodexAppServerClient.class);
        });
    }
}
