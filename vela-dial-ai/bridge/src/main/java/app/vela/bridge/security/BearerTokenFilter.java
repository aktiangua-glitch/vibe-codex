package app.vela.bridge.security;

import app.vela.bridge.config.BridgeProperties;
import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;
import tools.jackson.databind.json.JsonMapper;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.Map;

@Component
public class BearerTokenFilter extends OncePerRequestFilter {

    private static final String PREFIX = "Bearer ";

    private final byte[] expectedToken;
    private final JsonMapper jsonMapper;

    public BearerTokenFilter(BridgeProperties properties, JsonMapper jsonMapper) {
        this.expectedToken = properties.getSecurity().getBearerToken().getBytes(StandardCharsets.UTF_8);
        this.jsonMapper = jsonMapper;
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest request) {
        return !request.getRequestURI().startsWith("/api/v1/");
    }

    @Override
    protected void doFilterInternal(
            HttpServletRequest request,
            HttpServletResponse response,
            FilterChain filterChain) throws ServletException, IOException {
        String authorization = request.getHeader("Authorization");
        byte[] candidate = authorization != null && authorization.startsWith(PREFIX)
                ? authorization.substring(PREFIX.length()).getBytes(StandardCharsets.UTF_8)
                : new byte[0];

        if (!MessageDigest.isEqual(expectedToken, candidate)) {
            response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
            response.setContentType(MediaType.APPLICATION_JSON_VALUE);
            response.setCharacterEncoding(StandardCharsets.UTF_8.name());
            response.setHeader("WWW-Authenticate", "Bearer");
            jsonMapper.writeValue(response.getOutputStream(), Map.of(
                    "error", Map.of(
                            "code", "unauthorized",
                            "message", "A valid Vela Bridge bearer token is required"),
                    "timestamp", Instant.now().toString()));
            return;
        }

        filterChain.doFilter(request, response);
    }
}
