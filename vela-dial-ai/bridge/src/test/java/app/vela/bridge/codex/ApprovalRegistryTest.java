package app.vela.bridge.codex;

import app.vela.bridge.config.BridgeProperties;
import org.junit.jupiter.api.Test;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ObjectNode;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

class ApprovalRegistryTest {

    private final JsonMapper jsonMapper = JsonMapper.builder().build();

    @Test
    void concurrentApprovalsNeverOverwriteAndKeepCodexRequestIdType() {
        ApprovalRegistry registry = registry();
        ObjectNode firstParams = commandParams("thread-a", "turn-a", "item-shared", "pa-1");
        ObjectNode secondParams = commandParams("thread-a", "turn-a", "item-shared", "pa-2");

        ApprovalRegistry.ApprovalSnapshot first = registry.capture(
                "item/commandExecution/requestApproval",
                jsonMapper.getNodeFactory().numberNode(41),
                firstParams);
        ApprovalRegistry.ApprovalSnapshot second = registry.capture(
                "item/commandExecution/requestApproval",
                jsonMapper.getNodeFactory().textNode("rpc-string"),
                secondParams);

        assertThat(registry.queueSnapshot().pendingCount()).isEqualTo(2);
        assertThat(first.id()).startsWith("apv_").isNotEqualTo(second.id());
        assertThat(first.id()).isNotEqualTo("pa-1").isNotEqualTo("item-shared");
        assertThat(first.nonce()).isNotBlank();
        assertThat(first.actionDigest()).hasSize(64);

        ApprovalRegistry.ResolutionAttempt attempt = registry.beginResolution(
                first.id(), "accept", first.nonce(), first.actionDigest(), true);
        assertThat(attempt.approval().codexRequestId().isIntegralNumber()).isTrue();
        assertThat(attempt.approval().codexRequestId().asInt()).isEqualTo(41);
        registry.markSent(attempt);

        assertThat(registry.queueSnapshot().pendingCount()).isEqualTo(1);
        assertThat(registry.queueSnapshot().currentApproval().id()).isEqualTo(second.id());
    }

    @Test
    void nonceAndDigestBindDecisionToDisplayedAction() {
        ApprovalRegistry registry = registry();
        ApprovalRegistry.ApprovalSnapshot approval = registry.capture(
                "item/fileChange/requestApproval",
                jsonMapper.getNodeFactory().textNode("rpc-1"),
                fileParams());

        assertThatThrownBy(() -> registry.beginResolution(
                approval.id(), "decline", approval.nonce(), "wrong", true))
                .hasMessageContaining("nonce or actionDigest");
        assertThat(registry.queueSnapshot().pendingCount()).isEqualTo(1);
    }

    @Test
    void firmwareAllowAndRejectMapToCodexDecisions() {
        ApprovalRegistry registry = registry();
        ApprovalRegistry.ApprovalSnapshot approval = registry.capture(
                "item/fileChange/requestApproval",
                jsonMapper.getNodeFactory().textNode("rpc-allow"),
                fileParams());
        ApprovalRegistry.ResolutionAttempt allow = registry.beginResolution(
                approval.id(), "allow", approval.nonce(), approval.actionDigest(), true);
        assertThat(allow.decision()).isEqualTo("accept");
        assertThat(allow.wireDecision()).isEqualTo("accept");
        registry.rollbackResolution(allow);

        ApprovalRegistry.ResolutionAttempt reject = registry.beginResolution(
                approval.id(), "reject", approval.nonce(), approval.actionDigest(), true);
        assertThat(reject.decision()).isEqualTo("decline");
        assertThat(reject.wireDecision()).isEqualTo("decline");
    }

    @Test
    void deviceRejectSafelyMapsToCancelWhenDeclineIsUnavailable() {
        ApprovalRegistry registry = registry();
        ObjectNode params = commandParams(
                "thread-c", "turn-c", "item-c", "pa-c");
        params.putArray("availableDecisions")
                .add("acceptForSession")
                .add("cancel");
        ApprovalRegistry.ApprovalSnapshot approval = registry.capture(
                "item/commandExecution/requestApproval",
                jsonMapper.getNodeFactory().textNode("rpc-cancel"),
                params);

        ApprovalRegistry.ResolutionAttempt reject = registry.beginResolution(
                approval.id(), "reject", approval.nonce(), approval.actionDigest(), true);

        assertThat(reject.decision()).isEqualTo("decline");
        assertThat(reject.wireDecision()).isEqualTo("cancel");
        registry.markSent(reject);

        ApprovalRegistry.ResolutionAttempt replay = registry.beginResolution(
                approval.id(), "decline", approval.nonce(), approval.actionDigest(), true);
        assertThat(replay.state()).isEqualTo(ApprovalRegistry.ResolutionState.SENT);
        assertThat(replay.decision()).isEqualTo("decline");
        assertThat(replay.wireDecision()).isEqualTo("cancel");
    }

    @Test
    void deviceAllowNeverEscalatesToAcceptForSession() {
        ApprovalRegistry registry = registry();
        ObjectNode params = commandParams(
                "thread-c", "turn-c", "item-c", "pa-c");
        params.putArray("availableDecisions")
                .add("acceptForSession")
                .add("cancel");
        ApprovalRegistry.ApprovalSnapshot approval = registry.capture(
                "item/commandExecution/requestApproval",
                jsonMapper.getNodeFactory().textNode("rpc-session-only"),
                params);

        assertThatThrownBy(() -> registry.beginResolution(
                approval.id(), "allow", approval.nonce(), approval.actionDigest(), true))
                .hasMessageContaining("safe wire decision");
    }

    private ApprovalRegistry registry() {
        BridgeProperties properties = new BridgeProperties();
        properties.getSecurity().setBearerToken("test-token");
        return new ApprovalRegistry(properties);
    }

    private ObjectNode commandParams(
            String threadId,
            String turnId,
            String itemId,
            String protocolApprovalId) {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", threadId);
        params.put("turnId", turnId);
        params.put("itemId", itemId);
        params.put("approvalId", protocolApprovalId);
        params.put("startedAtMs", 1);
        params.put("command", "echo hello");
        params.putArray("availableDecisions").add("accept").add("decline");
        return params;
    }

    private ObjectNode fileParams() {
        ObjectNode params = jsonMapper.createObjectNode();
        params.put("threadId", "thread-b");
        params.put("turnId", "turn-b");
        params.put("itemId", "item-b");
        params.put("startedAtMs", 1);
        params.put("reason", "write file");
        return params;
    }
}
