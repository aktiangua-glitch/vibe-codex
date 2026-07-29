# Vela Dial Java Bridge

这是 Vela Dial 的本机 Bridge。它运行在 Mac 上，把 ESP32 圆屏设备连接到本机
Codex app-server，并负责保存录音、审批回包以及可选的火山引擎语音识别。

实现为独立重写的 Java 21 / Spring Boot 4.1.0 工程，没有复制
`open-vibe-island` 的 GPL 源码。

## 为什么直接接 Codex app-server

OpenAI 官方 `openai-java` SDK 面向 OpenAI HTTP API，不包含 Codex app-server
协议。Bridge 因此不把设备输入误接到 Responses API，而是严格使用 Codex 官方
app-server 的 stdio JSONL 协议。

当前实现依据本机 `codex app-server generate-json-schema` 生成的 stable schema：

- `initialize`
- `account/rateLimits/read`
- `thread/list`
- `thread/start`
- `thread/resume`
- `turn/start`
- `turn/steer`
- `item/commandExecution/requestApproval`
- `item/fileChange/requestApproval`
- thread / turn / item notifications

wire 消息遵循 schema，不添加 `jsonrpc` 字段。审批是 app-server 发给客户端的
反向 JSON-RPC 请求；Bridge 会保留 Codex 顶层 RequestId 的原始字符串或整数
类型，设备确认后再向同一个 ID 回应 `{ "decision": "accept|decline" }`。

## 运行

必须先配置 Bearer Token。没有 Token 时应用会拒绝启动：

```sh
cd vela-dial-ai/bridge
export VELA_BRIDGE_TOKEN='请替换成至少 32 字节的随机值'
./mvnw spring-boot:run
```

可用 [.env.example](./.env.example) 查看所有常用环境变量。它只是一份不含密钥的
模板，Spring Boot 不会自动加载；请按需复制其中的值到终端环境或 Cursor 的运行配置。

默认监听 `0.0.0.0:8787`，ESP32 应使用 Mac 的局域网 IP。所有
`/api/v1/**` 请求都必须携带：

```http
Authorization: Bearer <VELA_BRIDGE_TOKEN>
```

Codex 可执行文件按以下顺序解析：

1. `VELA_CODEX_EXECUTABLE`
2. `/Applications/ChatGPT.app/Contents/Resources/codex`
3. `/Applications/Codex.app/Contents/Resources/codex`
4. 用户 `Applications` 下的相同 App Bundle
5. 最后使用 PATH 中的 `codex`

如需手动覆盖：

```sh
export VELA_CODEX_EXECUTABLE=/Applications/ChatGPT.app/Contents/Resources/codex
```

## 设备 API

### 获取快照

```http
GET /api/v1/device/snapshot
Authorization: Bearer ...
```

固定设备契约的核心字段：

```json
{
  "revision": 9,
  "quota": {
    "five_hour": {
      "valid": false,
      "used_percent": null,
      "remaining_percent": null,
      "reset_label": null
    },
    "seven_day": {
      "valid": true,
      "used_percent": 57,
      "remaining_percent": 43,
      "reset_label": "2天后"
    }
  },
  "total_session_count": 20,
  "sessions": [
    {
      "thread_id": "019...",
      "title": "Build firmware",
      "summary": "正在编译",
      "state": "waiting_approval",
      "needs_feedback": false,
      "approval": {
        "present": true,
        "approval_id": "apv_...",
        "thread_id": "019..."
      }
    }
  ],
  "current_approval": {
    "approval_id": "apv_...",
    "thread_id": "019...",
    "nonce": "...",
    "action_digest": "64位十六进制SHA-256",
    "title": "运行命令",
    "detail": "pio run",
    "expires_at_ms": 1780000000000
  },
  "pending_approval_count": 1,
  "device_operation": {
    "kind": "NEW_THREAD",
    "state": "completed",
    "id": "operation UUID",
    "message": "录音已提交到 Codex",
    "result_thread_id": "019..."
  }
}
```

注意：

- 额度按官方 `windowDurationMins` 映射：`300` 为 5H，`10080` 为 7D。
- app-server 没返回的窗口保持 `valid=false`，绝不会伪造成使用率 0。
- 设备最多收到 5 个会话；待审批和需要补充拒绝理由的会话优先，不会因为
  排名较旧而消失。
- 审批使用全局 FIFO，`current_approval` 是队首，
  `pending_approval_count` 是尚未提交的总数。
- 拒绝后会持久化 `needs_feedback`。即使录音失败或 Bridge 重启，设备仍能用
  session 的 `approval.approval_id` 再次发送理由。

### 上传 WAV

支持原始 `audio/wav`，也支持标准 `multipart/form-data` 的 `file` part：

```http
POST /api/v1/device/recordings
Authorization: Bearer ...
Idempotency-Key: <设备生成的稳定opaque key>
X-Vela-Device-Id: <设备ID，可选，默认default>
X-Vela-Purpose: new_session
X-Vela-Wav-Crc32: 12ab34cd
Content-Type: audio/wav

<raw WAV bytes>
```

用途：

| 固件值 | Bridge 内部值 | 行为 |
|---|---|---|
| `new_session` | `NEW_THREAD` | 创建 thread，再启动 turn |
| `prompt` | `PROMPT` | 当前 turn 活跃时 steer，否则 start |
| `steer` | `STEER` | 只允许发送到活跃 turn |
| `reject_reason` | `APPROVAL_REASON` | 发送拒绝补充理由，成功后清除 ADD REASON |
| `store_only` | `STORE_ONLY` | 只保存 WAV |

已有会话需带 `X-Vela-Thread-Id`；拒绝理由还需
`X-Vela-Approval-Id`。

响应把固件需要的字段放在根部：

```json
{
  "operation_id": "uuid",
  "state": "completed",
  "message": "录音已提交到 Codex",
  "result_thread_id": "019...",
  "result_turn_id": "019..."
}
```

WAV 在服务端重新验证，不能只依赖设备：

- RIFF/WAVE committed length 必须与 HTTP body 完全一致
- PCM、单声道、24 kHz、16-bit
- `blockAlign=2`、`byteRate=48000`
- data chunk 必须完整且没有尾部垃圾
- CRC32 必须与 `X-Vela-Wav-Crc32` 一致
- 默认最大 25 MiB

录音和 operation ledger 保存在：

```text
~/.vela-dial/recordings/
├── *.wav
├── operations/*.json
└── feedback-state.json
```

幂等键为 `(deviceId, Idempotency-Key, purpose)`，阶段持久化为：

```text
received → transcribed → codexSubmitted → completed
```

重复请求返回原 operation，不重复 ASR 或 turn。Bridge 还把 operation UUID 作为
Codex `clientUserMessageId`。如果 mutation 在协议超时前没有收到响应，状态会停在
`codexSubmitted` 并注明“结果未知”，不会盲目重发。

### 处理审批

```http
POST /api/v1/device/approvals/{approval_id}/resolve
Authorization: Bearer ...
Content-Type: application/json

{
  "decision": "allow",
  "nonce": "<snapshot中的nonce>",
  "action_digest": "<snapshot中的64位digest>"
}
```

固件的 `allow/reject` 会映射为 Codex 的 `accept/decline`。Bridge 的 approval ID
是 HMAC 自签 opaque token，不复用 Codex 的 `params.approvalId` 或 `itemId`。
`nonce + action_digest` 把决定绑定到设备刚刚看到的操作，防止旧界面误批新操作。

- `200 OK`：Bridge 已把决定完整写入并 flush 到 Codex stdio。响应中的
  `codexConfirmed=false` 表示尚未观察到 `serverRequest/resolved`，
  `codexConfirmed=true` 表示 Codex 已明确确认；两者都不会让设备停在
  “仍在提交”的界面。
- `409 Conflict`：决定、nonce、digest 或幂等元数据冲突。
- `503 Service Unavailable`：Codex app-server 未连接。

审批默认 2 分钟过期。过期动作会安全拒绝并让 FIFO 继续，不会自动创建
“补充拒绝理由”任务。

### 健康状态

```http
GET /api/v1/health
Authorization: Bearer ...
```

Codex 已启用但尚未连接时返回 `503`；显式
`VELA_CODEX_ENABLED=false` 时可用于仅保存录音的维护环境。

## ASR

默认：

```sh
export VELA_ASR_PROVIDER=codex
```

Bridge 将绝对 WAV 路径作为官方 `localAudio` turn input 交给 Codex，不单独
生成 transcript。

火山引擎模式：

```sh
export VELA_ASR_PROVIDER=volcengine
export VOLCENGINE_ASR_API_KEY='...'
export VOLCENGINE_ASR_RESOURCE_ID=volc.bigasr.auc_turbo
```

实现使用 JDK `HttpClient` 调用官方录音文件极速 REST 端点，发送
`X-Api-Key`、`X-Api-Resource-Id`、`X-Api-Request-Id` 和
`X-Api-Sequence: -1`，读取 `result.text`。没有使用非官方 Maven SDK，密钥也
不会下发 ESP32。

## 构建和测试

```sh
./mvnw test
./mvnw package
```

测试覆盖：

- Spring 应用上下文和生产组件装配
- 额度缺失窗口和 sparse update
- 20 个会话中旧会话审批仍进入设备前 5
- 同 thread / item 的多个审批不会覆盖
- Codex RequestId 的 string / integer 类型保留
- nonce / action digest 绑定
- 固件 JSON 字段契约
- 固件 purpose 和 allow/reject vocabulary
- 24 kHz PCM WAV、RIFF/data 长度、CRC 校验
- 火山极速 API 请求头和 `result.text`

本目录只包含 Mac Bridge；不会编译或修改 `../src` 下的 ESP32 固件。
