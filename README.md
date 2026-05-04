# vibe-codex 启动总手册

这个目录下目前有 3 个彼此独立的前端子项目：

- `bean-pop-studio`
- `canvas-editor-studio`
- `relay-drop`

它们现在放在同一个 Git 仓库里统一管理 CI/CD，但依然不是共享依赖的 package monorepo，没有统一的根级构建脚本；本地开发时仍需要先进入各自目录，再用对应的包管理器安装和运行。

## 1. 先看结论

| 子项目 | 技术栈 | 包管理器 | 开发启动命令 | 默认访问地址 | 备注 |
| --- | --- | --- | --- | --- | --- |
| `bean-pop-studio` | Vue 3 + Vite | `npm` | `npm run dev` | `http://127.0.0.1:4173` | 拼豆图纸生成工具 |
| `canvas-editor-studio` | React 18 + TypeScript + Vite + Konva | `pnpm` | `pnpm dev` | `http://127.0.0.1:4178` | 本地画布编辑器 Demo |
| `relay-drop` | Vue 3 + TypeScript + Vite + PeerJS | `npm` | `npm run dev` | `http://127.0.0.1:4173` | 文件互传原型，同时会启动前端和 PeerJS 信令服务 |

## 2. 环境要求

推荐直接使用较新的 Node.js 版本，避免 Vite 版本差异导致的安装或启动问题。

- 推荐：`Node.js 24.x`
- 兼容建议：不要低于 `Node.js 20.19.0`
- 当前本机环境：`node v24.14.0`、`npm 11.9.0`、`pnpm 10.29.3`

说明：

- `relay-drop` 使用的是 `Vite 8`，对 Node 版本要求更高，建议把它当作全仓库的最低环境基线。
- `canvas-editor-studio` 使用 `pnpm-lock.yaml`，优先用 `pnpm`。
- `bean-pop-studio` 和 `relay-drop` 使用 `package-lock.json`，优先用 `npm`。

如果你的机器没有 `pnpm`，可以先执行：

```bash
corepack enable
```

然后再进入 `canvas-editor-studio` 执行 `pnpm install`。

## 3. 目录结构

```text
vibe-codex/
├─ .github/workflows/              # 仓库级 GitHub Actions
├─ README.md
├─ bean-pop-studio/
├─ canvas-editor-studio/
└─ relay-drop/
```

## 3.1 GitHub Actions

仓库级工作流放在根目录 `.github/workflows/`：

- `ci.yml`
  - 对 3 个项目分别执行构建校验
- `deploy-relay-drop-aliyun.yml`
  - 仅在 `relay-drop/` 或该工作流自身有变更时触发
  - 把 `relay-drop` 构建产物发布到阿里云轻量应用服务器

## 4. 通用启动方式

每个子项目都按这个顺序处理：

```bash
cd /Users/admin/GitHubProjects/vibe-codex/<project-name>
# npm install 或 pnpm install
# npm run dev 或 pnpm dev
```

也就是：

1. 进入目标子项目目录。
2. 用该项目自己的包管理器安装依赖。
3. 执行开发命令。
4. 打开浏览器访问对应端口。

## 5. bean-pop-studio

### 项目用途

这是一个把普通照片快速转换成拼豆图纸的浏览器端工具，主要能力包括：

- 上传图片并生成拼豆成品预览
- 生成制作图纸
- 输出色号清单和颗数统计
- 支持隐藏底色、合并颜色、局部改豆点和下载图纸

### 安装依赖

```bash
cd /Users/admin/GitHubProjects/vibe-codex/bean-pop-studio
npm install
```

### 启动开发环境

```bash
npm run dev
```

默认监听配置来自 `vite.config.js`：

- Host：`0.0.0.0`
- Port：`4173`

本机访问：

```text
http://127.0.0.1:4173
```

### 常用命令

```bash
npm run test
npm run build
npm run preview
```

### 适合什么时候启动它

如果你现在要改的是下面这些内容，就启动这个项目：

- 拼豆图纸生成逻辑
- 图片预处理和风格处理
- 色号映射、颗数统计、图纸导出
- Vue 页面交互和作品保存

### 启动注意事项

- 这是纯前端项目，不需要额外后端服务。
- 如果 `4173` 端口已经被占用，优先检查是不是 `relay-drop` 正在运行。
- 这个项目默认也支持局域网访问，因为 Vite 监听在 `0.0.0.0`。

## 6. canvas-editor-studio

### 项目用途

这是一个本地画布编辑器 Demo，偏海报/KV 编辑器方向，当前重点在基础图层编辑流程，包括：

- 背景图、底色、覆盖层编辑
- 文字图层新增、选中、原位编辑
- 图片图层上传、替换、尺寸调整
- 图层显隐、锁定、排序、复制、删除
- PNG / JSON 导出
- 撤销 / 重做

### 安装依赖

```bash
cd /Users/admin/GitHubProjects/vibe-codex/canvas-editor-studio
pnpm install
```

### 启动开发环境

```bash
pnpm dev
```

默认监听配置来自 `vite.config.ts`：

- Host：`0.0.0.0`
- Port：`4178`

本机访问：

```text
http://127.0.0.1:4178
```

### 常用命令

```bash
pnpm build
```

### 适合什么时候启动它

如果你现在要改的是下面这些内容，就启动这个项目：

- 画布编辑器交互
- React / TypeScript 组件行为
- Konva 图层绘制和拖拽
- JSON 导入导出协议

### 启动注意事项

- 这个项目和另外两个项目端口不冲突，默认是 `4178`。
- 它是独立项目，不依赖仓库内其他子项目。
- 如果 `pnpm` 不存在，先执行 `corepack enable`。

## 7. relay-drop

### 项目用途

这是一个“扫码进会话，连上就互传”的 Web 文件互传原型，主要场景是电脑和手机之间临时传文件。当前核心链路是：

- Host 打开页面建会话
- 页面生成二维码
- Guest 扫码进入同一房间
- 双方通过 `PeerJS + WebRTC DataChannel` 直连传文件

### 安装依赖

```bash
cd /Users/admin/GitHubProjects/vibe-codex/relay-drop
npm install
```

### 一键启动前端 + PeerJS

```bash
npm run dev
```

这个命令会同时启动两个进程：

- 前端页面：`npm run web`
- PeerJS 信令服务：`npm run peer`

默认端口：

- Web：`4173`
- PeerJS：`9000`
- PeerJS Path：`/peerjs`

本机访问：

```text
http://127.0.0.1:4173
```

### 分开启动

如果你只想调某一部分，也可以拆开启动：

```bash
npm run web
npm run peer
```

### 常用命令

```bash
npm run build
npm run preview
```

### 适合什么时候启动它

如果你现在要改的是下面这些内容，就启动这个项目：

- 扫码进房间流程
- PeerJS / WebRTC 连接逻辑
- 文件分片发送与接收
- 双端会话 UI
- 真机扫码和局域网联调

### 启动注意事项

- 这个项目默认占用 `4173`，和 `bean-pop-studio` 冲突，二者不要同时按默认端口启动。
- `npm run dev` 必须保证 `9000` 端口可用，否则 PeerJS 信令服务起不来。
- 如果你要让手机扫码访问，不能继续用 `localhost` 地址，必须改成当前电脑的局域网 IP。
- 当前页面逻辑实际由 `src/recovered-app.js` 驱动，`src/App.vue` 只是占位；排查问题时不要只看 `App.vue`。

## 8. 端口冲突说明

这个目录里最容易踩的坑就是端口重复：

- `bean-pop-studio` 默认是 `4173`
- `relay-drop` 的前端默认也是 `4173`
- `canvas-editor-studio` 默认是 `4178`

所以：

- `bean-pop-studio` 和 `relay-drop` 不能同时直接按默认配置启动。
- `canvas-editor-studio` 可以和它们任意一个同时启动。

如果你确实需要同时跑 `bean-pop-studio` 和 `relay-drop`，建议临时修改其中一个项目的 Vite 端口后再启动。

## 9. 推荐启动场景

### 只看单个项目

按对应项目自己的命令启动即可，不需要先启动别的项目。

### 同时开两个项目

推荐组合：

- `canvas-editor-studio` + `bean-pop-studio`
- `canvas-editor-studio` + `relay-drop`

不推荐直接同时启动：

- `bean-pop-studio` + `relay-drop`

原因是它们默认都抢 `4173`。

## 10. 常见问题排查

### 1. 安装依赖时报 Node 版本不兼容

先执行：

```bash
node -v
```

如果低于 `20.19.0`，先升级 Node 再安装，优先直接切到 `24.x`。

### 2. `pnpm: command not found`

执行：

```bash
corepack enable
```

然后重试：

```bash
pnpm install
```

### 3. 页面起不来，提示端口被占用

先看是不是已经有别的子项目在跑：

- `4173` 常见冲突：`bean-pop-studio` 和 `relay-drop`
- `9000` 常见冲突：其他本地 PeerJS 或 Node 服务

### 4. `relay-drop` 手机扫不到或扫进去连不上

优先排查这几项：

- 页面是不是还开在 `localhost`
- 手机和电脑是不是在同一局域网
- `9000` 端口的 PeerJS 信令服务是不是已经成功启动
- 浏览器是否拦截了相关网络或文件权限

## 11. 建议的使用方式

如果你的目标是“先知道这个仓库怎么跑起来”，最省事的顺序是：

1. 先启动 `canvas-editor-studio`，它最纯粹，依赖最简单，端口也独立。
2. 再启动 `bean-pop-studio`，确认 Vue 项目链路正常。
3. 最后启动 `relay-drop`，因为它除了前端，还涉及一个本地 PeerJS 服务和真机联调。

## 12. 子项目原始说明

如果你需要更细的产品设计、实现细节和目录解释，可以继续看各自目录下原有文档：

- [bean-pop-studio/README.md](./bean-pop-studio/README.md)
- [canvas-editor-studio/README.md](./canvas-editor-studio/README.md)
- [relay-drop/README.md](./relay-drop/README.md)
