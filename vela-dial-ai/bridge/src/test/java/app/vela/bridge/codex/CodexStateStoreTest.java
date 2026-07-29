package app.vela.bridge.codex;

import org.junit.jupiter.api.Test;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;

import java.time.Instant;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

class CodexStateStoreTest {

    private final JsonMapper jsonMapper = JsonMapper.builder().build();

    @Test
    void missingFiveHourWindowIsUnknownInsteadOfZero() throws Exception {
        CodexStateStore store = new CodexStateStore();
        JsonNode result = jsonMapper.readTree("""
                {
                  "rateLimits": {
                    "primary": {
                      "usedPercent": 57,
                      "windowDurationMins": 10080,
                      "resetsAt": 4102444800
                    },
                    "secondary": null
                  }
                }
                """);

        store.replaceRateLimits(result);
        CodexStateStore.StateSnapshot snapshot = store.snapshot(List.of(), List.of());

        assertThat(snapshot.quota5h().valid()).isFalse();
        assertThat(snapshot.quota5h().usedPercent()).isNull();
        assertThat(snapshot.quota7d().valid()).isTrue();
        assertThat(snapshot.quota7d().usedPercent()).isEqualTo(57);
        assertThat(snapshot.quota7d().remainingPercent()).isEqualTo(43);
    }

    @Test
    void sparseRateLimitNotificationPreservesOmittedWindow() throws Exception {
        CodexStateStore store = new CodexStateStore();
        store.replaceRateLimits(jsonMapper.readTree("""
                {
                  "rateLimits": {
                    "primary": {"usedPercent": 30, "windowDurationMins": 300},
                    "secondary": {"usedPercent": 57, "windowDurationMins": 10080}
                  }
                }
                """));
        store.onNotification("account/rateLimits/updated", jsonMapper.readTree("""
                {
                  "rateLimits": {
                    "primary": {"usedPercent": 31, "windowDurationMins": 300}
                  }
                }
                """));

        CodexStateStore.StateSnapshot snapshot = store.snapshot(List.of(), List.of());
        assertThat(snapshot.quota5h().usedPercent()).isEqualTo(31);
        assertThat(snapshot.quota7d().usedPercent()).isEqualTo(57);
    }

    @Test
    void pendingApprovalSessionSurvivesFiveSessionDeviceLimit() {
        CodexStateStore store = new CodexStateStore();
        ObjectNode result = jsonMapper.createObjectNode();
        ArrayNode data = result.putArray("data");
        long now = Instant.now().getEpochSecond();
        for (int index = 0; index < 20; index++) {
            ObjectNode thread = data.addObject();
            thread.put("id", "thread-" + index);
            thread.put("name", "Thread " + index);
            thread.put("preview", "preview");
            thread.put("cwd", "/tmp");
            thread.put("updatedAt", now - index);
            thread.putObject("status").put("type", "idle");
            thread.putArray("turns");
        }
        store.mergeThreadList(result);

        CodexStateStore.StateSnapshot snapshot =
                store.snapshot(List.of("thread-19"), List.of("thread-18"));

        assertThat(snapshot.sessions()).hasSize(5);
        assertThat(snapshot.sessions().get(0).id()).isEqualTo("thread-19");
        assertThat(snapshot.sessions().get(0).pendingApproval()).isTrue();
        assertThat(snapshot.sessions().get(1).id()).isEqualTo("thread-18");
        assertThat(snapshot.sessions().get(1).needsFeedback()).isTrue();
    }
}
