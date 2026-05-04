export const cropRatioPresets = {
  auto: { label: "原图比例", value: null },
  square: { label: "1:1 方图", value: 1 },
  portrait: { label: "礼物 4:5", value: 4 / 5 },
  story: { label: "海报 3:4", value: 3 / 4 },
  wide: { label: "横版 3:2", value: 3 / 2 },
};

export const framingPresets = {
  airy: { label: "留白", margin: 0.28 },
  balanced: { label: "平衡", margin: 0.2 },
  tight: { label: "靠近主体", margin: 0.12 },
};

export const stylePresets = {
  none: {
    label: "原图直转",
    tag: "原图",
    description: "保留照片原本感觉，先快速看看适不适合做。",
  },
  soft_toon: {
    label: "柔和卡通",
    tag: "柔和",
    description: "减少杂色，让人物和宠物更像插画。",
  },
  sticker_pop: {
    label: "潮玩贴纸",
    tag: "亮色",
    description: "颜色更亮、色块更明确，适合头像和礼物。",
  },
  clean_ink: {
    label: "清爽描边",
    tag: "描边",
    description: "强化轮廓，让拼出来的边界更清楚。",
  },
};

export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function getRatioValue(ratioKey, imageWidth, imageHeight) {
  if (ratioKey === "auto" || !cropRatioPresets[ratioKey]) {
    return imageWidth / imageHeight;
  }
  return cropRatioPresets[ratioKey].value;
}

export function getBaseCropRect(imageWidth, imageHeight, ratio) {
  const imageRatio = imageWidth / imageHeight;
  if (imageRatio > ratio) {
    return {
      width: imageHeight * ratio,
      height: imageHeight,
    };
  }

  return {
    width: imageWidth,
    height: imageWidth / ratio,
  };
}

function rgbDistance(left, right) {
  const dr = left[0] - right[0];
  const dg = left[1] - right[1];
  const db = left[2] - right[2];
  return Math.sqrt(dr * dr + dg * dg + db * db);
}

function computeEdgeAverage(data, width, height) {
  const totals = [0, 0, 0];
  let count = 0;

  const pushPixel = (offset) => {
    totals[0] += data[offset];
    totals[1] += data[offset + 1];
    totals[2] += data[offset + 2];
    count += 1;
  };

  for (let x = 0; x < width; x += 1) {
    pushPixel(x * 4);
    pushPixel(((height - 1) * width + x) * 4);
  }

  for (let y = 1; y < height - 1; y += 1) {
    pushPixel((y * width) * 4);
    pushPixel((y * width + (width - 1)) * 4);
  }

  if (!count) {
    return [255, 255, 255];
  }

  return totals.map((value) => value / count);
}

function computeEdgeNoise(data, width, height, edgeAverage) {
  const values = [];

  const readDistance = (offset) => rgbDistance(
    [data[offset], data[offset + 1], data[offset + 2]],
    edgeAverage
  );

  for (let x = 0; x < width; x += 1) {
    values.push(readDistance(x * 4));
    values.push(readDistance(((height - 1) * width + x) * 4));
  }

  for (let y = 1; y < height - 1; y += 1) {
    values.push(readDistance((y * width) * 4));
    values.push(readDistance((y * width + (width - 1)) * 4));
  }

  const average = values.reduce((sum, value) => sum + value, 0) / Math.max(values.length, 1);
  return average;
}

function findAlphaSubjectBox(data, width, height, alphaThreshold = 12) {
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;
  let visiblePixels = 0;
  let transparentPixels = 0;

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const alpha = data[(y * width + x) * 4 + 3];
      if (alpha <= alphaThreshold) {
        transparentPixels += 1;
        continue;
      }

      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);
      visiblePixels += 1;
    }
  }

  const totalPixels = Math.max(width * height, 1);
  const transparentRatio = transparentPixels / totalPixels;
  if (!visiblePixels || transparentRatio < 0.04) return null;

  return {
    x: clamp(minX / width, 0, 1),
    y: clamp(minY / height, 0, 1),
    width: clamp((maxX - minX + 1) / width, 0.12, 1),
    height: clamp((maxY - minY + 1) / height, 0.12, 1),
    confidence: clamp(0.72 + transparentRatio * 0.28, 0.72, 1),
    hasTransparentBackground: true,
    visibleRatio: visiblePixels / totalPixels,
  };
}

export function findSubjectBoxFromRgba(data, width, height) {
  const alphaSubject = findAlphaSubjectBox(data, width, height);
  if (alphaSubject) return alphaSubject;

  const edgeAverage = computeEdgeAverage(data, width, height);
  const noise = computeEdgeNoise(data, width, height, edgeAverage);
  const threshold = Math.max(28, noise + 24);
  const step = Math.max(1, Math.floor(Math.min(width, height) / 220));

  let minX = width;
  let minY = height;
  let maxX = 0;
  let maxY = 0;
  let hits = 0;

  for (let y = 0; y < height; y += step) {
    for (let x = 0; x < width; x += step) {
      const offset = (y * width + x) * 4;
      const distance = rgbDistance(
        [data[offset], data[offset + 1], data[offset + 2]],
        edgeAverage
      );

      if (distance < threshold) continue;

      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);
      hits += 1;
    }
  }

  if (!hits) {
    return {
      x: 0.2,
      y: 0.14,
      width: 0.6,
      height: 0.72,
      confidence: 0,
    };
  }

  const normalized = {
    x: clamp(minX / width, 0, 1),
    y: clamp(minY / height, 0, 1),
    width: clamp((maxX - minX) / width, 0.12, 1),
    height: clamp((maxY - minY) / height, 0.12, 1),
  };
  const confidence = clamp(hits / ((width / step) * (height / step) * 0.44), 0.18, 1);

  return {
    ...normalized,
    confidence,
  };
}

export function computeCropWindow({
  imageWidth,
  imageHeight,
  ratioKey,
  framingKey,
  zoomAdjust,
  offsetX,
  offsetY,
  subjectBox,
}) {
  const ratio = getRatioValue(ratioKey, imageWidth, imageHeight);
  const baseCrop = getBaseCropRect(imageWidth, imageHeight, ratio);
  const framing = framingPresets[framingKey] || framingPresets.balanced;
  const subject = subjectBox || {
    x: 0.2,
    y: 0.14,
    width: 0.6,
    height: 0.72,
    confidence: 0,
  };

  const subjectWidth = subject.width * imageWidth;
  const subjectHeight = subject.height * imageHeight;
  const subjectCenterX = (subject.x + subject.width / 2) * imageWidth;
  const subjectCenterY = (subject.y + subject.height / 2) * imageHeight;
  const subjectFill = clamp(1 - framing.margin * 2, 0.34, 0.92);
  const desiredCropWidth = subjectWidth / subjectFill;
  const desiredCropHeight = subjectHeight / subjectFill;
  const autoZoom = Math.max(
    baseCrop.width / Math.max(desiredCropWidth, 1),
    baseCrop.height / Math.max(desiredCropHeight, 1),
    1
  );
  const finalZoom = clamp(autoZoom * zoomAdjust, 1, 4);
  const cropWidth = baseCrop.width / finalZoom;
  const cropHeight = baseCrop.height / finalZoom;
  const maxShiftX = Math.max((imageWidth - cropWidth) / 2, 0);
  const maxShiftY = Math.max((imageHeight - cropHeight) / 2, 0);
  const centerX = clamp(subjectCenterX + offsetX * maxShiftX, cropWidth / 2, imageWidth - cropWidth / 2);
  const centerY = clamp(subjectCenterY + offsetY * maxShiftY, cropHeight / 2, imageHeight - cropHeight / 2);

  return {
    ratio,
    zoom: finalZoom,
    x: clamp(centerX - cropWidth / 2, 0, imageWidth - cropWidth),
    y: clamp(centerY - cropHeight / 2, 0, imageHeight - cropHeight),
    width: cropWidth,
    height: cropHeight,
  };
}

function getStyleConfig(styleMode, intensity) {
  const strength = clamp(intensity, 0, 1);
  switch (styleMode) {
    case "soft_toon":
      return {
        brightness: 0.03 * strength,
        contrast: 0.18 * strength,
        saturation: 0.24 * strength,
        warmth: 0.02 * strength,
        posterize: 7 - Math.round(strength * 2),
        edgeDarken: 0.12 * strength,
      };
    case "sticker_pop":
      return {
        brightness: 0.05 * strength,
        contrast: 0.36 * strength,
        saturation: 0.46 * strength,
        warmth: 0.06 * strength,
        posterize: 5,
        edgeDarken: 0.18 * strength,
      };
    case "clean_ink":
      return {
        brightness: 0.04 * strength,
        contrast: 0.24 * strength,
        saturation: 0.08 * strength,
        warmth: -0.01 * strength,
        posterize: 6,
        edgeDarken: 0.24 * strength,
      };
    default:
      return {
        brightness: 0,
        contrast: 0,
        saturation: 0,
        warmth: 0,
        posterize: 0,
        edgeDarken: 0,
      };
  }
}

function posterizeChannel(value, levels) {
  if (!levels || levels <= 1) return value;
  const step = 255 / (levels - 1);
  return Math.round(Math.round(value / step) * step);
}

export function applyStyleToImageData(imageData, width, height, styleMode, intensity) {
  if (styleMode === "none") return imageData;

  const config = getStyleConfig(styleMode, intensity);
  const data = imageData.data;
  const edgeSource = new Uint8ClampedArray(data);

  for (let index = 0; index < data.length; index += 4) {
    let r = data[index];
    let g = data[index + 1];
    let b = data[index + 2];

    const average = (r + g + b) / 3;
    r = average + (r - average) * (1 + config.saturation);
    g = average + (g - average) * (1 + config.saturation);
    b = average + (b - average) * (1 + config.saturation);

    r = ((r - 128) * (1 + config.contrast)) + 128 + 255 * config.brightness + 255 * config.warmth;
    g = ((g - 128) * (1 + config.contrast)) + 128 + 255 * config.brightness;
    b = ((b - 128) * (1 + config.contrast)) + 128 + 255 * config.brightness - 255 * config.warmth * 0.5;

    r = clamp(r, 0, 255);
    g = clamp(g, 0, 255);
    b = clamp(b, 0, 255);

    if (config.posterize) {
      r = posterizeChannel(r, config.posterize);
      g = posterizeChannel(g, config.posterize);
      b = posterizeChannel(b, config.posterize);
    }

    data[index] = r;
    data[index + 1] = g;
    data[index + 2] = b;
  }

  if (config.edgeDarken > 0) {
    for (let y = 1; y < height - 1; y += 1) {
      for (let x = 1; x < width - 1; x += 1) {
        const index = (y * width + x) * 4;
        const right = ((y * width + (x + 1)) * 4);
        const down = (((y + 1) * width + x) * 4);
        const edgeDelta = (
          Math.abs(edgeSource[index] - edgeSource[right])
          + Math.abs(edgeSource[index + 1] - edgeSource[right + 1])
          + Math.abs(edgeSource[index + 2] - edgeSource[right + 2])
          + Math.abs(edgeSource[index] - edgeSource[down])
          + Math.abs(edgeSource[index + 1] - edgeSource[down + 1])
          + Math.abs(edgeSource[index + 2] - edgeSource[down + 2])
        ) / 6;
        const edgeAmount = clamp(edgeDelta / 255, 0, 1) * config.edgeDarken;

        data[index] = clamp(data[index] * (1 - edgeAmount), 0, 255);
        data[index + 1] = clamp(data[index + 1] * (1 - edgeAmount), 0, 255);
        data[index + 2] = clamp(data[index + 2] * (1 - edgeAmount), 0, 255);
      }
    }
  }

  return imageData;
}

export async function analyzeImageSubject(image) {
  const canvas = document.createElement("canvas");
  const context = canvas.getContext("2d", { willReadFrequently: true });
  canvas.width = image.width;
  canvas.height = image.height;
  context.drawImage(image, 0, 0);
  const imageData = context.getImageData(0, 0, image.width, image.height);
  return findSubjectBoxFromRgba(imageData.data, image.width, image.height);
}

export function buildPreparedSource(image, options) {
  const cropWindow = computeCropWindow({
    imageWidth: image.width,
    imageHeight: image.height,
    ratioKey: options.cropRatio,
    framingKey: options.framing,
    zoomAdjust: options.zoomAdjust,
    offsetX: options.offsetX,
    offsetY: options.offsetY,
    subjectBox: options.subjectBox,
  });

  const outputWidth = 1200;
  const outputHeight = Math.round(outputWidth / cropWindow.ratio);
  const canvas = document.createElement("canvas");
  canvas.width = outputWidth;
  canvas.height = outputHeight;
  const context = canvas.getContext("2d", { willReadFrequently: true });
  context.drawImage(
    image,
    cropWindow.x,
    cropWindow.y,
    cropWindow.width,
    cropWindow.height,
    0,
    0,
    outputWidth,
    outputHeight
  );

  if (options.styleMode !== "none") {
    const imageData = context.getImageData(0, 0, outputWidth, outputHeight);
    const nextImageData = applyStyleToImageData(
      imageData,
      outputWidth,
      outputHeight,
      options.styleMode,
      options.styleIntensity
    );
    context.putImageData(nextImageData, 0, 0);
  }

  return {
    canvas,
    cropWindow,
    outputWidth,
    outputHeight,
  };
}
