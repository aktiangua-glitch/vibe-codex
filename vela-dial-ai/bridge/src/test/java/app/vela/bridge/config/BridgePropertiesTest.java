package app.vela.bridge.config;

import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThatNoException;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

class BridgePropertiesTest {

    @Test
    void rejectsShortBearerToken() {
        BridgeProperties properties = new BridgeProperties();
        properties.getSecurity().setBearerToken("too-short");

        assertThatThrownBy(properties::validate)
                .isInstanceOf(IllegalStateException.class)
                .hasMessageContaining("at least 32");
    }

    @Test
    void acceptsStrongLocalBearerToken() {
        BridgeProperties properties = new BridgeProperties();
        properties.getSecurity().setBearerToken("0123456789abcdef0123456789abcdef");

        assertThatNoException().isThrownBy(properties::validate);
    }
}
