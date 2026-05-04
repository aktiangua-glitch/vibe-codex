# Canvas Editor Studio

`Canvas Editor Studio` 是一个可独立运行的本地编辑器 Demo，用来验证海报/KV 类画面的基础编辑流程。

当前版本聚焦这些能力：

- 背景图、底色、覆盖层的独立编辑
- 文字图层的新增、选中、原位编辑与排版调整
- 图片图层的上传、替换、尺寸与样式调整
- 图层显隐、锁定、排序、复制、删除
- PNG / JSON 导出
- 撤销 / 重做

## 技术栈

- Vite
- React 18
- TypeScript
- Konva
- react-konva

## 本地运行

```bash
pnpm install
pnpm dev --host 127.0.0.1 --port 4178
```

打开 [http://127.0.0.1:4178](http://127.0.0.1:4178)

## 构建

```bash
pnpm build
```

## 项目结构

```text
canvas-editor-studio/
├─ src/
│  ├─ App.tsx
│  ├─ presets.ts
│  ├─ styles.css
│  ├─ types.ts
│  ├─ useLoadedImage.ts
│  └─ assets/
├─ reference/
├─ index.html
├─ package.json
└─ vite.config.ts
```

## 数据结构

编辑器主文档结构定义在 `src/types.ts`，核心是一个 `EditorDocument`：

```ts
type EditorDocument = {
  canvas: {
    width: number;
    height: number;
  };
  background: {
    color: string;
    imageSrc?: string;
    overlayColor: string;
    overlayOpacity: number;
    imageOpacity: number;
    fit: 'cover' | 'contain';
  };
  layers: Array<TextLayer | ImageLayer>;
};
```

这份 JSON 可以直接作为后续编辑器协议的基础，用于：

- 场景保存与恢复
- 批量属性修改
- 图层级 AI 操作扩展
- 导出与导入

## 后续扩展建议

如果要继续做 AI 图层编辑，建议优先在 `ImageLayer` 上补这些字段：

- `assetId`
- `originalSrc`
- `versions`
- `aiSession`
- `mask`

这样可以在不推翻现有结构的前提下，逐步接入：

- AI 替换图片
- 局部重绘
- 抠图 / 蒙版编辑
- 图层版本回滚

## 说明

- 这是一个独立项目，不依赖外部 monorepo 包。
- `reference/editor-studio-snapshot` 仅作为本地参考快照保留，不参与运行时依赖。
