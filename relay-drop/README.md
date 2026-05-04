# RelayDrop

基于 `Vue 3 + Vite + TypeScript` 的跨平台文件互传 Web 原型。

这个项目的目标很聚焦：电脑打开页面亮出会话二维码，对方手机扫码后进入同一条会话，随后两边都能直接互传文件，不需要安装 App，也不需要登录。

## 产品定位

- 品牌名：`Relay`
- 项目目录：`/Users/admin/GitHubProjects/vibe-codex/relay-drop`
- 核心卖点：`扫码进会话，连上就互传`
- 典型场景：手机给电脑发照片、跨系统临时传文档、微信里快速打开后直接送到当前设备

## 当前能力

- 单页首屏落地页，主任务入口直接可用
- Host / Guest 双角色会话模型
- 基于二维码的会话进入
- 基于 `PeerJS + WebRTC DataChannel` 的双向直传
- 文件队列、进度、完成态、下载态
- 断开后的 Guest 自动重连
- `localhost` 场景下的真机扫码提示

## 技术实现

### 会话模型

- Host 打开首页时生成 `roomId`
- 会话二维码内容为 `当前页面地址 + ?room=<roomId>`
- Guest 扫码后以 `guest` 角色进入，并主动连接 Host
- Host 的固定 Peer ID：`relay-session-<roomId>`
- Guest 的临时 Peer ID：`relay-peer-<roomId>-<random>`

### 网络链路

- 信令层使用本地 `PeerJS Server`
- 默认开发端口：`9000`
- 默认路径：`/peerjs`
- 文件数据本身走 `WebRTC DataChannel`，不经过信令服务
- 当前 STUN 配置：
  - `stun:stun.l.google.com:19302`
  - `stun:stun.cloudflare.com:3478`

### 传输协议

DataChannel 上使用一套轻量消息协议同步会话和文件状态：

- `hello`
  - 交换设备名称和角色
- `manifest`
  - 声明本批文件列表和总字节数
- `file-start`
  - 某个文件开始发送
- `file-chunk`
  - 发送文件分片
- `file-end`
  - 某个文件发送结束
- `transfer-complete`
  - 当前批次发送完成

当前分片大小为 `24 KB`。

### 状态管理

会话状态围绕以下枚举流转：

- `booting`
- `ready`
- `connected`
- `transferring`
- `done`
- `error`

页面逻辑已经改为依赖响应式状态，而不是直接读取 `dataConnection.open` 这类非响应式字段，所以对端加入、断开、重连后，按钮、弹窗和传输区域都会立即同步。

## 项目结构

当前源码已经整理回正常的 Vue 工程结构，不再依赖恢复态 bundle：

```text
relay-drop/
├─ docs/
│  └─ market-research.md
├─ public/
├─ src/
│  ├─ components/
│  │  ├─ BrandChip.vue
│  │  ├─ GuestPanel.vue
│  │  ├─ HeroPanel.vue
│  │  ├─ ShareDialog.vue
│  │  └─ StagePanel.vue
│  ├─ composables/
│  │  └─ useRelayTransfer.ts
│  ├─ styles/
│  │  └─ relay.css
│  ├─ types/
│  │  └─ relay.ts
│  ├─ utils/
│  │  └─ relay.ts
│  ├─ App.vue
│  ├─ main.ts
│  └─ qrcode.d.ts
├─ package.json
├─ tsconfig.app.json
└─ vite.config.ts
```

### 目录职责

- `src/App.vue`
  - 页面装配层，只负责拼装 guest / host / dialog 三块视图
- `src/components`
  - 首屏、舞台区、扫码弹窗、guest 端视图等展示组件
- `src/composables/useRelayTransfer.ts`
  - 会话状态、Peer 连接、二维码、收发文件、进度同步等核心交互逻辑
- `src/utils/relay.ts`
  - 房间 ID、文件展示、预览数据、格式化等纯函数
- `src/types/relay.ts`
  - 会话消息、传输记录、方向和状态等类型定义
- `src/styles/relay.css`
  - 全局样式与动效

## 本地开发

安装依赖：

```bash
npm install
```

启动前端和 PeerJS：

```bash
npm run dev
```

只启动前端：

```bash
npm run web
```

只启动 PeerJS：

```bash
npm run peer
```

只做类型检查：

```bash
npm run typecheck
```

生产构建：

```bash
npm run build
```

构建后本地启动一体化服务：

```bash
npm start
```

## 已知限制

- 目前依赖浏览器直连，复杂网络环境下成功率不如带中转的产品
- 还没有接入 TURN 中继
- 还没有做断点续传
- 还没有做后台传输和页面关闭恢复
- 收到文件后当前以浏览器下载链接形式落地

## 后续建议

### P0

- 接入 TURN / 中转兜底
- 补真机扫码、微信内打开、iOS Safari 回归
- 为会话建立和重连补更明确的状态反馈

### P1

- 单文件取消、失败重发
- 常用设备 / 配对设备
- 更稳定的大文件传输策略

### P2

- 企业版 / 自部署版
- 传输历史
- 团队设备页和品牌化入口页

## 市场与竞品调研

调研文档在：

- [docs/market-research.md](./docs/market-research.md)

目前的高层结论：

- 这个市场还有机会，但不是空白市场
- 真正有价值的是 `免安装 + 扫码即进 + 当前设备直收`
- 更合理的商业化方向通常不是首页展示广告，而是企业版、自部署版、加速服务或轻量赞助位

主要参考产品：

- [PairDrop](https://github.com/schlagmichdoch/PairDrop)
- [Snapdrop](https://github.com/SnapDrop/snapdrop)
- [LocalSend](https://localsend.org/zh-CN)
- [LANDrop](https://github.com/LANDrop/LANDrop)
- [Wormhole](https://wormhole.app/)
- [AltSendme](https://github.com/tonyantony300/alt-sendme)
