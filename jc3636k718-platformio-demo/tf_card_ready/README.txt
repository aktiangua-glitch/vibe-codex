JC3636K718 TF 卡媒体说明
========================

复制到 TF 卡时，请复制本目录里面的 pic 和 mjpeg 两个文件夹。
不要把外层 tf_card_ready 文件夹一起套进去。

正确结构：

TF 卡根目录/
├── pic/
│   └── demo.jpg
└── mjpeg/
    └── demo.mjpeg

格式要求：

1. 图片放在 /pic，使用 baseline JPEG。
2. 视频放在 /mjpeg，必须是 raw MJPEG，不是改名后的 MP4。
3. 推荐视频参数为 320x240、20 FPS、4:2:0。
4. 每类最多识别 24 个文件。
5. 单个压缩 JPEG 帧不能超过 256 KB。

需要转换自己的照片或视频时，请查看项目根目录 README.md，并使用：

tools/prepare_tf_media.sh
