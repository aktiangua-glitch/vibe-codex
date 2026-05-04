# RelayDrop 市场与竞品调研

调研日期：`2026-05-04`

## 结论先说

`P2P / 跨平台 / 扫码传文件` 这个市场依然存在，但不是空白市场。

现在真正有需求的，不是“再做一个网盘”或者“再做一个只能局域网发现的工具”，而是下面这条路径：

- 电脑打开即亮码
- 手机扫码即进入
- 不要求安装 App
- 不要求注册登录
- 进来就能把文件直接送到当前设备

这条路径仍然有价值，尤其适合：

- 临时跨系统传文件
- 手机给电脑发照片 / 视频 / 文档
- 微信里快速打开
- 用户不愿安装客户端的轻量场景

但竞争也很明确：浏览器型产品、原生局域网产品、带链接的跨网分享产品都已经存在。

## 竞品分层

### 1. 浏览器直传型

代表产品：

- [PairDrop](https://github.com/schlagmichdoch/PairDrop)
- [Snapdrop](https://github.com/SnapDrop/snapdrop)

特点：

- 免安装
- 打开网页就能用
- 强依赖 WebRTC / WebSocket / 浏览器能力
- 非常适合“临时、面对面、轻量”的场景

### 2. 本地网络原生型

代表产品：

- [LocalSend](https://localsend.org/zh-CN)
- [LANDrop](https://github.com/LANDrop/LANDrop)

特点：

- 速度和稳定性通常强于纯浏览器
- 更适合大文件和高频用户
- 往往需要安装客户端
- 产品心智更偏“工具软件”而不是“即开即用网页”

### 3. 跨网链接 / 服务端兜底型

代表产品：

- [Wormhole](https://wormhole.app/)
- [AltSendme](https://github.com/tonyantony300/alt-sendme)

特点：

- 不局限于同一局域网
- 更适合异步发送和远程传输
- 更容易做企业版、专业版、传输保障
- 信任感、隐私说明、产品文案会比“局域网工具”更重要

## 核心竞品表

| 产品 | 当前定位 | 核心卖点 | 对 RelayDrop 的启发 |
| --- | --- | --- | --- |
| [PairDrop](https://github.com/schlagmichdoch/PairDrop) | 浏览器直传 / PWA / 可自托管 | 跨平台、免安装、免注册，还支持公网房间、配对设备、二维码进入 | RelayDrop 的直接对标。它证明“网页即用”有长期需求，但也说明只做局域网发现已经不够，公网房间和设备配对是自然演进方向。 |
| [Snapdrop](https://github.com/SnapDrop/snapdrop) | 浏览器本地分享 | 经典的“浏览器里本地互传”心智入口 | 它验证了网页互传的普适性；同时它后来被 LimeWire 收购，也说明浏览器传输品牌本身可以被整合进更大的文件平台。 |
| [LocalSend](https://localsend.org/zh-CN) | 原生局域网互传 | 无需云端、快速、私密、离线、无广告 | 说明“隐私 + 无广告 + 本地网络”仍然是非常强的用户价值；但它依赖安装，给 RelayDrop 留出了“免安装”的空间。 |
| [LANDrop](https://github.com/LANDrop/LANDrop) | 原生 LAN 互传 | 局域网高速、跨平台、无压缩 | 说明“高速本地传输”一直有需求；但它的开源仓库更新滞后，也提示这个方向如果没有持续迭代，很容易失去产品吸引力。 |
| [Wormhole](https://wormhole.app/) | 带链接的私密分享 | 简单、私密、端到端加密、链接自动过期、免费支持到 10 GB | 证明“跨网 + 链接 + 隐私”是另一条稳定需求线。它和 RelayDrop 不同，但可作为后续“跨网分享模式”的参考。 |
| [AltSendme](https://github.com/tonyantony300/alt-sendme) | 更激进的 P2P / 跨网直传 | 不存云端、任意格式、无账号、无大小限制，底层强调 QUIC / hole punching / resumable | 它说明“更成熟的 P2P 技术故事”对极客和重度用户有吸引力。RelayDrop 可以先做轻量网页入口，后续再决定是否往这一层技术能力升级。 |

## 首页文案参考

下面这些文案值得参考，不是因为要照搬，而是因为它们都非常直接，不解释技术，不绕弯子。

| 产品 | 文案方向 | 对应启发 |
| --- | --- | --- |
| PairDrop | `Transfer Files Cross-Platform. No Setup, No Signup.` | 一句话直接打“跨平台 + 低门槛” |
| LocalSend | `无需云端即可分享文件。快速、私密、离线。` | 先讲结果，再讲信任 |
| Wormhole | `Simple, private file sharing` | 文案极短，但把“简单”和“私密”都打到了 |
| Snapdrop | `local file sharing in your browser` | 浏览器这个入口本身就是卖点 |
| AltSendme | `without storing in cloud ... no accounts, no restrictions` | 面向重度用户时，隐私和限制条件可以变成主卖点 |

## 从竞品看，RelayDrop 该怎么打

### 最该坚持的产品重点

- `扫码进会话`
- `连上就互传`
- `当前设备直接收`
- `不装 App 也能发`

### 最该避免的表达

- 解释太多底层协议
- 首页同时塞太多场景
- 一上来就讲“P2P / WebRTC / NAT / 信令”
- 首屏被广告位破坏信任感

用户真正要的是：

- 现在能不能传
- 要不要装东西
- 微信里能不能直接开
- 文件会不会跑丢

## 商业化判断

下面这一段是基于上面竞品现状做的推断，不是官网原文。

### 1. 只靠标准展示广告，不是最优解

原因很直接：

- 文件传输是强任务型行为，用户停留时间短
- 首页的核心操作只有一个，广告很容易破坏转化
- 文件传输场景非常依赖信任，广告会削弱“安全感”

### 2. 竞品更常见的模式不是 banner ad

从公开信息看，更常见的是：

- 捐赠 / 赞助
  - PairDrop
  - LocalSend
  - LANDrop
- 平台化整合
  - Snapdrop -> LimeWire
- 更强的产品化 / 订阅空间
  - Wormhole
  - AltSendme 的技术栈也明显给“高级能力”留了空间

### 3. RelayDrop 更合理的商业化路径

建议优先级：

1. 免费基础版
2. 企业 / 团队版
3. 自部署版
4. 公网加速 / 中转保障
5. 品牌页 / 自定义域名 / 团队设备管理

如果一定要尝试广告：

- 不建议首屏大广告
- 可以考虑结果页、下载完成页、帮助页、博客页的轻广告
- 更适合做“工具赞助位”而不是通用广告联盟位

## 对产品路线的实际建议

### 第一阶段

- 守住“网页即开即用”
- 把扫码进入会话做到极稳
- 把微信内打开和手机浏览器体验做好

### 第二阶段

- 加入公网复杂网络兜底
- 支持会话配对 / 常用设备
- 支持更长时间的异步会话

### 第三阶段

- 做企业版、自托管版
- 做团队设备、组织内互传
- 做品牌化文件入口页

## 主要参考来源

- PairDrop GitHub：<https://github.com/schlagmichdoch/PairDrop>
- LocalSend 官网：<https://localsend.org/zh-CN>
- Snapdrop GitHub：<https://github.com/SnapDrop/snapdrop>
- Wormhole 官网：<https://wormhole.app/>
- LANDrop GitHub：<https://github.com/LANDrop/LANDrop>
- AltSendme GitHub：<https://github.com/tonyantony300/alt-sendme>

## 备注

本调研只覆盖与 `RelayDrop` 最相关的“跨平台互传”方向，不包含网盘、企业 IM 附件、传统大文件投递平台的完整横评。
