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
    void onlySevenDayPrimaryDoesNotInventFiveHourWindow() throws Exception {
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

        assertThat(snapshot.quotaWindows()).singleElement().satisfies(window -> {
            assertThat(window.valid()).isTrue();
            assertThat(window.key()).isEqualTo("primary");
            assertThat(window.label()).isEqualTo("7D");
            assertThat(window.windowMinutes()).isEqualTo(10_080);
            assertThat(window.usedPercent()).isEqualTo(57);
            assertThat(window.remainingPercent()).isEqualTo(43);
        });
        assertThat(CodexStateStore.findWindow(snapshot.quotaWindows(), 300).valid())
                .isFalse();
    }

    @Test
    void derivesLabelsForArbitraryRateLimitDurations() throws Exception {
        CodexStateStore store = new CodexStateStore();
        store.replaceRateLimits(jsonMapper.readTree("""
                {
                  "rateLimits": {
                    "primary": {
                      "usedPercent": 12,
                      "windowDurationMins": 90
                    },
                    "secondary": {
                      "usedPercent": 34,
                      "windowDurationMins": 1500
                    }
                  }
                }
                """));

        CodexStateStore.StateSnapshot snapshot = store.snapshot(List.of(), List.of());

        assertThat(snapshot.quotaWindows())
                .extracting(
                        CodexStateStore.QuotaWindow::key,
                        CodexStateStore.QuotaWindow::label,
                        CodexStateStore.QuotaWindow::windowMinutes,
                        CodexStateStore.QuotaWindow::usedPercent)
                .containsExactly(
                        org.assertj.core.groups.Tuple.tuple(
                                "primary", "1H 30M", 90L, 12),
                        org.assertj.core.groups.Tuple.tuple(
                                "secondary", "1D 1H", 1_500L, 34));
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
        assertThat(CodexStateStore.findWindow(snapshot.quotaWindows(), 300).usedPercent())
                .isEqualTo(31);
        assertThat(CodexStateStore.findWindow(snapshot.quotaWindows(), 10_080).usedPercent())
                .isEqualTo(57);
    }

    @Test
    void parsesAccountTokenUsageAndSelectsLatestDailyBucket() throws Exception {
        CodexStateStore store = new CodexStateStore();
        store.replaceAccountUsage(jsonMapper.readTree("""
                {
                  "summary": {
                    "lifetimeTokens": 987654321,
                    "peakDailyTokens": 7654321,
                    "currentStreakDays": 23
                  },
                  "dailyUsageBuckets": [
                    {"startDate": "2026-07-28", "tokens": 111111},
                    {"startDate": "2026-07-30", "tokens": 333333},
                    {"startDate": "2026-07-29", "tokens": 222222}
                  ]
                }
                """));

        CodexStateStore.AccountTokenUsage usage =
                store.snapshot(List.of(), List.of()).tokenUsage();

        assertThat(usage.valid()).isTrue();
        assertThat(usage.lifetimeTokens()).isEqualTo(987_654_321L);
        assertThat(usage.peakDailyTokens()).isEqualTo(7_654_321L);
        assertThat(usage.currentStreakDays()).isEqualTo(23);
        assertThat(usage.latestDayLabel()).isEqualTo("2026-07-30");
        assertThat(usage.latestDayTokens()).isEqualTo(333_333L);
    }

    @Test
    void threadContextPercentExcludesReasoningTokensFromLastTurn() throws Exception {
        CodexStateStore store = new CodexStateStore();
        store.mergeThreadList(jsonMapper.readTree("""
                {
                  "data": [{
                    "id": "thread-token",
                    "name": "Token session",
                    "updatedAt": 100,
                    "status": {"type": "active"}
                  }]
                }
                """));
        store.onNotification("thread/tokenUsage/updated", jsonMapper.readTree("""
                {
                  "threadId": "thread-token",
                  "tokenUsage": {
                    "total": {
                      "totalTokens": 500000
                    },
                    "last": {
                      "totalTokens": 100000,
                      "reasoningOutputTokens": 20000
                    },
                    "modelContextWindow": 200000
                  }
                }
                """));

        CodexStateStore.SessionSnapshot session =
                store.findSession("thread-token").orElseThrow();

        assertThat(session.totalTokens()).isEqualTo(500_000L);
        assertThat(session.lastTokens()).isEqualTo(100_000L);
        assertThat(session.contextWindowTokens()).isEqualTo(200_000L);
        assertThat(session.contextUsedPercent()).isEqualTo(40);
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

    @Test
    void threadReadRestoresLatestAgentMessageAfterBridgeRestart() throws Exception {
        CodexStateStore store = new CodexStateStore();
        store.mergeThreadList(jsonMapper.readTree("""
                {
                  "data": [{
                    "id": "thread-latest",
                    "name": "语音会话",
                    "preview": "你好你好。",
                    "updatedAt": 100,
                    "status": {"type": "idle"}
                  }]
                }
                """));

        store.mergeThreadRead(jsonMapper.readTree("""
                {
                  "thread": {
                    "id": "thread-latest",
                    "name": "语音会话",
                    "preview": "你好你好。",
                    "updatedAt": 100,
                    "status": {"type": "idle"},
                    "turns": [
                      {
                        "id": "turn-1",
                        "status": "completed",
                        "items": [
                          {"type": "userMessage", "content": [{"type": "text", "text": "早上好"}]},
                          {"type": "agentMessage", "text": "早上好，需要我做什么？"}
                        ]
                      },
                      {
                        "id": "turn-2",
                        "status": "completed",
                        "items": [
                          {"type": "userMessage", "content": [{"type": "text", "text": "你好你好。"}]},
                          {"type": "agentMessage", "text": "你好，我在。"},
                          {"type": "commandExecution", "command": "ignored"}
                        ]
                      }
                    ]
                  }
                }
                """));

        CodexStateStore.SessionSnapshot session =
                store.findSession("thread-latest").orElseThrow();
        assertThat(session.lastItemType()).isEqualTo("agentMessage");
        assertThat(session.lastMessage()).isEqualTo("你好，我在。");
    }

    @Test
    void hydrationCandidatesAreLimitedToTheFiveDeviceSessions() {
        CodexStateStore store = new CodexStateStore();
        ObjectNode result = jsonMapper.createObjectNode();
        ArrayNode data = result.putArray("data");
        for (int index = 0; index < 20; index++) {
            ObjectNode thread = data.addObject();
            thread.put("id", "thread-" + index);
            thread.put("updatedAt", index);
            thread.putObject("status").put("type", "idle");
        }
        store.mergeThreadList(result);

        List<CodexStateStore.ThreadHydrationCandidate> candidates =
                store.hydrationCandidates(List.of("thread-0"), List.of());

        assertThat(candidates).hasSize(5);
        assertThat(candidates.get(0).id()).isEqualTo("thread-0");
        assertThat(candidates.stream().map(CodexStateStore.ThreadHydrationCandidate::id))
                .containsExactly("thread-0", "thread-19", "thread-18", "thread-17", "thread-16");
    }
}
