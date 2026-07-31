# Pindou Pattern Skill

把一张图片转成拼豆图纸 PNG。算法复用 `bean-pop-studio` 的推荐尺寸、推荐颜色数、主体取景、清爽描边预处理、CIEDE2000 近色匹配和现有下载图纸渲染。

## 色卡

不要在 skill 里写死或改动颜色。色卡读取自：

```text
bean-pop-studio/src/data/palettes.json
```

构建时会同步到：

```text
bean-pop-studio/public/palettes.json
```

当前默认品牌是 `MARD`，按已校对的 `MARD 221` 色卡运行。

## 网页脚本

本地开发或线上构建后可引入：

```html
<script src="/skill/pindou-pattern-skill.js"></script>
```

最小调用：

```html
<input id="photo" type="file" accept="image/*" />
<script src="/skill/pindou-pattern-skill.js"></script>
<script>
  document.querySelector("#photo").addEventListener("change", async (event) => {
    const file = event.target.files[0];
    const result = await window.PindouPatternSkill.generate(file);

    console.log(result.width, result.height, result.totalBeads, result.counts);
    document.body.append(result.exportCanvas);
  });
</script>
```

直接下载：

```js
await window.PindouPatternSkill.download(file, {
  projectName: "我的拼豆图纸",
});
```

## 可选参数

```js
await window.PindouPatternSkill.generate(file, {
  brand: "MARD",
  targetWidth: 87,
  maxColors: 24,
  projectName: "peach soda",
  showCodes: true,
  roundBeads: true,
  styleMode: "clean_ink",
  styleIntensity: 0.72,
});
```

不传 `targetWidth` 和 `maxColors` 时，会使用推荐算法自动计算。
