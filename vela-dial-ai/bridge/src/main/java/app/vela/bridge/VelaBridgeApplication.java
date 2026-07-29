package app.vela.bridge;

import app.vela.bridge.config.BridgeProperties;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.EnableConfigurationProperties;

@SpringBootApplication
@EnableConfigurationProperties(BridgeProperties.class)
public class VelaBridgeApplication {

    public static void main(String[] args) {
        SpringApplication.run(VelaBridgeApplication.class, args);
    }
}
