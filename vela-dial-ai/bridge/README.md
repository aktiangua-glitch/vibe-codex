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
- `account/rateLimits/updated`
- `account/usage/read`
- `thread/list`
- `thread/start`
- `thread/resume`
- `turn/start`
- `turn/steer`
- `thread/tokenUsage/updated`
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

本机开发也可以使用 Spring Boot 原生外部配置。复制示例后填写本机值：

```sh
cp config/application.example.yml config/application.yml
```

`config/application.yml` 会在从 `bridge/` 目录启动时自动覆盖打包内的安全默认值，
并已被项目 `.gitignore` 排除。火山引擎 API Key 只应写入这个本地文件，不应写入
`src/main/resources/application.yml`、固件或提交到 Git。

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
    "windows": [
      {
        "valid": true,
        "key": "primary",
        "label": "7D",
        "window_minutes": 10080,
        "used_percent": 57,
        "remaining_percent": 43,
        "reset_label": "2天后"
      }
    ],
    "tokens": {
      "valid": true,
      "lifetime_tokens": 123456,
      "latest_day_tokens": 1234,
      "latest_day_label": "2026-07-30",
      "peak_daily_tokens": 5678,
      "current_streak_days": 3
    },
    "five_hour": {
      "valid": false,
      "key": "primary",
      "label": "5H",
      "window_minutes": 300,
      "used_percent": null,
      "remaining_percent": null,
      "reset_label": null
    },
    "seven_day": {
      "valid": true,
      "key": "primary",
      "label": "7D",
      "window_minutes": 10080,
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
      "total_tokens": 48000,
      "last_tokens": 9200,
      "context_window_tokens": 200000,
      "context_used_percent": 4,
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

- `quota.windows` 是当前权威字段。Bridge 按官方 `windowDurationMins` 动态生成
  标签并返回实际存在的窗口，不假设账户一定同时拥有 `5H / 7D`；当前账户可能
  只返回一个 `7D`。
- `five_hour / seven_day` 仅为旧固件兼容别名。app-server 没返回的兼容窗口保持
  `valid=false`，绝不会伪造成使用率 0，也不会在新 UI 中占据一个空白页面。
- 固件默认选中 `used_percent` 最高、压力最大的实际窗口；旋钮在实际窗口与
  `TOKEN` 页之间切换，右滑才进入会话列表。
- `quota.tokens` 来自官方 `account/usage/read`，是账户活动统计，不等同于
  `account/rateLimits/read` 的额度百分比。
- 会话 Token 来自 `thread/tokenUsage/updated`。上下文占用按
  `(last.totalTokens - last.reasoningOutputTokens) / modelContextWindow`
  计算，不使用线程累计 Token 除以上下文窗口。
- 设备最多收到 5 个会话；待审批和需要补充拒绝理由的会话优先，不会因为
  排名较旧而消失。
- 审批使用全局 FIFO，`current_approval` 是队首，
  `pending_approval_count` 是尚未提交的总数。
- 拒绝后会持久化 `needs_feedback`。即使录音失败或 Bridge 重启，设备仍能用
  session 的 `approval.approval_id` 再次发送理由。

额度窗口的动态解析思路与
[`open-vibe-island` 的 `CodexUsage`](https://github.com/Octane0411/open-vibe-island/blob/6e5e7a6a5b5097ee627a7d4dea6226c128747a71/Sources/OpenIslandCore/CodexUsage.swift)
一致：读取 `primary / secondary` 的实际分钟数，而不是把产品文案当成协议。
`open-vibe-island` 的生产 UI 当前没有账户 Token 页；本 Bridge 通过
[Codex app-server 官方接口](https://github.com/openai/codex/blob/main/codex-rs/app-server/README.md)
补充 `account/usage/read` 和单会话上下文通知。

## 中文字形职责

当前中文由固件内置的 Noto Sans CJK SC 16px、2 bpp 压缩常用字库负责，
`LV_USE_FONT_COMPRESSED=1` 必须保持开启。它覆盖 GB2312 常用简体字、ASCII
与常用标点，不能保证生僻字、繁体扩展字和 emoji。

商业版不会让 Bridge 下发完整字体文件，而是参考 `xiaozhi-esp32`：

- Bridge 按 Unicode 码点返回缺失字形位图与度量信息；
- 固件把它接入 LVGL font fallback；
- 字形使用 PSRAM LRU 缓存，基础压缩字库继续承担离线兜底。

参考：

- [动态字形缓存](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/display/lvgl_display/dynamic_glyph_cache.h#L11-L19)
- [LVGL fallback 接入](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/display/lvgl_display/lvgl_display.cc#L78-L103)
- [字形载荷校验](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/protocols/text_glyph_payload.cc#L14-L63)

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

当前接口接收一条完整 WAV，适合旋钮语音命令。长时间听写将使用另一条流式
链路：ESP32 持续向局域网 Bridge 发送 24 kHz、16-bit、mono PCM 分片，Bridge
再通过火山引擎双向流式 WebSocket 上报。设备仍以连续静音 2 秒结束当前语句；
云端 API Key 和 WebSocket 鉴权始终只存在 Java 进程中。

## 构建和测试

```sh
./mvnw test
./mvnw package
```

测试覆盖：

- Spring 应用上下文和生产组件装配
- 动态额度窗口、缺失窗口和 sparse update
- 账户 Token 与单会话上下文占用
- 20 个会话中旧会话审批仍进入设备前 5
- 同 thread / item 的多个审批不会覆盖
- Codex RequestId 的 string / integer 类型保留
- nonce / action digest 绑定
- 固件 JSON 字段契约
- 固件 purpose 和 allow/reject vocabulary
- 24 kHz PCM WAV、RIFF/data 长度、CRC 校验
- 火山极速 API 请求头和 `result.text`

本目录只包含 Mac Bridge；不会编译或修改 `../src` 下的 ESP32 固件。
