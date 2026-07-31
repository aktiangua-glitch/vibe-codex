package app.vela.bridge.api;

import org.springframework.http.HttpStatus;

public class BridgeApiException extends RuntimeException {

    private final HttpStatus status;
    private final String code;

    public BridgeApiException(HttpStatus status, String code, String message) {
        super(message);
        this.status = status;
        this.code = code;
    }

    public BridgeApiException(HttpStatus status, String code, String message, Throwable cause) {
        super(message, cause);
        this.status = status;
        this.code = code;
    }

    public HttpStatus status() {
        return status;
    }

    public String code() {
        return code;
    }
}
