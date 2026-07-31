# Vela Dial AI

面向 **JC3636K718 / ESP32-S3** 的独立编程 AI 项目。设备端使用 PlatformIO、
Arduino 与 LVGL 8.4；电脑端使用 Java 21 与 Spring Boot，把圆屏旋钮连接到本机
Codex。

这不是网页原型，也不依赖旧的 `jc3636k718-platformio-demo`。当前工程已经包含：

- 可烧录的 ESP32 固件
- 可直接启动的 Java Bridge
- 编译期私有 Wi-Fi 配置与开放 AP 兜底配网
- 动态 Codex 额度、Token 用量与会话上下文
- 新会话语音输入
- 旋钮审批与拒绝理由语音回传
- 录音掉线重试、请求幂等和审批安全绑定

2026-07-30 已用上一版固件完成 Wi-Fi、Java Bridge、火山引擎语音识别、
新建 Codex 会话与回复回显的端到端联调。本次动态额度、Token 与中文字库修复
已完成离线构建和 Bridge 验收，等待下一次连接硬件后烧录复验。

## 整体架构

```text
┌──────────────────────────────────────┐
│ JC3636K718                           │
│ LVGL · 旋钮 · 圆屏 · 麦克风 · TF 卡 │
└────────────────┬─────────────────────┘
                 │ 局域网 HTTP / Bearer / Idempotency-Key
                 ▼
┌──────────────────────────────────────┐
│ Vela Java Bridge                    │
│ Spring Boot · 状态聚合 · WAV 持久化 │
└───────────────┬──────────────────────┘
                │ 官方 stdio JSONL
                ▼
┌──────────────────────────────────────┐
│ Codex app-server                    │
│ 额度 · Token · Thread · Turn · 审批 │
└──────────────────────────────────────┘

当前实机语音路径：
TF 卡 WAV → Java Bridge → 火山引擎极速版 ASR → 文本 → Codex
可选回退路径：
TF 卡 WAV → Java Bridge → Codex localAudio

计划中的长语音路径：
ESP32 PCM 音频流 → Java Bridge → 火山引擎双向流式 ASR → 文本 → Codex
```

## 为什么 Java 没有直接引入 OpenAI SDK

这里没有自造一套 Codex 协议，但也没有为了“使用 SDK”而选错接口：

- [OpenAI 官方 Java SDK](https://github.com/openai/openai-java) 面向 OpenAI
  REST API。
- 本项目需要的是本机 Codex 的额度、已有会话、Turn 生命周期和双向审批，
  官方集成入口是
  [Codex app-server](https://github.com/openai/codex/blob/main/codex-rs/app-server/README.md)。
- Bridge 使用随当前 ChatGPT/Codex 应用安装的 `codex` 可执行文件，并调用
  `app-server generate-json-schema` 所生成的同版本 stable schema。
- 连接使用官方的 stdio JSONL 双向协议；初始化明确关闭 experimental API。
- Java Web 层使用 Spring Boot 4.1.0、Bean Validation、JDK `HttpClient`、
  `java.nio`、持久化 JSON 文件和标准 HTTP 语义。
- 火山引擎部分严格按
  [录音文件极速版识别 HTTP 文档](https://docs.volcengine.com/docs/6561/1631584?lang=zh)
  实现；没有引入来源不明的第三方 Maven SDK。

项目特有的部分只有 ESP32 与 Java Bridge 之间很薄的一层版本化设备 REST API。
它沿用常见的 `Authorization: Bearer`、`Idempotency-Key`、明确 HTTP 状态码和
严格 JSON 契约，详细字段见 [Bridge 文档](./bridge/README.md)。

## 旋钮优先的 AI 流程

### 额度与 Token

开机联网后默认进入额度页。

- 额度窗口来自 `account/rateLimits/read`，按 app-server 实际返回的
  `windowDurationMins` 动态生成，不再写死 `5H / 7D`
- 默认显示当前 `usedPercent` 最高、压力最大的窗口
- 旋钮只在实际存在的额度窗口与 `TOKEN` 页之间顺序浏览；不存在的窗口不占位置，
  也不会伪造成 `0%`
- 当前账户可能只返回一个 `7D` 窗口，此时顺序是 `7D → TOKEN → 会话列表`，
  不会从末尾循环回第一项
- `TOKEN` 页来自官方 `account/usage/read`，显示最近一天与累计 Token 等账户级
  统计
- 额度同步采用双来源：Bridge 优先读取 Codex app-server；当
  `account/usage/read` 暂时超时或当前 Codex 版本没有返回完整窗口时，再合并读取
  `~/.codex/sessions/**/rollout-*.jsonl` 中的 `event_msg → token_count →
  rate_limits`。这与 [open-vibe-island](https://github.com/Octane0411/open-vibe-island)
  的成熟做法一致，避免把“暂时没有数据”误显示成 `0%`。
- 右滑或在 `TOKEN` 页继续向右旋一格进入会话列表；额度页的用量切换不会循环

已配置设备启动时只显示一次轻量初始化过渡，后台继续连接 Wi-Fi/Bridge，随后
直接进入编程 AI 额度页；不会再先显示 `CONNECTING` 或 `BRIDGE OFFLINE` 的第二个
加载界面。未配置设备仍会进入 AP 配网界面。

配置过的设备在所有编程 AI 页面顶部持续显示 `WiFi` 与 `Bridge` 两个状态点：
绿色表示在线，琥珀色表示后台重连，红色表示 Wi-Fi 不可用。额度尚未到达时显示
“Wi-Fi 未连接”“Bridge 未连接”或“正在同步额度”，不会再只显示一个无原因的 `--`。

设备端 UI 使用 ASCII `WiFi`，并会把服务端回执中的 Unicode 连接号、长破折号和
数学减号统一归一化为 ASCII `-`；这样不会因为精简字库缺少 U+2010/U+2011 等字形
而出现方框。会话详情的最新回执使用 LVGL `LV_LABEL_LONG_SCROLL_CIRCULAR` 多行
回执区：单向缓慢滚到尾端，停顿后从开头重新开始，不使用来回播放。Bridge 摘要现在
最多保留 480 个字符，设备端保留 768 字节，避免回复只剩第一句。详情页只保留标题、
输入/完成状态和最新回执；输入中会显示 spinner 与动态点号。当前详情会话内容没有变化
时不会因后台其他会话更新而重绘，因此 spinner 和回执动画不会闪烁或被重置。

完整回执不放进高频设备快照：Bridge 在内存中保留完整最新回复，并提供
`GET /api/v1/device/sessions/{threadId}/reply` 返回完整文本；设备快照只携带显示摘要。
Bridge 日志固定写入 `~/.vela-dial/vela-bridge.log`，启动终端仍会同步输出。

会话处于 `running` 时，详情页中央改为持续旋转的 LVGL spinner，并显示“正在输入”与
动态点号；设备端快照轮询在普通状态约 1.5 秒一次，操作进行中约 0.7 秒一次，避免
输入后页面长时间看起来没有变化。

额度百分比与 Token 不是同一指标：前者是服务端配额窗口，后者是账户活动统计。
会话详情中的上下文占用来自 `thread/tokenUsage/updated`，计算方式为：

```text
(last.totalTokens - last.reasoningOutputTokens) / modelContextWindow
```

这里使用“最后一次上下文”而不是累计 Token，避免长会话被错误显示为超过 100%。

### 会话

会话列表先显示最多 5 个真实 Codex 会话，“新会话”固定放在列表末尾，避免每次
打开列表时首先停在高频误触动作上。

- 旋钮选择条目
- 停留 3 秒进入
- 继续旋转会立即取消旧计时并重新计时
- 任意已有会话都先进入详情，不会从列表直接跳审批
- 详情卡的 `CODEX REPLY` 显示最后一条助手消息；Bridge 重启后会用官方
  `thread/read(includeTurns=true)` 自动恢复，而不是退回成用户输入预览
- 列表左边界第一次继续左转显示外圈返回提示，再左转一次才返回

### 新会话语音

```text
选择“新会话”
→ 停留 3 秒
→ 自动录音
→ 检测到讲话
→ 连续静默 2 秒
→ 停止并写入 TF 卡
→ 上传 Java Bridge
→ 创建 Codex Thread 与 Turn
→ 返回新会话详情
```

没有检测到有效讲话时不会发送空会话。网络断开时，已提交的录音会在 TF 卡与
NVS 中保留幂等标记，重连或重启后继续同一次上传，不会重复创建 Turn。

### 审批

待确认会话仍先进入会话详情；在详情中选择确认入口并停留 3 秒后进入审批页。

- 左转选择拒绝
- 右转选择通过
- 对选择停留 3 秒才提交
- 计时期间反向旋转会取消旧决定并重新计时
- 通过后 Codex 继续运行
- 拒绝决定先成立，随后自动录制拒绝理由
- 拒绝理由静默 2 秒后自动发回同一个 Thread
- 录音失败时保留 `ADD REASON`，不会撤销或重复提交拒绝
- 审批页左滑为“稍后处理”：返回被打断前的页面，审批仍保留，同一条请求
  不会立即再次抢占

触摸只保留为少量快捷入口；不用触屏也能完成额度、会话、语音和审批主流程。

## 第一次运行

### 1. 启动 Java Bridge

本机已经安装 Java 21、Maven 和 PlatformIO。先生成并保存一个仅供设备与
Bridge 使用的 Token：

```sh
openssl rand -hex 32
```

启动 Bridge：

```sh
cd vela-dial-ai/bridge

export VELA_BRIDGE_TOKEN='粘贴刚生成的随机值'
export VELA_DEFAULT_CWD='/path/to/your/codex/workspace'

./mvnw spring-boot:run
```

默认监听 `0.0.0.0:8787`，会自动寻找：

1. ChatGPT 应用内的 `codex`
2. Codex 应用内的 `codex`
3. PATH 中的 `codex`

也可以直接运行已打包的文件：

```sh
cd vela-dial-ai/bridge

export VELA_BRIDGE_TOKEN='同一个随机值'
export VELA_DEFAULT_CWD='/path/to/your/codex/workspace'

java -jar target/vela-dial-bridge-0.1.0-SNAPSHOT.jar
```

Spring Boot 会自动读取 `bridge/config/application.yml`。本机开发可从无密钥示例
创建本地配置：

```sh
cd vela-dial-ai/bridge
cp config/application.example.yml config/application.yml
```

真实 `config/application.yml` 已被 Git 忽略，可在里面设置火山 ASR provider、
API Key 和 Resource ID；仓库只提交 `application.example.yml`。

不要把 Token、Codex 凭据或火山密钥写入 ESP32 源码或提交到 Git。

### 2. 设备 AP 配网

自己的设备也可以使用零操作启动：复制
`include/vela_secrets.example.h` 为 `include/vela_secrets.h`，填写家庭 Wi-Fi
和本机 Bridge 信息后重新烧录。该文件已被 Git 忽略；设备首次启动会直接
联网并进入 AI 页面，不显示配网界面。服务商 API Key 仍然只能配置在 Java
Bridge 服务端，不能写入此文件。

没有本机私有配置、已保存网络失效，或用户主动选择“重新配网”时，设备会
自动进入下面的开放 AP 流程。

首次启动且没有已保存配置时，设备会进入临时开放 AP 模式。圆屏显示：

- `Vela-XXXXXX` AP 名称
- `OPEN - NO PASSWORD`
- 配网页地址，通常是 `192.168.4.1`

操作步骤：

1. 手机连接圆屏显示的 `Vela-XXXXXX`。
2. 等待系统自动弹出 `Vela Link`；没弹出就打开 `http://192.168.4.1`。
3. 填写家庭/办公室 Wi-Fi 名称和密码。
4. Bridge Host 填 Mac 的局域网 IP，不能填 `127.0.0.1`。
5. Port 填 `8787`。
6. Bridge Token 填启动 Java Bridge 时使用的同一个 Token。
7. 保存后设备会关闭 AP，连接 Wi-Fi，再连接本机 Bridge。

手机无需密码即可连接，提交成功后设备会立即关闭临时 AP。已配网设备正常
启动时不会开放 AP；只有首次使用、用户主动选择“重新配网”或清除配置后才
重新开启。Wi-Fi、Bridge 地址与设备访问 Token 使用带 schema 和 CRC 的
Preferences 结构保存；Codex、火山引擎等服务密钥始终只保存在 Java Bridge
服务端，不进入 ESP32 或配网页。

### 3. 选择语音方案

仓库安全默认值可以使用 Codex `localAudio`：

```sh
export VELA_ASR_PROVIDER=codex
```

当前实机已切换并验证火山引擎。除了环境变量，也可以把同名属性写入已忽略的
`bridge/config/application.yml`：

```sh
export VELA_ASR_PROVIDER=volcengine
export VOLCENGINE_ASR_API_KEY='你的 APP Key'
export VOLCENGINE_ASR_RESOURCE_ID='volc.bigasr.auc_turbo'
```

火山密钥只保存在 Mac 的 Spring Boot 本地配置或环境变量中，永远不下发设备。

当前固件采用“先在 TF 卡生成完整 WAV，再上传 Bridge”的可靠路径，并设置
30 秒单句安全上限。若要做长时间听写，不应继续放大 WAV、PSRAM 与 HTTP
请求上限，而应改为持续发送 PCM 分片：ESP32 只连接局域网 Bridge，由 Java
Bridge 维护火山引擎 WebSocket 和云端密钥。2 秒静音仍由设备侧 VAD 判定，
触发结束当前语句并把最终文本发送给 Codex。

## 构建与烧录

### ESP32 固件

```sh
cd vela-dial-ai
pio run
```

构建产物：

```text
.pio/build/vela_dial_ai/firmware.bin
```

设备接好后：

```sh
pio run -t upload --upload-port /dev/cu.usbmodemXXXX
```

当前自定义固件保留 USB CDC，一般重连后可直接烧录。如果串口没有出现，再用
已经在这台样机上验证过的恢复流程：

1. 原厂固件中关闭 `HID Input / USB HID`，保存后完整关机再开机。
2. USB 数据线保持连接，设备关机。
3. 按住 USB-C 左侧隐藏的 BOOT 小扁按钮，同时开机。
4. 保持约 3 秒再松开；黑屏表示 ROM 下载模式。
5. 出现 `/dev/cu.usbmodem...` 后再上传。

更完整的原厂差异与 BOOT 经验在
[旧硬件 Demo 文档](../jc3636k718-platformio-demo/README.md)。

### Java Bridge

```sh
cd vela-dial-ai/bridge
./mvnw test
./mvnw package
```

产物：

```text
bridge/target/vela-dial-bridge-0.1.0-SNAPSHOT.jar
```

## 已完成验证

2026-07-29 至 2026-07-30 验证结果：

- ESP32 固件完整构建成功
- RAM：181,956 / 327,680 bytes（55.5%）
- Flash：2,680,820 / 8,388,608 bytes（32.0%，无本机私有配置的干净构建）
- 实机自动识别到 16 MB Flash 与 8 MB PSRAM
- 屏幕、触摸、旋钮、TF 卡、麦克风、扬声器、灯环与 DRV2605L 初始化通过
- 修复首次 UI 同步的 `loopTask` 栈溢出，并为 Wi-Fi/音频保留内部 DMA 内存
- 内置 Noto Sans CJK SC 16px Flash 字库，开启压缩字体解码，并把 CJK 文本
  与 LVGL 系统图标分开选字体；配合 UTF-8 安全截断消除整段中文方块的根因。
  这项修复已通过编译，仍需下一次实机烧录确认最终像素效果
- 新会话实机录音 3.88 秒，WAV 保存完整且丢帧为 0
- 连续静音 2 秒自动结束录音、上传 Bridge，并成功创建 Codex Thread
- 火山引擎新版 `X-Api-Key` 与 `volc.bigasr.auc_turbo` 实测通过；9.07 秒
  历史录音和最新 3.9 秒录音都成功识别
- 最新语音“你好你好。”已创建 Codex Thread，并取得回复“你好，我在。”
- 录音状态冲突改为等待上一条保存完成后自动开始，不再直接显示
  `Recording unavailable`
- 火山返回 `20000003` 时明确显示“没有听到说话，请重试”，不再笼统报录音失败
- 修复版烧录校验通过，串口连续观察未再出现 panic 或自动重启
- Java 24 项测试全部通过，0 failure / 0 error
- Spring 应用上下文启动测试通过
- Java 21 可执行 JAR 启动成功
- Bridge 成功连接当前 ChatGPT 内置 Codex app-server
- 真实读取到动态额度窗口、账户 Token 与会话数据，并按设备契约返回最近 5 个会话
- Bridge 完整重启后已通过 `thread/read` 恢复最后一条 Codex 回复
- `decline` 在 Codex 仅提供 `cancel` 时会安全映射为 `cancel`；设备端的
  `accept` 不会自动升级成 `acceptForSession`
- 旧的音频转写 Approval 已清空，重启后待审批数为 0

## 安全与可靠性

- 所有设备 API 都要求 Bearer Token；未配置 Token 时 Bridge 拒绝启动
- WAV 在设备端和 Bridge 端分别校验
- 只接受 PCM、mono、24 kHz、16-bit、正确 RIFF/data committed length
- `X-Vela-Wav-Crc32` 与服务端重新计算值必须一致
- 设备持久化稳定 `Idempotency-Key`
- Bridge 持久化 `received → transcribed → codexSubmitted → completed`
- Codex mutation 超时且结果未知时不盲目重发
- 审批使用 HMAC opaque ID、nonce、64 位 action digest 和过期时间
- 待审批为全局 FIFO，不会因会话不在最近 5 个中而丢失
- Java Bridge 和设备都不会记录或下发云端 API Key

## 当前边界

- 实机固件已经完成直接 Wi-Fi、Java Bridge、火山识别、录音上传、新建 Codex
  会话和回复回显联调；业务操作产生的真实允许/拒绝仍建议另做一轮专项验收。
- Bridge 能可靠处理由该 Bridge 发起或恢复后产生的 Codex Turn 审批；另一个
  Codex 客户端连接中已经悬起的审批，不保证会转发到本 Bridge。
- 当前固件内置 Noto Sans CJK SC 16px、2 bpp 的压缩常用字库，覆盖 GB2312
  常用简体字、ASCII 与常用标点；LVGL 已开启
  `LV_USE_FONT_COMPRESSED=1`。生僻字、繁体扩展字和 emoji 仍会回退为缺字符号，
  OFL 许可见 `LICENSES/NotoSansCJK-OFL.txt`。
- 当前 WAV 上传路径单句上限为 30 秒；长时间语音应使用计划中的
  ESP32 → Java Bridge → 火山引擎 WebSocket 流式路径。
- 当前只做编程 AI，没有加入英语、天气、番茄钟和设置应用。
- `open-vibe-island` 用于理解产品状态机和事件优先级；本项目没有复制其 GPL
  源码，是独立实现。第三方源码声明见
  [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)。

## 中文与用量实现依据

### 中文显示

当前版本采用“离线基础字库”方案：`src/vela_cjk_16.c` 保证设备断网时仍能显示
常用中文。UI 只在检测到 CJK 码点时选择该字体，LVGL 的箭头、勾选等系统图标
仍使用原字体，避免把图标误判成中文后显示方块。它适合当前 Demo，但不应把完整
Unicode 字库全部塞进 ESP32 Flash。

商业版建议参考 `xiaozhi-esp32` 的成熟做法：

1. 保留当前压缩常用字库作为离线兜底。
2. Java Bridge 按 Unicode 码点下发缺失字形位图。
3. 固件通过 LVGL font fallback 接入动态字形，并在 PSRAM 中维护 LRU 缓存。
4. 建议缓存 128–256 个字形，预算约 32–64 KiB；缓存未命中也不影响基础界面。

相关实现：

- [xiaozhi-esp32 动态字形缓存](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/display/lvgl_display/dynamic_glyph_cache.h#L11-L19)
- [LVGL fallback 接入](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/display/lvgl_display/lvgl_display.cc#L78-L103)
- [PSRAM 字形分配](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/display/text_glyph.h#L25-L40)
- [字形载荷校验](https://github.com/78/xiaozhi-esp32/blob/5540258abcbfa62518d09959308200be1c5b1b2b/main/protocols/text_glyph_payload.cc#L14-L63)
- [LVGL 字体转换器](https://github.com/lvgl/lv_font_conv)

### 额度与 Token

- [Codex app-server 官方协议](https://github.com/openai/codex/blob/main/codex-rs/app-server/README.md)
  定义了 `account/rateLimits/read`、`account/rateLimits/updated` 与
  `account/usage/read`。
- [`open-vibe-island` 的 CodexUsage](https://github.com/Octane0411/open-vibe-island/blob/6e5e7a6a5b5097ee627a7d4dea6226c128747a71/Sources/OpenIslandCore/CodexUsage.swift)
  同样按 `primary / secondary` 和实际分钟数动态解析额度窗口；其生产 UI 当前
  不提供账户 Token 页。本项目复用的是这一产品思路，不复制源码，并补上官方
  Token 与单会话上下文数据。

## 工程结构

```text
vela-dial-ai/
├── src/
│   ├── app_ui.cpp                 LVGL AI 页面与旋钮状态机
│   ├── bridge_client.cpp          非阻塞设备网络客户端
│   ├── connectivity_service.cpp   Wi-Fi、AP 与 captive portal
│   ├── recording_store.cpp        TF 卡录音幂等与重试
│   ├── board_hardware.cpp         屏幕、音频、灯环、TF 与振动
│   ├── text_utils.h                UTF-8 安全截断
│   ├── vela_cjk_16.c               Noto CJK LVGL Flash 字库
│   └── main.cpp                   安全启动与任务初始化
├── bridge/
│   ├── src/main/java/             Java Bridge
│   ├── src/test/java/             契约、音频与状态测试
│   ├── README.md                  Bridge API 与配置详解
│   └── pom.xml
├── include/lv_conf.h
├── LICENSES/NotoSansCJK-OFL.txt
├── partitions_16mb.csv
└── platformio.ini
```
