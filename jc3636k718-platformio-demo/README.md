# JC3636K718 PlatformIO + LVGL 完整硬件 Demo

这是为 JC3636K718 旋钮屏制作的完整板级验证项目，使用：

- Cursor + PlatformIO
- Arduino Framework
- LVGL 8.4
- JPEGDEC 1.8.4

项目已经按这块实物的引脚和启动参数配置好。它不仅检查屏幕和旋钮，还覆盖
触摸、振动、RGB、麦克风频谱、WAV 录音、音频输出、TF 卡、JPG/MJPEG、
Wi-Fi、BLE、Flash、PSRAM，以及可选 USB HID / USB TF 磁盘。

> **文档适用范围**
>
> 这里的“已确认”是指当前这一台 JC3636K718 实物、当前厂家资料包和本项目
> 默认 `jc3636k718_msc` 固件的组合，不代表所有采购批次天然一致。原厂出货
> 固件、厂家 Arduino 示例、本硬件 Demo 和将来的商业固件是四个不同基线；
> 换批次、换屏幕模组或换库版本后都要重新跑实机检查清单。
> 当前这台样机的记录日期为 **2026-07-28**。

## 最快体验：封装样机无需拆 TF 卡

如果设备现在运行原厂程序：

1. 保持设备开机并连接数据 USB-C 线。
2. 在原厂界面进入 `SETTING -> SYSTEM -> Reboot to MSC`；部分版本会直接
   显示 `USB MSC`。
3. 设备重启后，macOS Finder 应出现名为 `N7 Disk` 的磁盘。
4. 把本项目 `tf_card_ready` 里面的 `pic` 和 `mjpeg` 两个文件夹复制到
   该磁盘根目录。
5. 在 Finder 中点击“推出 N7 Disk”，等待推出完成。
6. 让设备退出原厂 MSC 模式，按后面的“原厂首次烧录实机流程”关闭 HID、
   进入黑屏下载状态，再使用出现的 USB 串口烧录：

```sh
pio run -e jc3636k718_msc
pio run -e jc3636k718_msc \
  -t upload \
  --upload-port /dev/cu.usbmodemXXXX
```

`jc3636k718_msc` 是适合当前封装样机验证的完整 Demo：正常运行时由设备读取 TF；
需要传文件时，在 `STORAGE` 页面点击 `ENABLE USB`，电脑就会把内部 TF 卡
显示为磁盘。复制完成后必须先在电脑上推出，再回到屏幕点击 `DISCONNECT`。
这套运行时交接已经在本机走通，但还不是可以直接出货的存储方案；强制拔线、
电脑休眠、异常掉电和多种 TF 卡的容错仍需做产品级验证。

把 `XXXX` 替换成电脑实际显示的端口。第一次从原厂程序烧录时如果没有端口，
按后面的完整流程操作。不需要拆开外壳或拔出 TF 卡。

正确的 TF 卡结构必须是：

```text
TF 卡根目录
├── pic
│   └── demo.jpg
├── mjpeg
│   └── demo.mjpeg
└── recordings
    └── REC_0001.WAV
```

`recordings` 目录不需要预先创建，第一次录音时 Demo 会自动建立。不要把外层
`tf_card_ready` 文件夹整个套进去，否则板子会在错误的位置查找图片和视频。

## 已确认的硬件

- MCU：ESP32-S3，双核 240 MHz
- Flash：16 MB
- PSRAM：8 MB OPI
- LCD：ST77916，360 × 360，QSPI
- 触摸：CST816S，I2C
- 输入：旋转编码器
- 振动：DRV2605L + LRA
- 麦克风：PDM 数字麦克风
- 音频输出：I2S + PCM5100A
- RGB：当前 Demo/实机效果按 13 个可寻址像素配置
- 存储：4-bit SD_MMC TF 卡
- USB：ESP32-S3 原生 USB

厂家示例和原理图中的关键引脚如下；本工程已按同一映射实现：

| 功能 | 引脚 |
| --- | --- |
| 旋钮 | A = GPIO2，B = GPIO1 |
| RGB 灯环 / BOOT 复用 | DATA = GPIO0 |
| PDM 麦克风 | CLK = GPIO5，DATA = GPIO4 |
| 电池采样 | ADC = GPIO6 |
| 触摸 / 触觉共享 I²C | SDA = GPIO9，SCL = GPIO10；触摸 INT = 7，RST = 8 |
| LCD QSPI | CLK = 11，CS = 12，D0–D3 = 13–16，RST = 17，TE = 18，BL = 21 |
| 原生 USB | D- = GPIO19，D+ = GPIO20 |
| 扬声器链路 | BCLK = 3，WS = 45，DOUT = 42，PA = 46 |
| TF 4-bit SDMMC | CLK = 39，CMD = 38，D0 = 40，D1 = 41，D2 = 48，D3 = 47 |

本机实测可靠的 Flash 参数是 **DIO 80 MHz**。不要因为资料截图而把 Flash
强制改为 QIO；这块实物在 QIO 下会早期看门狗重启，常见表现是黑屏和
`TG0WDT_SYS_RST` 循环。

`board_build.arduino.memory_type = dio_opi` 明确表示 DIO Flash +
OPI PSRAM，和这块实物一致。

厂家 Arduino 配置截图使用 QIO 120 MHz，而当前实物使用该配置会在应用启动
早期触发看门狗复位。商业化时不能只照资料截图，应固定主板/Flash 料号和
固件参数，并对每一采购批次抽检冷启动、读写和长时间运行。

厂家原理图还显示：

- BOOT 是瞬时开关 `SW1`，按下时把 GPIO0 拉到 GND；它不是需要一直保持
  “打开”的工作模式。
- GPIO0 同时被用作灯环的 `RGB_DATA`。上电或复位时按住 BOOT 会进入
  下载模式；正常启动后应松开，否则也会干扰灯环数据。
- 商业外壳应保留受控的下载/维修触点，但不要把 BOOT 当普通用户按键。量产
  PCB 还应确认背光使能的硬件默认态，避免 MCU 程序运行前背光意外点亮。

### 开机画面标准

旧版 Demo 会先出现一下雪花再进入界面。原因是 ST77916 上电后的显存内容未
初始化，而背光在 LVGL 第一帧完成前就亮了；这不是屏幕损坏，但属于应修复的
启动时序缺陷。

当前代码在 ESP-IDF 应用启动早期、Arduino `setup()` 之前就把 GPIO21 背光和
GPIO17 LCD 复位拉低；进入 `setup()` 后再次钳位，只在黑屏期间完成 200 ms
电源稳定等待和 LCD/LVGL 显示初始化。ST77916 初始化表不再提前发送
`DISPLAY ON (0x29)`：先把内置的 `JC / 3636 / K718` Logo、旋转圆环和进度条
完整刷入，等待最后一次异步 DMA 结束，然后才开启控制器输出和背光。若首帧
刷新超时，固件会继续保持黑屏并报告错误，而不是主动露出随机显存。

Logo 出现后，非 LVGL 工作由 Core 0 的单个后台任务依次完成触摸/旋钮、板级
硬件和 TF/媒体初始化；LVGL 始终只在 Core 1 的 Arduino 主任务运行，每约
5 ms 刷新一次。触摸和 DRV2605L 虽共用 I²C，但后台任务按顺序初始化，直到
任务结束才向 LVGL 注册输入，因此不会并发争用总线。状态文字会显示
`STARTING INPUT`、`CHECKING HARDWARE`、`INDEXING MEDIA` 等阶段。

为缩短真正的启动时间和消除动画停顿，还做了这些调整：

- 删除原来额外的 650 + 200 ms 固定等待，只保留厂家基线的 200 ms。
- 将显示与输入初始化拆开，先显示 Logo，再执行 CST816S 约 400 ms 的复位；
  同时避免 ST77916 被重复硬复位一次。
- 阶段变化只刷新文字和进度条，不再强制重刷 360 × 360 整屏。
- 把原来单个 72 行 DMA 缓冲拆成两个 36 行缓冲，内存总量不变，让 LVGL
  绘制下一块时 QSPI 可以发送上一块。
- RGB565 16 位配置不支持对整个启动页做透明图层淡出。旧代码的“淡出”实际
  可能不绘制，却仍空等约 265 ms。当前改为：先把旋转动画停在静态 `READY`
  帧，后台创建首页，再用约 70–100 ms 的 PWM 背光过渡进入黑场；黑场中
  完整刷新首页并等待 DMA 结束，最后快速恢复到 88% 亮度。

正确冷启动表现应是：
**短暂黑屏 -> Logo/进度动画 -> 很短的暗场换帧 -> 完整 Demo**。不应出现
随机色块、半帧界面、Logo/首页来回闪，动画也不应在外设初始化阶段停住。

临时 Logo 全部由 `src/startup_screen.cpp` 的 LVGL 图形和内置字体生成，不依赖
TF 卡或外部图片，所以无卡、坏卡时仍能显示。正式品牌确定后，可以在该文件
替换文字、颜色和动效；若改成位图，应把小尺寸 RGB565 资源编译进固件，不要
让产品开机 Logo 依赖可拔插 TF 卡。

软件只能从第二阶段 bootloader 装入应用后控制背光，不能覆盖 ESP32-S3 ROM
和 bootloader 自身的时间窗。厂家原理图第 4 页显示 GPIO21/LCD_BLK 所控制的
Q2 栅极应由 R54 10 kΩ 下拉，上电默认本应灭灯。如果真正拔掉 USB、关机等待
10 秒后再冷启动仍从第一瞬间亮出雪花，需要检查实物 R54 是否漏焊/开路、是否
存在 USB/电池回灌，以及 GPIO17 复位默认态；量产版应以硬件默认下拉/门控或
受控 bootloader 早期钳位解决，不能只依赖应用代码。

### 2026-07-28 最终启动实测结论

最终双缓冲版本已经烧入当前样机，并完成真正的关机、开机观察。用户实机确认：

- 开机不再出现雪花、随机色块或中间黑屏，点亮后直接进入 Logo 动画。
- Logo 动画连续，进入首页顺畅，不再出现首页先闪出、Logo 又回来或末端停顿。
- CST816S、旋钮、DRV2605L、TF、PDM 麦克风、I2S 扬声器和 RGB 均继续
  初始化成功；TF/媒体状态为 `READY`。
- 默认 `jc3636k718_msc` 的 1200 baud 自动下载仍正常，不需要再次按 BOOT。

同一台样机、同一张 TF 卡的串口对比如下。数字从 `setup()` 开始计时，不包含
ROM、第二阶段 bootloader 和电脑 USB 枚举时间：

| 指标 | 修复前实测 | 最终版实测 | 结论 |
| --- | ---: | ---: | --- |
| 启动画面最大 LVGL handler 间隔 | 136–138 ms | 38 ms | 阶段切换不再明显冻结 |
| 隐藏状态首页完整绘制 | 未单独记录；同版整屏刷新为 136–138 ms | 90 ms | 双缓冲版的隐藏整帧刷新明显缩短 |
| TF/媒体就绪 | 约 1.70–1.75 s | 1.699 s | 后台化没有拖慢媒体就绪 |
| 首页对象创建完成 | 约 1.89–1.94 s | 1.841 s | 约 126 ms，在静态 READY 帧后完成 |
| 启动页可见时间 | 约 1.72 s | 1.659 s | 动画更短且连续 |
| 完整首页可见 | 约 2.19–2.24 s | 2.181 s | 保留安全首帧的同时没有额外空等 |
| 启动页与首页重叠时 LVGL 内存 | 约 34 KB 可用 | 34,040 B、0% 碎片 | 当前 48 KB LVGL heap 仍有余量 |

这些数据只代表当前样机，不应直接外推到所有采购批次。量产验收还要覆盖纯电池
冷启动、USB 供电、快速复位、低电量、棕断电、不同 TF 卡和连续开关机。

### 这次真正踩到的启动坑

1. **把慢外设初始化放在 LVGL 线程。** CST816S 复位约占 400 ms，TF 挂载、
   目录扫描和其他板级初始化也会阻塞；即使屏幕上已经有 spinner，它也不会
   自动动。最终把所有非 LVGL 初始化放到 Core 0 的单个后台任务，LVGL 只留在
   Core 1，每约 5 ms 运行一次 handler。
2. **后台化后仍强制整屏刷新。** 每次状态变化都调用
   `lv_obj_invalidate(360x360) + lv_refr_now()`，实测一次可制造
   136–138 ms 间隔。状态切换现在只让 label/bar 自己失效，交给下一次普通
   handler 刷新。
3. **在 RGB565 上给整页做透明淡出。** 当前配置是 16 位色且
   `LV_COLOR_SCREEN_TRANSP=0`，LVGL 8.4 不能为整个 360 × 360 root 建立所需的
   alpha layer。旧代码看似调用淡出，实际可能没有正确动画，却仍白等约
   265 ms。最终改成 PWM 背光短过渡，在背光关闭期间完整刷新首页并等待最后
   一次 DMA，然后恢复亮度。
4. **首页和 Logo 的层级顺序。** `app_ui_begin()` 创建的新 root 默认在 Logo
   上方；如果中间运行一次 handler，就可能出现“首页闪出 -> Logo 回来 ->
   首页”。最终先停在静态 `READY` 帧，在任何刷新前把 Logo 保持在前景，黑场
   内再一次性切到首页。
5. **单缓冲让绘制和 QSPI DMA 串行。** 原来一个 72 行缓冲必须等传输结束才能
   继续画。现在改成两个 36 行 DMA 缓冲，总内存仍约 52 KB，但可边传上一块、
   边画下一块；第二块分配失败时仍能自动退化成单缓冲启动。
6. **屏幕在安全首帧前已经 DISPLAY ON。** ST77916 初始化表原先末尾包含
   `0x29`，会在首帧之前开启控制器输出。现在只保留 `0x11 + 120 ms`，完整
   Logo 帧刷完后才发送 `0x29`，最后开启背光。
7. **只在 Arduino `setup()` 里关背光仍偏晚。** 应用载入后还要先初始化
   Arduino/USB 服务。最终使用 ESP-IDF startup hook，在 Arduino loop task
   创建前把 GPIO21 背光和 GPIO17 LCD reset 拉低，`setup()` 开头再重复钳位。
8. **串口打开了却没有启动日志。** 默认 MSC/HID 的 `Serial` 是 TinyUSB CDC，
   主机未置 DTR 时早期输出不会缓存。正确监视参数是
   `115200, DTR=1, RTS=0`；1200 只用于故意进入 ROM 下载器。

### 可复用的工程结论

- 开机动画“卡”不一定是动画本身慢，先测 `lv_timer_handler()` 的最大间隔，
  再区分 CPU 建 UI、整屏渲染、DMA 传输和外设阻塞。
- LVGL 不是线程安全库。可以把 I²C、TF、媒体扫描放到另一个核心，但所有
  `lv_*` 调用、输入设备注册和 UI 创建仍必须留在同一个 LVGL 任务。
- 安全开屏顺序应固定为：
  **背光关闭 + LCD 输出关闭 -> 写完整首帧 -> 等最后 DMA -> DISPLAY ON ->
  开背光**。
- 过渡动画必须与颜色深度匹配。RGB565 上优先使用位置/尺寸动画或背光 PWM，
  不要直接照搬依赖 32 位 alpha layer 的整页淡入淡出。
- 部分缓冲优先用双缓冲重叠“软件绘制”和“总线传输”；缓冲必须位于可 DMA 的
  内部内存，不能因为有 8 MB PSRAM 就把 LCD DMA 缓冲随意放进 PSRAM。
- 触摸和 DRV2605L 共用 I²C 时，应按顺序初始化，并在后台任务结束后才把触摸
  注册给 LVGL，避免两个核心并发访问同一总线。
- 启动优化不能只看“总秒数”。无雪花、无半帧、动画连续、输入可靠和可恢复
  烧录，比盲目删除电源稳定等待更重要。

当前代码依赖锁定版本中的 `esp_private/startup_internal.h` 来提前钳位 GPIO；
它属于 ESP-IDF 私有接口。升级 Arduino-ESP32/ESP-IDF 时必须重新编译并做冷启动
回归。原厂 bootloader 基于 IDF 5.5.1-dirty，当前 PlatformIO 平台基于 IDF
5.3.2 系，不能为了“看起来更快”直接混烧两个版本的 bootloader。

## 四个固件基线不要混用

| 项目 | 原厂出货固件 V1.1 | 厂家 Arduino 示例 | 当前 PlatformIO Demo | 商业版应明确决定 |
| --- | --- | --- | --- | --- |
| 主要用途 | 成品展示、SETTING、原厂维护 | 板级示例代码 | 硬件验收、开发和故障定位 | 单独的产品固件与产测固件 |
| USB 默认行为 | HID 可开启；关闭 HID 后重启，或按 BOOT 下载 | 配置截图/示例随工程而异 | 默认 TinyUSB CDC + 按需 MSC | 固定 VID/PID、产品名、唯一序列号和升级策略 |
| 首次烧录 | 无可用 CDC 时需关闭 HID 或按 BOOT | 不能代表出货固件行为 | 已可用 1200 baud 自动进下载 | 保留受控维修入口和恢复包 |
| TF 作为 U 盘 | 重启进入独立维护模式，播放器不运行 | 以具体示例为准 | 应用运行中做设备/电脑所有权交接 | 二选一并完成异常掉电、拔线、休眠测试 |
| Flash 参数 | 硬件为 16 MB，完整镜像从 `0x0` 恢复 | 截图为 QIO 120 MHz、16 MB、OPI PSRAM | 本机稳定参数为 DIO 80 MHz、16 MB、8 MB OPI PSRAM | 以锁定 BOM 和每批实测为准 |
| 分区/OTA | 资料未给出可维护的产品分区定义 | 截图是 Huge APP、无 OTA | 单 8 MB app + storage，无 OTA | 双 OTA、回滚、版本与数据迁移 |
| 开机屏幕 | 有 Logo/动画 | 未作为本项目判定依据 | Logo 首帧完成后开背光，PWM 黑场完整换帧 | 冷启动、棕断电和复位都不得闪随机画面 |
| 旋钮/振动/灯环 | 行为由原厂 UI 定义 | 以具体示例为准 | 顺时针下一页；旋转不自动振；13 灯跑马 | 形成产品交互规范并做整机测试 |
| 源码与恢复 | 只有厂家完整合并镜像可直接恢复 | 厂家示例不是出货源码 | Arduino 3.1.0 + LVGL 8.4 板级 Demo | Git/CI、锁版本、许可证清单、签名发布包 |

尤其不要把“厂家 Arduino 示例能运行”理解成“与出货固件完全相同”，也不要把
当前 Demo 直接当成商业固件。这个工程更适合作为硬件抽检、产测参考和救援工具。

### 厂家资料中已经发现的歧义

资料包内部并非所有页面都一致，商业化前要把“看图猜参数”改成受控的 BOM、
样机批次和验收记录：

- 型号文字同时出现 `JC3636K718`、`JC3636K718C_I_Y1`、`JC3636W718`，
  另一个换麦克风/玻璃的批次提示又写成 `JC3636W518`。
- 屏幕被分别写成 1.8 和 1.85 英寸，但 360 × 360 和约 45.68 mm 有效区较一致。
- Flash 页面分别出现 QIO/DIO、80/120 MHz；当前实物以 DIO 80 MHz 才稳定。
- 厂家示例按 13 个 RGB 像素配置，但原理图的灯环链标号看起来是
  LED2–LED13 共 12 颗，LED1 又是充电灯。当前 Demo 保持经本机效果验证的
  13 像素配置，换批次必须重新数灯、确认颜色顺序和环绕方向。
- 厂家板级代码出现 44.1 kHz 立体声音乐输出，语音板配置则是 24 kHz
  输入/输出；它们是不同用途，不应混成同一音频规格。
- 供应商仓库提到 2025 年 7 月后某些“太极派”批次更换麦克风和屏幕玻璃，
  但目录与正文型号不一致。采购时应按标签批次向厂家确认，不能直接外推到
  所有 K718。

建议每批至少留档：外壳/PCB 标签、采购批次、关键芯片/模组 BOM、原厂固件
版本及 SHA-256、实测 Flash/PSRAM、灯数、麦克风类型、构建 profile、测试
日期和测试人。

## 7 个 LVGL 页面

底部左右按钮可以翻页；默认也可以直接转动旋钮翻页。底部中间的 `PAGE`
按钮用于切换旋钮在当前页面的特殊功能，例如频谱增益、亮度或 HID 音量。
本机实物方向已经校正为：**顺时针 = 下一页 / 增加，逆时针 = 上一页 / 减少**。

### 1. OVERVIEW 总览

集中显示：

- 电源电压
- 16 MB Flash 是否正常
- 8 MB PSRAM 是否正常
- TF 卡是否挂载
- 麦克风和音频输出是否就绪
- 是否已经检测到触摸和旋钮

先触摸一下屏幕，再转动一下旋钮，`TOUCH / KNOB` 应最终显示两者都已检测到。

### 2. SPECTRUM 拾音频谱

实时读取 PDM 麦克风并显示：

- 24 段频谱柱
- 当前声音强度 dB
- 主频率 Hz
- 频谱增益
- TF 卡 WAV 录音

对着 USB-C 右侧的小圆孔说话、拍手或播放音乐，柱状图应随声音变化。
点击 `-`、`+` 可以调增益；也可以点击底部 `PAGE` 使它变为 `GAIN`，
然后转动旋钮调节。

点击 `RECORD` 开始录音，状态行会显示时长、文件大小和丢帧数；说完后点击
`STOP`，等待状态回到 `IDLE`。录音保存为标准 **24 kHz、16-bit、单声道
PCM WAV**：

```text
/recordings/REC_0001.WAV
/recordings/REC_0002.WAV
...
```

文件编号自动递增，不会覆盖已有录音。录音期间频谱仍会实时显示，但媒体播放、
TF 重扫和 USB 磁盘会等待或被拦截，以免多个功能同时写同一张卡。状态中的
`DROP` 正常应为 `0`；若持续增加，先停止视频、Wi-Fi/BLE 扫描等高负载操作
后重试。

本机实际生成并试听过 `REC_0001.WAV`：文件 102,444 bytes，其中 PCM 数据
102,400 bytes，24 kHz、16-bit、单声道，时长约 2.133 秒，WAV 头和结束收尾
均完整。原始录音仍有约 0.0376 的正向 DC 偏置，电平也偏保守；作为硬件验证
没有问题，若用于语音产品，还要加入去直流、增益/AGC、降噪，并按交互方式
评估回声消除。

### 3. MEDIA 图片与视频

媒体页提供四个控制：

- `PLAY PHOTO`：播放 `/pic` 中的下一张 JPG
- `PLAY VIDEO`：循环播放 `/mjpeg` 中的下一段 MJPEG，默认 20 FPS
- `STOP`：停止媒体解码
- `RESCAN`：重新扫描 TF 卡

播放时会进入 360 × 360 的全屏画面，底部使用 `STOP / EXIT` 返回。
JPG 会按比例缩放并居中；视频帧由后台任务解码，LVGL 主线程只接收已经完成
的 RGB565 画面，避免界面和解码线程互相踩内存。

### 4. ACTUATORS 输出器件

- `VIBRATE`：依次播放不同振动效果
- `CHASE`：启动/停止 13 颗独立 RGB 灯的环形跑马灯；每转完一圈自动换色
- `SPEAKER`：播放一段短音调检查音频输出
- `DISPLAY BRIGHTNESS`：拖动滑条改变背光

旋钮转动和普通翻页默认不触发振动；振动电机只在点击 `VIBRATE` 时运行。
也可以点击底部 `PAGE` 使它变为 `BRI`，再转动旋钮调亮度。测试振动时把手
放在外壳上更容易感受到；测试扬声器前不要把设备贴近耳朵。

### 5. STORAGE TF 卡

显示 TF 卡是否挂载、总容量、已使用容量和百分比：

- `RESCAN TF`：重新挂载并扫描卡片
- `ENABLE USB`：停止媒体并结束录音，把 TF 卡安全交给电脑
- `USB DISK ON`：电脑正在使用 TF，设备端媒体访问已锁定
- `DISCONNECT`：电脑已经安全推出后，把 TF 卡交还给 Demo

电脑接管 TF 时，录音、图片、视频和设备端重扫都会被禁止。必须先在 Finder
或 Windows 中安全推出磁盘；屏幕确认 `Computer eject confirmed safe`
后，才能点击 `DISCONNECT`。这样可以避免电脑与设备同时修改 FAT 文件系统。

普通 `jc3636k718` 和 HID 构建会显示 `USB DISK N/A`；当前封装样机请使用
`jc3636k718_msc` 构建。

### 6. WIRELESS 无线扫描

- `SCAN WI-FI`：扫描附近 Wi-Fi 名称和信号强度
- `SCAN BLE`：扫描附近 BLE 设备

这里只做扫描，不会连接路由器，不需要输入密码，也不会把扫描结果上传。

### 7. SYSTEM 系统

显示运行时间、Flash、PSRAM、USB 模式、触摸/旋钮状态和 HID 音量状态。

普通固件显示 `CDC SERIAL`，USB TF 版显示 `CDC + DISK`，可选 HID 固件
显示 `CDC + HID`。HID 版本中先点击 `TOGGLE HID VOLUME` 启用音量功能，
再让底部模式变为 `VOL`，旋钮即可控制电脑音量。

## 准备 TF 卡媒体

### 把录音读取到电脑

1. 在 `SPECTRUM` 页面点击 `STOP`，等待状态显示 `IDLE` 和保存路径。
2. 转到 `STORAGE` 页面，点击 `ENABLE USB`。
3. 等待电脑显示 TF 磁盘，打开其中的 `recordings` 文件夹。
4. 直接播放或复制 `REC_XXXX.WAV`。
5. 完成后先在 Finder 或 Windows 中安全推出该磁盘。
6. 屏幕按钮变成 `DISCONNECT` 后点击它，等待 TF 回到 `MOUNTED`。

不要在 `RECORDING`、`STARTING` 或 `STOPPING` 状态直接断电，也不要在电脑
仍挂载 USB 磁盘时关机。

### 方法一：直接复制项目内的样例

项目已经提供可直接使用的文件：

```text
tf_card_ready/pic/demo.jpg
tf_card_ready/mjpeg/demo.mjpeg
```

内置图片是 360 × 360 baseline JPEG。内置视频是约 8 秒、160 帧、
320 × 240 的 raw MJPEG，固件按 20 FPS 播放。

已经封装的产品使用 USB TF 磁盘复制：

1. 在 `STORAGE` 页面点击 `ENABLE USB`。
2. 等待电脑显示 TF 磁盘。
3. 将 `pic` 和 `mjpeg` 复制到磁盘根目录。
4. 在 Finder 或 Windows 中安全推出磁盘。
5. 屏幕按钮变成 `DISCONNECT` 后点击它。
6. Demo 会重新挂载 TF，并自动刷新媒体列表。

macOS 也可以在终端执行：

```sh
cp -R ./tf_card_ready/pic ./tf_card_ready/mjpeg /Volumes/你的TF卡名称/
```

裸板或 TF 卡槽可接触时，也可以先将卡格式化为 FAT32，直接复制目录，正常
弹出后再在设备关机状态下插入。不要在 USB 磁盘工作时关机、拔线或播放媒体。

原厂程序自身也支持 `SETTING -> SYSTEM -> Reboot to MSC`，磁盘通常显示为
`N7 Disk`，可以用它完成第一次烧录前的媒体复制。

目前只把 **FAT32** 作为已验证格式；在 exFAT 经固件、电脑系统和异常断电
组合测试前，不要把它写进商业规格。`N7 Disk` 是原厂维护模式常见卷标，
`NO NAME` 是当前 TF 卡自身的卷标，都不代表 Flash 容量或 USB 模式。

### 方法二：把自己的照片和视频转换好

转换脚本依赖 FFmpeg。macOS 可先安装：

```sh
brew install ffmpeg
```

用自己的照片和视频生成一套目录：

```sh
./tools/prepare_tf_media.sh \
  ./tf_output \
  "/你的路径/photo.jpg" \
  "/你的路径/video.mp4"
```

脚本会生成：

```text
tf_output/pic/demo.jpg
tf_output/mjpeg/demo.mjpeg
```

然后将 `tf_output` 里面的 `pic` 和 `mjpeg` 复制到 TF 卡根目录。

如果不提供照片和视频，脚本会生成测试图案：

```sh
./tools/prepare_tf_media.sh ./tf_output
```

只转换照片：

```sh
./tools/prepare_tf_media.sh ./tf_output "/你的路径/photo.jpg"
```

只转换视频时，照片参数留空：

```sh
./tools/prepare_tf_media.sh \
  ./tf_output \
  "" \
  "/你的路径/video.mp4"
```

### 媒体格式和限制

- 照片目录固定为 `/pic`。
- 视频目录固定为 `/mjpeg`。
- 每一类最多扫描 24 个文件，按文件名排序。
- JPG 必须是 **baseline JPEG**，不能是 progressive JPEG。
- 视频推荐 **320 × 240、20 FPS、4:2:0**。
- 视频必须是 **raw MJPEG**：本质是连续排列的独立 JPEG 帧。
- 把 `movie.mp4` 直接改名成 `movie.mjpeg` 不会完成转换。
- 视频不播放音轨；脚本会主动去掉音频。
- 单个压缩 JPEG 帧最大 256 KB。超过后该帧会被丢弃；降低分辨率或质量即可。
- 照片推荐不超过 360 × 360；更大的 baseline JPEG 会自动按比例缩小。
- 建议使用简单的英文、数字文件名，避免过深目录和超长路径。

raw MJPEG 本身通常没有可靠的 FPS 时间信息，所以固件按设置的 20 FPS
播放。分辨率更高或质量过高会增加解码和 QSPI 传输压力；先以
320 × 240 @ 20 FPS 为基准，实测稳定后再提高。

## 为什么当前使用 JPEGDEC

当前工程是 PlatformIO Arduino + LVGL 8，因此选择
[JPEGDEC 1.8.4](https://github.com/bitbank2/JPEGDEC)：

- 能直接从 Arduino `File` 或内存解码
- 支持 ESP32-S3 SIMD 优化
- 能输出 RGB565 big-endian，和本项目 LVGL 色彩配置匹配
- 支持分块回调和 1/2、1/4、1/8 缩放
- 是源码依赖，不依赖厂家版本不明的旧预编译二进制

当前媒体管线是：

```text
TF 卡 -> JPG/MJPEG 帧 -> 后台 JPEG 解码 -> PSRAM RGB565 双缓冲 -> LVGL
```

以后正式项目如果切换到 ESP-IDF，可以保留 UI 和媒体服务接口，仅替换解码
后端：

- [Espressif esp_new_jpeg](https://github.com/espressif/esp-adf-libs/tree/master/esp_new_jpeg)
  负责 ESP32-S3 优化 JPEG 解码
- [Espressif esp_lv_decoder](https://components.espressif.com/components/espressif/esp_lv_decoder)
  接入 LVGL 的 JPG、PNG、QOI 和分片图片流程

静态 UI 图标和背景仍优先转换为 LVGL 原生 RGB565、索引图或 QOI；相册照片
适合 JPEG，短视频适合 MJPEG。

## USB TF 磁盘为什么这样实现

当前实现参考了以下成熟开源方案：

- [Arduino-ESP32 SD2USBMSC](https://github.com/espressif/arduino-esp32/blob/master/libraries/SD_MMC/examples/SD2USBMSC/SD2USBMSC.ino)
  的 `USBMSC + SD_MMC.readRAW/writeRAW` 扇区回调
- [Espressif esp_tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb)
  的 APP / USB 存储挂载交接事件
- [TinyUSB](https://github.com/hathach/tinyusb) 的 CDC + MSC 复合设备架构

无论采用哪种方案，都不能让电脑和固件同时挂载、读写同一个 FAT 文件系统。
原厂方案是重启到独立维护模式，播放器完全不运行，隔离最彻底；本 Demo 为了
封装样机调试方便，采用同一应用内的明确所有权状态机：

```text
设备读取 TF
  -> 停止媒体并关闭 File
  -> USB MSC 接管
  -> 电脑安全推出
  -> TF 重新挂载
  -> 媒体目录重新扫描
```

USB 回调直接按扇区读写 SD_MMC，支持非零 offset、跨扇区请求和局部扇区
read-modify-write。所有原始读写共用 TF mutex；MSC 活跃或交接期间，设备端
文件锁、重扫和媒体播放入口都会拒绝访问。

这些互锁能避免正常操作时的双重挂载，但不能消除电脑强制拔线、休眠、供电
中断或文件系统尚未落盘带来的损坏风险。商业版若把“稳定优先”放在第一位，
仍建议采用原厂式独立维护模式；若保留当前运行时交接，必须增加掉电/强拔压力
测试、损坏检测和用户可理解的恢复流程。

ESP32-S3 原生 USB 端点数量有限，因此本项目把 CDC+MSC 与 CDC+HID 拆成
不同构建，不在首版中组合 CDC+MSC+HID。这样枚举和排错更稳定。

## Cursor / PlatformIO 编译和烧录

项目的默认环境是 `jc3636k718_msc`。它是完整 Demo 的主力版本，使用
TinyUSB CDC 串口，并提供运行时 TF 卡 USB 磁盘交接：

```sh
pio run
```

查看当前串口：

```sh
pio device list
```

上传：

```sh
pio run \
  -t upload \
  --upload-port /dev/cu.usbmodemXXXX
```

打开串口监视器：

```sh
pio device monitor \
  --port /dev/cu.usbmodemXXXX \
  --baud 115200
```

配置中已经关闭串口监视器的 DTR/RTS，避免打开 Monitor 后设备一直被复位。

### 推荐：当前封装样机 USB TF 磁盘版本

设备已经封装、以后需要经常更换图片或视频时，使用：

```sh
pio run -e jc3636k718_msc
pio run -e jc3636k718_msc \
  -t upload \
  --upload-port /dev/cu.usbmodemXXXX
```

该版本仍有 CDC 串口，可以继续使用 PlatformIO 烧录和查看日志；同时在
`STORAGE` 页面提供安全的 USB TF 磁盘交接。MSC 默认不会把 TF 卡暴露给
电脑，只有点击 `ENABLE USB` 后才会出现磁盘。

### 可选 USB HID 音量版本

需要把旋钮同时作为电脑音量控制器时，编译另一个环境：

```sh
pio run -e jc3636k718_hid
pio run -e jc3636k718_hid \
  -t upload \
  --upload-port /dev/cu.usbmodemXXXX
```

三个环境的区别：

| 环境 | USB 功能 | 推荐用途 |
| --- | --- | --- |
| `jc3636k718_msc`（默认） | TinyUSB CDC + MSC | 完整 Demo、当前封装样机传 TF 图片和视频 |
| `jc3636k718` | CDC 串口 | 最简保底、日志和烧录排错 |
| `jc3636k718_hid` | TinyUSB CDC + HID | 验证电脑音量旋钮 |

HID 是 Human Interface Device，键盘、鼠标、媒体键和 Surface Dial 都属于
这一类。它不等于烧录串口。原厂固件只启用 HID 时，电脑虽然能看到旋钮，
PlatformIO 却找不到 CDC 端口，这就是第一次烧录通常需要关闭 HID 的原因。

本机已安装用户级 PlatformIO Core，普通终端可直接使用 `pio`。若旧终端
提示找不到命令，关闭后重新打开终端。日常不需要设置
`PLATFORMIO_CORE_DIR`；项目内 `.platformio-core` 只是隔离构建缓存时的
备用目录。

不要同时启动两个共享同一 `.pio` 目录的 PlatformIO 构建。本机曾因此出现
一次 `.sconsign39.dblite` 缓存缺失/竞争错误，停止并行构建后重新执行即可。

`pio device list` 里若出现类似 `ESP32-S3-DevKitC-1-N8 (8 MB, No PSRAM)`
的文字，不要据此判断实物只有 8 MB。那是当前通用 PlatformIO board profile
的描述字符串；本机由芯片接口和运行时分别确认是 16 MB Flash、8 MB PSRAM。
商业工程应建立专用 board definition，避免名称、容量和 USB 身份继续误导。

## 第一次从原厂程序烧录：实机确认流程

下面是这台 JC3636K718 实机已经走通的完整顺序。原厂固件默认启用 HID，
电脑会看到 `N7 Workshop / ESP USB DEVICE`，但 PlatformIO 没有串口。

### A. 关闭原厂 HID 并保存

1. 如果 Finder 正显示 `N7 Disk` 或 `NO NAME`，先安全推出，等磁盘消失。
2. 正常关机再开机，回到原厂界面。
3. 进入原厂 `SETTING`。
4. 找到 `HID Input` 或 `USB HID`，将它关闭。
5. **先退出设置页面**，让原厂程序保存设置；不要停留在设置页直接断电。
6. USB 数据线保持连接，把最右侧电源开关拨到右边关机，再拨到左边正常
   开机一次，让关闭 HID 的设置生效。

### B. 进入黑屏下载状态

1. 再把最右侧电源开关拨到右边关机，USB 数据线保持连接。
2. 按住 USB-C 接口左侧、比较隐蔽的 **BOOT 小扁按钮**。
3. 保持按住 BOOT，同时把右侧电源开关拨到左边开机。
4. 继续按住约 3 秒，再松开 BOOT。
5. 此时屏幕保持黑色是正常现象，表示设备正在等待烧录，并不代表屏幕损坏。
6. 在电脑上执行：

```sh
pio device list
```

成功时应看到类似：

```text
/dev/cu.usbmodem2101
USB JTAG/serial debug unit
```

只有看到 `/dev/cu.usbmodem...` 后才开始上传。如果屏幕已经黑了但电脑仍
没有端口，拔下设备端 USB-C，等待 3 秒，把插头翻转 180° 后重新插入再查。

厂家原理图把这个 BOOT 开关标为连接 GPIO0 的 `SW1`，但外观说明页没有
清晰标注，所以它很容易被忽略。USB-C 右侧的小圆孔不是 BOOT，不要用针捅。

### C. PlatformIO 上传

```sh
pio run -e jc3636k718_msc \
  -t upload \
  --upload-port /dev/cu.usbmodemXXXX
```

上传期间不要关机或拔线。看到 `Hash of data verified` 和 `[SUCCESS]` 后，
固件通常会自动重启；如果仍保持黑屏，松开所有按键，正常关机再开机一次，
这次不要按 BOOT。

## 后续再次烧录

三个 Demo 环境的配置都保留 USB CDC；普通环境使用硬件 CDC/JTAG，MSC 和
HID 环境使用 TinyUSB CDC。当前默认 `jc3636k718_msc` 已在这台实物上反复
验证 **1200 baud touch** 能自动进入下载模式，不需要再次关闭 HID 或按
BOOT。另两个环境已经保留对应能力，但在作为发布版本前仍应各自做一次完整
实机验证，不把“能编译”当作“已验证”。

### 为什么刚拿到时必须按 BOOT，现在却能自动烧录

两次情况的 USB 固件不同：

```text
原厂出货固件
  -> 默认可见的是 HID（Surface Dial/输入设备）
  -> 没有 PlatformIO 可用的运行时 CDC 串口
  -> 电脑无法发送 1200 baud 请求
  -> 只能在原厂设置关闭 HID 后重启，或用 BOOT + 上电进入 ROM 下载器

当前 Demo
  -> 启动时创建 CDC 串口
  -> 电脑以 1200 baud 短暂打开该串口
  -> Arduino USB CDC 代码请求重启到 Bootloader
  -> USB 重新枚举为 ROM 的 USB JTAG/serial debug unit
  -> PlatformIO 向新端口烧录
```

所以不是 BOOT 被“永久打开”了，也不是电脑突然学会了烧录；是当前 Demo
主动提供了原厂默认固件没有提供的 CDC 重启入口。BOOT 仍是最后的硬件救援
方式：固件损坏、USB 配置错误或 CDC 起不来时，照旧按住 BOOT 上电。

烧录前如果 Finder 正显示 `NO NAME`，先安全推出；等屏幕按钮变为
`DISCONNECT` 后点击它，让 Demo 重新接管 TF 卡。然后查找当前运行端口：

```sh
pio device list
```

把下面的 `XXXX` 换成当前 Demo 的端口。这一步会让设备自动重启到 ROM 下载
模式，屏幕变黑属于正常现象：

```sh
~/.platformio/penv/bin/python -c 'import serial,time; s=serial.Serial("/dev/cu.usbmodemXXXX",1200,timeout=.2); time.sleep(.25); s.close()'
```

再次执行 `pio device list`，使用新出现、说明为
`USB JTAG/serial debug unit` 的端口上传：

```sh
pio run -e jc3636k718_msc \
  -t upload \
  --upload-port /dev/cu.usbmodemYYYY
```

Cursor 的 PlatformIO Upload 若能自动找到切换后的端口，也可以直接使用。
本机默认 MSC 固件已实测上述 1200 baud 方式能够自动重启并完成上传。只有
当前固件损坏、CDC 不再出现或仍为原厂 HID 时，才需要使用前面的手动 BOOT
黑屏流程。

普通环境关键参数是：

```ini
-DARDUINO_USB_MODE=1
-DARDUINO_USB_CDC_ON_BOOT=1
```

如果上传失败、端口消失、程序启动后不断重启，或者电脑仍停留在原厂 MSC
模式，先安全推出磁盘，再按上面的 A、B 两段重新进入黑屏下载状态。

## 16 MB Flash 为什么应用上限是 8 MB

硬件确实是 16 MB Flash，不是 8 MB。项目使用
`partitions_16mb.csv` 将空间分为：

```text
0x00010000 - 约 8 MB：应用程序 app0
0x00810000 - 约 7.9 MB：storage
0x00FF0000 - 64 KB：coredump
```

因此：

- `ESP.getFlashChipSize()` 应显示 16 MB。
- PlatformIO 的“Maximum program size”约为 8 MB。
- 两者不矛盾：16 MB 是整颗 Flash，8 MB 是给当前单个应用分区的上限。
- 当前布局只有一个 8 MB 应用槽，没有双 OTA 槽。

关键 PlatformIO 参数：

```ini
board_upload.flash_size = 16MB
board_upload.maximum_size = 8388608
board_build.flash_mode = dio
board_build.f_flash = 80000000L
board_build.arduino.memory_type = dio_opi
board_build.partitions = partitions_16mb.csv
```

## 实机完整检查清单

烧录成功后，按下面顺序检查最容易定位问题：

- [ ] 屏幕正常显示圆形 LVGL 界面，没有持续黑屏或反复重启。
- [ ] 完整关机再开机时依次出现短暂黑屏、Logo/进度动画和完整 Demo，没有
      雪花、随机色块、半帧或 Logo/首页来回闪；允许一次很短的受控暗场换帧。
- [ ] 启动日志中的 `Splash max LVGL handler gap` 明显低于旧版实测的
      136–138 ms；`Draw buffer` 显示 `2 x 36 rows`。
- [ ] 拔掉 USB、关机等待 10 秒后的纯电池冷启动也不提前点亮随机显存。
- [ ] OVERVIEW 显示 Flash `16 MB OK`、PSRAM `8 MB OK`。
- [ ] 触摸屏幕、转动旋钮后，输入状态显示两者都已检测到。
- [ ] TF 卡显示 `MOUNTED`，并能看到容量。
- [ ] SPECTRUM 页面说话或拍手时，24 段柱状图会变化。
- [ ] SPECTRUM 点击 `RECORD`、说话后再点 `STOP`，能生成可播放的
      `/recordings/REC_XXXX.WAV`，且 `DROP` 为 `0`。
- [ ] MEDIA 的 `PLAY PHOTO` 能显示 `demo.jpg`。
- [ ] MEDIA 的 `PLAY VIDEO` 能循环播放 `demo.mjpeg`，`STOP / EXIT` 可退出。
- [ ] `VIBRATE` 有触感；`CHASE` 能启动 13 像素三点拖尾跑马灯、每圈换色，
      再点一次会停止并熄灭；`SPEAKER` 有短音调。
- [ ] 背光滑条和旋钮 `BRI` 模式都能改变亮度。
- [ ] STORAGE 能显示 TF 总容量和使用量。
- [ ] MSC 版本点击 `ENABLE USB` 后电脑能显示 TF 磁盘。
- [ ] 电脑推出磁盘后，屏幕出现 `DISCONNECT`，点击后媒体能重新扫描。
- [ ] WIRELESS 能分别列出 Wi-Fi 和 BLE 扫描结果。
- [ ] SYSTEM 显示正确 USB 模式和运行时间。
- [ ] 串口日志没有持续的复位原因或看门狗循环。

## 从 Demo 到商业产品还缺什么

当前 Demo 已适合验证硬件、媒体链路和交互方向，但不能只改 Logo 就出货。
下面按优先级保留为正式项目的门槛。

### P0：出货前必须解决

1. **建立可复现发布体系**
   - 把正式工程放入私有 Git，提交源码和自定义 board definition。
   - 锁定 PlatformIO 平台、Arduino/ESP-IDF、LVGL 和所有组件版本。
   - 每个 release 保存 commit、工具链版本、硬件批次、分区表、各烧录地址、
     二进制 SHA-256、验收结果和一键恢复包。
   - CI 分别构建目标，不让多个任务共享同一可写 `.pio` 缓存。

2. **清理许可证和 USB 身份**
   - 审计厂家示例及第三方库能否用于商业产品，生成 `LICENSES`、
     `THIRD_PARTY_NOTICES` 和 SBOM；未明确授权的厂家代码不要默认可商用。
   - 厂家示例里的 `0xCAFE`、Espressif VID `0x303A`、`TinyUSB Device` 和
     固定序列号 `123456` 都是占位/示例，不能原样出货。
   - 申请或合法取得 VID/PID，设置产品名、厂商名和每台唯一序列号，并验证
     Windows/macOS/Linux 的驱动、重连和升级行为。

3. **重新设计分区和升级**
   - 当前 `factory app + storage` 只有一个应用槽，不能做安全 OTA 回滚。
   - 产品版应预留双 OTA 槽、OTA data、NVS/密钥、故障转储和必要的数据分区，
     明确失败回滚、版本迁移、最低可升级版本及离线恢复。
   - 参考 Espressif 的
     [分区表文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/partition-tables.html)
     和
     [OTA 回滚文档](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/ota.html)。

4. **建立安全启动和密钥流程**
   - 联网产品通常应评估 Secure Boot v2、Flash Encryption、签名 OTA、设备
     身份凭据、密钥轮换和服务端鉴权。
   - eFuse/安全配置可能不可逆，并可能改变 ROM USB 恢复路径。开发阶段不要
     随手烧安全 eFuse；先在可报废样机上验证生产、升级、返修和密钥托管全流程。
   - 参考 Espressif 的
     [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)
     与
     [安全功能启用流程](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/security-features-enablement-workflows.html)。

5. **选定 TF/USB 产品架构**
   - 最稳方案是原厂式独立维护模式：重启后只运行 MSC，播放器和录音完全停止。
   - 当前运行时 MSC 适合封装样机，但若用于出货，必须覆盖写入中拔线、电脑
     休眠/唤醒、设备掉电、磁盘满、损坏 FAT、坏卡、不同容量/品牌和恢复流程。
   - 无论哪种方案，都要保证电脑与固件永不同时挂载同一文件系统。

6. **保住救援和产测能力**
   - 保留 GPIO0/BOOT、复位、串口/原生 USB 和关键测试点，形成工厂烧录治具。
   - GPIO0 同时承载灯环数据，必须验证上电绑带、电平、灯环和下载模式互不干扰。
   - 发布独立的产测/硬件 Demo，不把工厂诊断入口直接暴露给普通用户。

### P1：产品质量与长期维护

- **设置持久化**：当前亮度、页面和大多数选项只在 RAM；产品版要用 NVS，
  处理版本迁移、断电原子性和恢复出厂设置。
- **语音链路**：加入去直流、标定增益/AGC、降噪、静音/隐私指示和录音
  保留/删除规则。单 PDM 麦克风适合基础唤醒和轮流对话，不要未经验证承诺
  真正的全双工设备侧 AEC。
- **电源与热设计**：校准电池电压/电量，测试低电、充电、睡眠/唤醒、RGB
  全亮电流、扬声器保护、温升和棕断电恢复。
- **存储寿命**：处理磁盘满、REC 编号上限、WAV 断电修复、坏卡提示、日志
  轮转和不同 TF 容量；exFAT 未验证前只声明 FAT32。
- **整机验证**：至少覆盖多块板、多采购批次、不同 USB 线和三大桌面系统，
  做冷启动、长稳、ESD、异常拔线、低压和反复升级测试。
- **法规与生产**：按销售地区评估无线/EMC/ESD、电池、USB、材料与标签要求，
  建立来料抽检、校准、序列号、最终功能测试和售后追溯。

### 接入小智的建议工程路线

保留本 Arduino/LVGL 工程作为板级验收、产测和恢复工具；另建 ESP-IDF 产品
工程接入小智，不把完整语音助手硬塞进本 Demo。优先参考：

- [xiaozhi-esp32 官方仓库](https://github.com/78/xiaozhi-esp32)
- [官方自定义开发板说明](https://github.com/78/xiaozhi-esp32/blob/main/docs/custom-board_zh.md)
- [Taiji Pi S3 板级实现](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/taiji-pi-s3)

新工程应复用已验证的显示、CST816S、旋钮、TF、PDM/I2S、灯环和触觉引脚，
但重新实现产品级 USB、OTA、安全、设置和音频策略。这样 Demo 升级不会影响
产品固件，产品业务变化也不会破坏工厂硬件诊断基线。

## 可以直接关机吗

可以，但不能在 `USB DISK ON` 时直接关机。若电脑正在使用 TF：

1. 先在 Finder 或 Windows 中安全推出磁盘。
2. 屏幕显示 `DISCONNECT` 后点击它。
3. 等 TF 再次显示 `MOUNTED`。

普通状态下先停止录音并等待 `IDLE`，再停止 MEDIA 视频播放，等几秒让文件
关闭；然后把最右侧电源开关拨到右边关机，最后拔掉 USB。

## 恢复原厂程序

厂家资料包中的完整合并镜像位于：

```text
9-Burn/Burn operation instructions/JC3636K718_V1.1.bin
```

镜像大小约 12 MB，已核对 SHA-256：

```text
3e329c121a5f5b97e014bbd390d4851176499276b6ff47a86203d4067b539093
```

恢复完整镜像会覆盖当前 Demo、分区数据和自定义程序。确认确实要恢复后：

1. 按前面的“原厂首次烧录实机流程”进入黑屏下载状态，让设备出现
   `/dev/cu.usbmodem...`。
2. 在本项目目录设置正确端口并写入地址 `0x0`：

```sh
export factory_bin="/你的资料包路径/9-Burn/Burn operation instructions/JC3636K718_V1.1.bin"
export serial_port="/dev/cu.usbmodemXXXX"

PATH="$HOME/.platformio/penv/bin:$PATH" \
pio pkg exec -p tool-esptoolpy -c \
  'python "$(command -v esptool.py)" \
    --chip esp32s3 \
    --port "$serial_port" \
    --baud 460800 \
    write_flash 0x0 "$factory_bin"'
```

3. 写入完成后关机再开机。

恢复成功后，USB 会重新显示为 `N7 Workshop / ESP USB DEVICE`，
`/dev/cu.usbmodem...` 消失是正常现象，因为原厂 HID 又被启用了。以后要
再次开发，重新执行前面的 A、B 两段即可。

Windows 也可以使用厂家资料包中的 `flash_download_tool_3.9.3`，芯片选择
ESP32-S3、完整镜像地址填写 `0x0`。不要把完整合并镜像写到应用分区地址
`0x10000`。

## 厂家资料证据索引

本 README 中涉及“原厂要求”“厂家示例”和“原理图”的结论，主要核对了资料包
`JC3636K718_knob_EN` 中的以下文件。商业项目应把对应版本随采购批次归档，
但对外再分发前要先确认厂家许可：

- `1-Instructions/JC3636K718 Instructions-EN.pdf`：产品功能、默认 HID、
  二次开发和 U 盘说明。
- `7-User's manual/Getting started JC3636K718.pdf`：Arduino 配置、关闭
  HID/按 BOOT 和重新上电流程。
- `6-Schematic diagram/JC3636K718.pdf` 与 `JC3636K718_P.pdf`：主控、
  W25Q128、GPIO0/BOOT/RGB、USB、显示、麦克风、DAC/功放和灯环。
- `2-Program Example/Demo_idf/demo/main/device/pinconfig.h`：厂家示例
  引脚总表。
- `2-Program Example/Demo_idf/demo/main/knob/tusb_config.h` 与
  `usb_descriptors.c`：HID=1、CDC=0 的 Surface Dial 示例及占位 USB 身份。
- `2-Program Example/Demo_idf/msc_demo/main/msc/msc_init.c`：TF 作为
  USB MSC 介质以及主机/应用不能同时访问的限制。
- `2-Program Example/Demo_arduino/Arduino configuration.png`：
  16 MB、OPI PSRAM、硬件 CDC/JTAG 等厂家 Arduino 截图；其中 Flash
  参数与其他资料冲突，不能单独作为量产依据。
- `9-Burn/Burn operation instructions/JC3636K718_V1.1.bin`：当前资料包
  的原厂完整恢复镜像，其 SHA-256 已记录在上一节。

## 常见问题

### USB 能连接，但 PlatformIO 没有端口

这通常是原厂 HID 模式，不代表 USB 线一定有问题。确认使用数据线后，先
关闭原厂 HID、退出设置保存，再按住 BOOT 开机进入黑屏下载状态。黑屏后用
`pio device list` 确认出现 `USB JTAG/serial debug unit`。

### 上传成功后黑屏或不断重启

确认 Flash 使用 DIO 80 MHz，不要改成 QIO。若串口出现
`TG0WDT_SYS_RST`，重新使用本项目配置完整编译并烧录。

### TF 卡已插入，但找不到媒体

逐项确认：

1. 卡根目录直接存在 `/pic` 和 `/mjpeg`。
2. 不是 `/tf_card_ready/pic` 这种多套一层的结构。
3. 图片扩展名是 `.jpg`/`.jpeg`，视频是 `.mjpeg`/`.mjpg`。
4. JPG 是 baseline，不是 progressive。
5. MP4 已经过脚本转换，不是只改了文件名。
6. 点击 MEDIA 或 STORAGE 的 `RESCAN`，必要时关机重插 TF 卡。

### MJPEG 卡顿、报错或不播放

先用项目自带样例验证。如果样例正常，重新转换自己的视频为
320 × 240 @ 20 FPS，并降低 JPEG 质量，确保每个压缩帧小于 256 KB。

### 打开串口监视器后设备复位

默认 MSC/HID 环境的 `Serial` 是 TinyUSB CDC，主机必须置
`DTR=1, RTS=0` 才会发送应用日志；项目已经按此设置。正常监视使用 115200：

```sh
pio device monitor \
  -p /dev/cu.usbmodemXXXX \
  -b 115200 \
  --dtr 1 \
  --rts 0
```

不要用 1200 baud 打开监视器：1200 是本项目故意保留的自动下载触发条件，会
让设备重启进入 ROM bootloader。若其他串口软件仍导致复位，检查它是否自动
探测多个波特率或切换 DTR/RTS。
