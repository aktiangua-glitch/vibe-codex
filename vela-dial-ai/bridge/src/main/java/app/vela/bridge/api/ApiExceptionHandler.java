package app.vela.bridge.api;

import jakarta.validation.ConstraintViolationException;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.MissingRequestHeaderException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

import java.time.Instant;
import java.util.Map;
import java.util.concurrent.CompletionException;

@RestControllerAdvice
public class ApiExceptionHandler {

    @ExceptionHandler(BridgeApiException.class)
    ResponseEntity<Map<String, Object>> handleBridgeException(BridgeApiException exception) {
        return error(exception.status(), exception.code(), exception.getMessage());
    }

    @ExceptionHandler({MissingRequestHeaderException.class, IllegalArgumentException.class,
            ConstraintViolationException.class})
    ResponseEntity<Map<String, Object>> handleBadRequest(Exception exception) {
        return error(HttpStatus.BAD_REQUEST, "bad_request", exception.getMessage());
    }

    @ExceptionHandler(CompletionException.class)
    ResponseEntity<Map<String, Object>> handleCompletionException(CompletionException exception) {
        Throwable cause = exception.getCause();
        if (cause instanceof BridgeApiException bridgeApiException) {
            return handleBridgeException(bridgeApiException);
        }
        return error(HttpStatus.BAD_GATEWAY, "upstream_failure",
                cause == null ? exception.getMessage() : cause.getMessage());
    }

    @ExceptionHandler(Exception.class)
    ResponseEntity<Map<String, Object>> handleUnexpected(Exception exception) {
        return error(HttpStatus.INTERNAL_SERVER_ERROR, "internal_error", "Bridge request failed");
    }

    private ResponseEntity<Map<String, Object>> error(HttpStatus status, String code, String message) {
        return ResponseEntity.status(status).body(Map.of(
                "error", Map.of(
                        "code", code,
                        "message", message == null ? status.getReasonPhrase() : message),
                "timestamp", Instant.now().toString()));
    }
}
