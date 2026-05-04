export const presetConfigs = {
  pet: {
    label: "宠物纪念",
    shortLabel: "宠物",
    targetWidth: 56,
    maxColors: 18,
    brand: "MARD",
    cropRatio: "portrait",
    framing: "balanced",
    styleMode: "soft_toon",
    subtitle: "保留毛色和眼神，适合做成桌上的小纪念。",
    guidance: "重点看眼睛、鼻尖和轮廓是否清楚，满意了再开拼。",
  },
  couple: {
    label: "情侣头像",
    shortLabel: "情侣",
    targetWidth: 48,
    maxColors: 18,
    brand: "Perler",
    cropRatio: "square",
    framing: "tight",
    styleMode: "sticker_pop",
    subtitle: "适合头像和双人礼物，让脸部轮廓更利落。",
    guidance: "先确认脸型和发色看起来舒服，再下载图纸。",
  },
  family: {
    label: "亲子手作",
    shortLabel: "亲子",
    targetWidth: 40,
    maxColors: 12,
    brand: "Hama",
    cropRatio: "portrait",
    framing: "airy",
    styleMode: "none",
    subtitle: "颜色更少、难度更低，适合一起完成。",
    guidance: "优先看整体是否可爱，细节不用追得太满。",
  },
  gift: {
    label: "周末小礼物",
    shortLabel: "礼物",
    targetWidth: 32,
    maxColors: 12,
    brand: "MARD",
    cropRatio: "auto",
    framing: "balanced",
    styleMode: "clean_ink",
    subtitle: "更快出效果，适合先做一张小礼物。",
    guidance: "先看拼豆效果够不够可爱，够的话就下载图纸。",
  },
};

export const stageCaptions = {
  original: "先确认是不是这张照片。",
  prepared: "系统会自动裁出更适合拼豆的画面。",
  result: "先看拼出来大概长什么样。",
  sheet: "满意后按这张图纸开拼。",
};

export const productMoments = [
  {
    id: "01",
    title: "把喜欢的照片，变成能开拼的图纸",
    text: "先把主体、颜色和色号整理好，让照片更适合真的拼出来。",
  },
  {
    id: "02",
    title: "先看效果，再决定怎么做",
    text: "先判断这张图做出来好不好看，再选择尺寸、颜色数和豆子品牌。",
  },
  {
    id: "03",
    title: "一张照片，直接拿到制作清单",
    text: "拼豆效果、色号清单、颜色数量、总颗数和图纸图片，一次给全。",
  },
];
