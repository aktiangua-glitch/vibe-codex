import { palettes } from "../data/palettes.js";

function hexToRgb(hex) {
  const normalized = hex.replace("#", "");
  const value = normalized.length === 3
    ? normalized.split("").map((part) => `${part}${part}`).join("")
    : normalized;

  return {
    r: Number.parseInt(value.slice(0, 2), 16),
    g: Number.parseInt(value.slice(2, 4), 16),
    b: Number.parseInt(value.slice(4, 6), 16),
  };
}

function srgbToLinear(value) {
  const channel = value / 255;
  return channel <= 0.04045
    ? channel / 12.92
    : Math.pow((channel + 0.055) / 1.055, 2.4);
}

function labF(value) {
  return value > 0.008856
    ? Math.cbrt(value)
    : 7.787 * value + 16 / 116;
}

function rgbToLab({ r, g, b }) {
  const rl = srgbToLinear(r);
  const gl = srgbToLinear(g);
  const bl = srgbToLinear(b);

  const x = rl * 0.4124 + gl * 0.3576 + bl * 0.1805;
  const y = rl * 0.2126 + gl * 0.7152 + bl * 0.0722;
  const z = rl * 0.0193 + gl * 0.1192 + bl * 0.9505;

  const fx = labF(x / 0.95047);
  const fy = labF(y / 1);
  const fz = labF(z / 1.08883);

  return {
    l: 116 * fy - 16,
    a: 500 * (fx - fy),
    b: 200 * (fy - fz),
  };
}

function degToRad(value) {
  return (value * Math.PI) / 180;
}

function radToDeg(value) {
  return (value * 180) / Math.PI;
}

function deltaE2000(left, right) {
  const avgLp = (left.l + right.l) / 2;
  const c1 = Math.sqrt(left.a * left.a + left.b * left.b);
  const c2 = Math.sqrt(right.a * right.a + right.b * right.b);
  const avgC = (c1 + c2) / 2;

  const g = 0.5 * (1 - Math.sqrt(Math.pow(avgC, 7) / (Math.pow(avgC, 7) + Math.pow(25, 7))));
  const a1p = (1 + g) * left.a;
  const a2p = (1 + g) * right.a;
  const c1p = Math.sqrt(a1p * a1p + left.b * left.b);
  const c2p = Math.sqrt(a2p * a2p + right.b * right.b);
  const avgCp = (c1p + c2p) / 2;

  let h1p = Math.atan2(left.b, a1p);
  if (h1p < 0) h1p += 2 * Math.PI;
  let h2p = Math.atan2(right.b, a2p);
  if (h2p < 0) h2p += 2 * Math.PI;

  let avgHp = 0;
  if (Math.abs(h1p - h2p) > Math.PI) {
    avgHp = (h1p + h2p + 2 * Math.PI) / 2;
  } else {
    avgHp = (h1p + h2p) / 2;
  }

  let deltahp = 0;
  if (Math.abs(h1p - h2p) <= Math.PI) {
    deltahp = h2p - h1p;
  } else if (h2p <= h1p) {
    deltahp = h2p - h1p + 2 * Math.PI;
  } else {
    deltahp = h2p - h1p - 2 * Math.PI;
  }

  const deltaLp = right.l - left.l;
  const deltaCp = c2p - c1p;
  const deltaHp = 2 * Math.sqrt(c1p * c2p) * Math.sin(deltahp / 2);

  const t = 1
    - 0.17 * Math.cos(avgHp - degToRad(30))
    + 0.24 * Math.cos(2 * avgHp)
    + 0.32 * Math.cos(3 * avgHp + degToRad(6))
    - 0.2 * Math.cos(4 * avgHp - degToRad(63));

  const deltaTheta = degToRad(30) * Math.exp(-Math.pow((radToDeg(avgHp) - 275) / 25, 2));
  const rc = 2 * Math.sqrt(Math.pow(avgCp, 7) / (Math.pow(avgCp, 7) + Math.pow(25, 7)));
  const sl = 1 + (0.015 * Math.pow(avgLp - 50, 2)) / Math.sqrt(20 + Math.pow(avgLp - 50, 2));
  const sc = 1 + 0.045 * avgCp;
  const sh = 1 + 0.015 * avgCp * t;
  const rt = -Math.sin(2 * deltaTheta) * rc;

  return Math.sqrt(
    Math.pow(deltaLp / sl, 2)
      + Math.pow(deltaCp / sc, 2)
      + Math.pow(deltaHp / sh, 2)
      + rt * (deltaCp / sc) * (deltaHp / sh)
  );
}

function enrichPalette(palette) {
  return palette.map((color) => {
    const rgb = hexToRgb(color.hex);
    return {
      ...color,
      rgb,
      lab: rgbToLab(rgb),
    };
  });
}

const enrichedPalettes = Object.fromEntries(
  Object.entries(palettes).map(([brand, palette]) => [brand, enrichPalette(palette)])
);

function nearestColor(lab, palette) {
  let best = palette[0];
  let bestDistance = Number.POSITIVE_INFINITY;

  for (let index = 0; index < palette.length; index += 1) {
    const candidate = palette[index];
    const distance = deltaE2000(lab, candidate.lab);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = candidate;
    }
  }

  return best;
}

function rankPaletteColors(lab, palette, limit = palette.length) {
  return palette
    .map((color) => ({
      color,
      distance: deltaE2000(lab, color.lab),
    }))
    .sort((left, right) => left.distance - right.distance)
    .slice(0, limit);
}

function setCanvasSize(canvas, width, height) {
  const ratio = Math.max(window.devicePixelRatio || 1, 1);
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  canvas.style.width = `${width}px`;
  canvas.style.height = `${height}px`;

  const context = canvas.getContext("2d");
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return context;
}

function fitImageDimensions(image, width, height) {
  const ratio = Math.min(width / image.width, height / image.height);
  const drawWidth = image.width * ratio;
  const drawHeight = image.height * ratio;

  return {
    width: drawWidth,
    height: drawHeight,
    x: (width - drawWidth) / 2,
    y: (height - drawHeight) / 2,
  };
}

export function getPreviewCellSize({
  containerWidth,
  containerHeight,
  gridWidth,
  gridHeight,
  padding,
}) {
  const usableWidth = Math.max(1, containerWidth - padding * 2);
  const usableHeight = Math.max(1, containerHeight - padding * 2);
  const fitCell = Math.min(
    Math.floor(usableWidth / Math.max(1, gridWidth)),
    Math.floor(usableHeight / Math.max(1, gridHeight))
  );

  return Math.max(1, fitCell);
}

function getViewportContentSize(element) {
  if (!element) {
    return {
      height: 0,
      width: 0,
    };
  }

  const styles = window.getComputedStyle(element);
  const paddingX = (Number.parseFloat(styles.paddingLeft) || 0) + (Number.parseFloat(styles.paddingRight) || 0);
  const paddingY = (Number.parseFloat(styles.paddingTop) || 0) + (Number.parseFloat(styles.paddingBottom) || 0);

  return {
    width: Math.max(1, element.clientWidth - paddingX),
    height: Math.max(1, element.clientHeight - paddingY),
  };
}

function getBrightness(color) {
  return (color.rgb.r * 0.299 + color.rgb.g * 0.587 + color.rgb.b * 0.114) / 255;
}

function mixRgb(left, right, amount) {
  const blend = Math.max(0, Math.min(1, amount));
  return {
    r: Math.round(left.r + (right.r - left.r) * blend),
    g: Math.round(left.g + (right.g - left.g) * blend),
    b: Math.round(left.b + (right.b - left.b) * blend),
  };
}

function rgbToCss(rgb, alpha = 1) {
  return `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${alpha})`;
}

function getPreviewBeadFinish(color) {
  if (!color?.rgb) return null;

  const bright = getBrightness(color);
  const chroma = getRgbChroma(color.rgb);
  const lightLab = color.lab?.l ?? 0;
  const highlightMix = bright >= 0.76 ? 0.08 : bright <= 0.28 ? 0.04 : 0.06;
  const shadowMix = bright >= 0.82 ? 0.16 : bright <= 0.28 ? 0.28 : 0.2;
  const shadowTarget = bright <= 0.28
    ? { r: 10, g: 8, b: 6 }
    : { r: 34, g: 28, b: 20 };

  let rimColor = "rgba(36, 29, 22, 0.08)";
  let rimWidthFactor = 0.038;
  let boardShadowColor = "rgba(50, 40, 28, 0.08)";

  if (bright >= 0.84 || lightLab >= 86) {
    rimColor = "rgba(122, 105, 78, 0.24)";
    rimWidthFactor = 0.052;
    boardShadowColor = "rgba(86, 68, 45, 0.13)";
  } else if (bright >= 0.68 || chroma <= 0.22 || lightLab >= 72) {
    rimColor = "rgba(96, 80, 58, 0.14)";
    rimWidthFactor = 0.046;
    boardShadowColor = "rgba(70, 56, 38, 0.1)";
  } else if (bright <= 0.24) {
    rimColor = "rgba(255, 255, 255, 0.12)";
    rimWidthFactor = 0.04;
    boardShadowColor = "rgba(18, 14, 11, 0.08)";
  }

  return {
    boardShadowColor,
    highlightColor: rgbToCss(mixRgb(color.rgb, { r: 255, g: 255, b: 255 }, highlightMix), 0.96),
    shadowColor: rgbToCss(mixRgb(color.rgb, shadowTarget, shadowMix), 1),
    rimColor,
    rimWidthFactor,
  };
}

function findMatrixColorByCode(matrix, code) {
  if (!code) return null;
  for (let rowIndex = 0; rowIndex < matrix.length; rowIndex += 1) {
    const row = matrix[rowIndex];
    for (let columnIndex = 0; columnIndex < row.length; columnIndex += 1) {
      const color = row[columnIndex];
      if (color?.code === code) return color;
    }
  }
  return null;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function clampByte(value) {
  return Math.round(clamp(value, 0, 255));
}

function getChroma(lab) {
  return Math.sqrt(lab.a * lab.a + lab.b * lab.b);
}

function getRgbLuma(rgb) {
  return rgb.r * 0.299 + rgb.g * 0.587 + rgb.b * 0.114;
}

function getRgbChroma(rgb) {
  return (Math.max(rgb.r, rgb.g, rgb.b) - Math.min(rgb.r, rgb.g, rgb.b)) / 255;
}

function blendRgb(base, accent, amount) {
  const ratio = clamp(amount, 0, 1);
  return {
    r: clampByte(base.r * (1 - ratio) + accent.r * ratio),
    g: clampByte(base.g * (1 - ratio) + accent.g * ratio),
    b: clampByte(base.b * (1 - ratio) + accent.b * ratio),
  };
}

function readCompositedRgb(data, index) {
  const alpha = data[index + 3] / 255;
  if (alpha >= 0.999) {
    return {
      r: data[index],
      g: data[index + 1],
      b: data[index + 2],
    };
  }

  return {
    r: Math.round(data[index] * alpha + 255 * (1 - alpha)),
    g: Math.round(data[index + 1] * alpha + 255 * (1 - alpha)),
    b: Math.round(data[index + 2] * alpha + 255 * (1 - alpha)),
  };
}

function getLabFromCache(rgb, cache) {
  const key = `${rgb.r},${rgb.g},${rgb.b}`;
  const cached = cache.get(key);
  if (cached) return cached;

  const lab = rgbToLab(rgb);
  cache.set(key, lab);
  return lab;
}

function getPixelImportance(lab, edgeStrength) {
  const chromaBoost = Math.min(getChroma(lab) / 90, 1) * 0.55;
  const edgeBoost = Math.min(edgeStrength / 90, 1) * 0.42;
  const contrastBoost = Math.min(Math.abs(lab.l - 56) / 56, 1) * 0.24;
  return 1 + chromaBoost + edgeBoost + contrastBoost;
}

export function summarizeCellSamples(samples) {
  const visibleSamples = samples.filter((sample) => sample && (sample.alpha ?? 255) > 0);
  if (!visibleSamples.length) return null;

  let totalWeight = 0;
  let totalR = 0;
  let totalG = 0;
  let totalB = 0;
  let totalLuma = 0;
  let totalChroma = 0;
  let darkest = visibleSamples[0];
  let vividest = visibleSamples[0];

  visibleSamples.forEach((sample) => {
    const weight = sample.weight ?? 1;
    const luma = getRgbLuma(sample.rgb);
    const chroma = getRgbChroma(sample.rgb);

    totalWeight += weight;
    totalR += sample.rgb.r * weight;
    totalG += sample.rgb.g * weight;
    totalB += sample.rgb.b * weight;
    totalLuma += luma * weight;
    totalChroma += chroma * weight;

    if (luma < getRgbLuma(darkest.rgb)) {
      darkest = sample;
    }
    if (chroma > getRgbChroma(vividest.rgb)) {
      vividest = sample;
    }
  });

  const averageRgb = {
    r: clampByte(totalR / totalWeight),
    g: clampByte(totalG / totalWeight),
    b: clampByte(totalB / totalWeight),
  };
  const averageLuma = totalLuma / totalWeight;
  const averageChroma = totalChroma / totalWeight;

  const variance = visibleSamples.reduce((sum, sample) => {
    const lumaDelta = getRgbLuma(sample.rgb) - averageLuma;
    return sum + lumaDelta * lumaDelta;
  }, 0) / visibleSamples.length;
  const lumaContrast = Math.sqrt(variance);
  const darkThreshold = Math.max(24, lumaContrast * 0.72);
  const darkCoverage = visibleSamples.filter((sample) => {
    const luma = getRgbLuma(sample.rgb);
    return luma < averageLuma - darkThreshold && luma < 150;
  }).length / visibleSamples.length;
  const vividCoverage = visibleSamples.filter((sample) => {
    return getRgbChroma(sample.rgb) > averageChroma + 0.14;
  }).length / visibleSamples.length;

  let rgb = averageRgb;

  // Preserve small but intentional details such as eyes, mouth lines and outlines before palette reduction.
  if (darkCoverage >= 0.055 && lumaContrast >= 28 && getRgbLuma(darkest.rgb) < averageLuma - 24) {
    const amount = clamp(0.26 + lumaContrast / 220, 0.32, 0.58);
    rgb = blendRgb(rgb, darkest.rgb, amount);
  }

  if (
    vividCoverage >= 0.12
    && lumaContrast >= 18
    && getRgbChroma(vividest.rgb) > averageChroma + 0.12
    && getRgbLuma(vividest.rgb) > 36
  ) {
    const amount = clamp(0.18 + getRgbChroma(vividest.rgb) * 0.24, 0.22, 0.4);
    rgb = blendRgb(rgb, vividest.rgb, amount);
  }

  return {
    rgb,
    detailScore: lumaContrast + Math.max(0, getRgbChroma(vividest.rgb) - averageChroma) * 80,
    visibleRatio: visibleSamples.length / Math.max(samples.length, 1),
  };
}

function getOversampleScale(width, height) {
  const largestSide = Math.max(width, height);
  if (largestSide <= 48) return 4;
  if (largestSide <= 80) return 3;
  return 2;
}

function buildCellSummaries({
  drawableSource,
  width,
  height,
  transparentBackground,
  emptyAlphaThreshold,
}) {
  const oversample = getOversampleScale(width, height);
  const sampleWidth = width * oversample;
  const sampleHeight = height * oversample;
  const workCanvas = document.createElement("canvas");
  workCanvas.width = sampleWidth;
  workCanvas.height = sampleHeight;
  const context = workCanvas.getContext("2d", { willReadFrequently: true });
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.clearRect(0, 0, sampleWidth, sampleHeight);
  if (!transparentBackground) {
    context.fillStyle = "#ffffff";
    context.fillRect(0, 0, sampleWidth, sampleHeight);
  }
  context.drawImage(drawableSource, 0, 0, sampleWidth, sampleHeight);

  const data = context.getImageData(0, 0, sampleWidth, sampleHeight).data;
  const summaries = [];

  for (let row = 0; row < height; row += 1) {
    for (let column = 0; column < width; column += 1) {
      const samples = [];

      for (let sampleY = 0; sampleY < oversample; sampleY += 1) {
        for (let sampleX = 0; sampleX < oversample; sampleX += 1) {
          const x = column * oversample + sampleX;
          const y = row * oversample + sampleY;
          const index = (y * sampleWidth + x) * 4;
          const alpha = data[index + 3];

          if (transparentBackground && alpha <= emptyAlphaThreshold) {
            samples.push({ alpha: 0, rgb: { r: 255, g: 255, b: 255 } });
            continue;
          }

          const centerX = (sampleX + 0.5) / oversample - 0.5;
          const centerY = (sampleY + 0.5) / oversample - 0.5;
          const centerDistance = Math.sqrt(centerX * centerX + centerY * centerY);
          samples.push({
            alpha,
            rgb: readCompositedRgb(data, index),
            weight: 1 + clamp(0.42 - centerDistance, 0, 0.42),
          });
        }
      }

      const summary = summarizeCellSamples(samples);
      if (transparentBackground && summary && summary.visibleRatio < 0.08) {
        summaries.push(null);
      } else {
        summaries.push(summary);
      }
    }
  }

  return summaries;
}

function recordPaletteUsage(usageMap, color, weight) {
  const current = usageMap.get(color.code) || {
    color,
    count: 0,
    weight: 0,
  };

  current.count += 1;
  current.weight += weight;
  usageMap.set(color.code, current);
}

function recordPaletteCandidates(usageMap, lab, palette, importance) {
  const ranked = rankPaletteColors(lab, palette, 5);
  const bestDistance = ranked[0]?.distance ?? 0;

  ranked.forEach((candidate, index) => {
    const nearEnough = candidate.distance <= bestDistance + 18 || index <= 1;
    if (!nearEnough) return;

    const rankWeight = [1, 0.48, 0.28, 0.16, 0.1][index] || 0.08;
    const distanceWeight = 1 / (1 + Math.max(0, candidate.distance - bestDistance) / 12);
    recordPaletteUsage(usageMap, candidate.color, importance * rankWeight * distanceWeight);
  });
}

function addSelectedColor(selected, usage, limit) {
  if (!usage || selected.size >= limit) return;
  selected.set(usage.color.code, usage.color);
}

function getMinPaletteDistance(color, selectedColors) {
  if (!selectedColors.length) return Number.POSITIVE_INFINITY;
  return selectedColors.reduce((shortest, selectedColor) => (
    Math.min(shortest, deltaE2000(color.lab, selectedColor.lab))
  ), Number.POSITIVE_INFINITY);
}

function getPaletteCompressionThresholds(limit, usageCount) {
  const compressionRatio = clamp(limit / Math.max(usageCount, 1), 0.14, 1);
  return {
    coverageDistance: 22 + (1 - compressionRatio) * 14,
    hardDistance: 8 + (1 - compressionRatio) * 12,
    softDistance: 18 + (1 - compressionRatio) * 14,
  };
}

function getSimilarityPenalty(distance, thresholds) {
  if (!Number.isFinite(distance)) return 1;
  if (distance <= thresholds.hardDistance) return 0.18;
  if (distance >= thresholds.softDistance) return 1;

  const progress = (distance - thresholds.hardDistance)
    / Math.max(thresholds.softDistance - thresholds.hardDistance, 1);

  return 0.18 + progress * 0.82;
}

function getPaletteCoverageScore(candidateUsage, rankedUsages, selected, thresholds) {
  let score = 0;

  rankedUsages.forEach((usage) => {
    if (!usage || selected.has(usage.color.code) || usage.color.code === candidateUsage.color.code) return;

    const distance = deltaE2000(candidateUsage.color.lab, usage.color.lab);
    if (distance > thresholds.coverageDistance) return;

    score += usage.weight * (1 - distance / thresholds.coverageDistance);
  });

  return score * 0.18;
}

export function selectActivePaletteForUsage(usages, maxColors, totalPixels) {
  const limit = Math.max(1, Math.min(Math.round(maxColors), usages.length));
  const selected = new Map();
  const meaningfulCount = Math.max(1, Math.ceil(totalPixels * 0.0015));
  const significant = usages.filter((usage) => usage.count / Math.max(totalPixels, 1) >= 0.012);
  const detailCandidates = usages.filter((usage) => usage.count >= meaningfulCount);
  const ranked = usages.slice().sort((left, right) => right.weight - left.weight);
  const thresholds = getPaletteCompressionThresholds(limit, usages.length);

  const darkAnchor = detailCandidates
    .filter((usage) => usage.color.lab.l <= 34)
    .sort((left, right) => left.color.lab.l - right.color.lab.l || right.weight - left.weight)[0];
  const lightAnchor = significant
    .filter((usage) => usage.color.lab.l >= 78)
    .sort((left, right) => right.color.lab.l - left.color.lab.l || right.weight - left.weight)[0];
  const vividAnchor = detailCandidates
    .filter((usage) => getChroma(usage.color.lab) >= 34)
    .sort((left, right) => getChroma(right.color.lab) - getChroma(left.color.lab) || right.weight - left.weight)[0];

  addSelectedColor(selected, ranked[0], limit);
  addSelectedColor(selected, darkAnchor, limit);
  addSelectedColor(selected, vividAnchor, limit);
  addSelectedColor(selected, lightAnchor, limit);

  while (selected.size < limit) {
    const selectedColors = [...selected.values()];
    let nextUsage = null;
    let nextScore = Number.NEGATIVE_INFINITY;

    ranked.forEach((usage) => {
      if (!usage || selected.has(usage.color.code)) return;

      const minDistance = getMinPaletteDistance(usage.color, selectedColors);
      const similarityPenalty = getSimilarityPenalty(minDistance, thresholds);
      const coverageScore = getPaletteCoverageScore(usage, ranked, selected, thresholds);
      const candidateScore = usage.weight * similarityPenalty + coverageScore;

      if (candidateScore > nextScore) {
        nextScore = candidateScore;
        nextUsage = usage;
      }
    });

    if (!nextUsage) break;
    addSelectedColor(selected, nextUsage, limit);
  }

  return [...selected.values()];
}

function getNeighborMajority(matrix, width, height, row, column) {
  const current = matrix[row][column];
  const counts = new Map();
  let currentNeighborCount = 0;
  let neighborCount = 0;

  for (let y = Math.max(0, row - 1); y <= Math.min(height - 1, row + 1); y += 1) {
    for (let x = Math.max(0, column - 1); x <= Math.min(width - 1, column + 1); x += 1) {
      if (y === row && x === column) continue;

      const color = matrix[y][x];
      if (!color) continue;

      neighborCount += 1;
      if (current && color.code === current.code) {
        currentNeighborCount += 1;
      }

      counts.set(color.code, {
        color,
        count: (counts.get(color.code)?.count || 0) + 1,
      });
    }
  }

  const majority = [...counts.values()].sort((left, right) => right.count - left.count)[0];
  return {
    majority,
    neighborCount,
    currentNeighborCount,
  };
}

export function smoothIsolatedCells(matrix, width, height) {
  if (width < 3 || height < 3 || !matrix.length) return matrix;

  return matrix.map((row, rowIndex) => row.map((color, columnIndex) => {
    if (!color) return color;

    const {
      majority,
      neighborCount,
      currentNeighborCount,
    } = getNeighborMajority(matrix, width, height, rowIndex, columnIndex);

    if (!majority || majority.color.code === color.code) return color;

    const majorityThreshold = neighborCount >= 8 ? 5 : Math.ceil(neighborCount * 0.72);
    const isIsolated = currentNeighborCount <= 1;
    const isNearEnough = deltaE2000(color.lab, majority.color.lab) <= 24;

    if (isIsolated && majority.count >= majorityThreshold && isNearEnough) {
      return majority.color;
    }

    return color;
  }));
}

function getOuterScore(rowIndex, columnIndex, width, height) {
  const edgeDistance = Math.min(
    rowIndex,
    columnIndex,
    height - 1 - rowIndex,
    width - 1 - columnIndex
  );
  const maxDistance = Math.max(1, Math.min(width, height) / 2);
  return 1 - Math.min(edgeDistance / maxDistance, 1);
}

export function sortCountsOuterFirst(counts) {
  return counts.slice().sort((left, right) => {
    const outerDiff = (right.outerScore || 0) - (left.outerScore || 0);
    if (Math.abs(outerDiff) > 0.08) return outerDiff;
    return right.count - left.count;
  });
}

function buildCountsFromMatrix(matrix, width, height) {
  const counts = new Map();

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      if (!color) return;

      const outerScore = getOuterScore(rowIndex, columnIndex, width, height);
      const current = counts.get(color.code);
      counts.set(color.code, {
        ...color,
        count: (current?.count || 0) + 1,
        outerWeight: (current?.outerWeight || 0) + outerScore,
        outerScore: ((current?.outerWeight || 0) + outerScore) / ((current?.count || 0) + 1),
      });
    });
  });

  return sortCountsOuterFirst([...counts.values()]);
}

function drawSampleScene(context, width, height) {
  context.clearRect(0, 0, width, height);

  const gradient = context.createLinearGradient(0, 0, width, height);
  gradient.addColorStop(0, "#fff5d5");
  gradient.addColorStop(0.45, "#c9ffe0");
  gradient.addColorStop(1, "#ff8465");
  context.fillStyle = gradient;
  context.fillRect(0, 0, width, height);

  context.fillStyle = "#161616";
  context.fillRect(0, height * 0.79, width, height * 0.21);

  context.fillStyle = "#ffe25a";
  context.beginPath();
  context.arc(width * 0.8, height * 0.18, width * 0.12, 0, Math.PI * 2);
  context.fill();

  context.fillStyle = "#ff6a42";
  context.beginPath();
  context.arc(width * 0.27, height * 0.34, width * 0.15, 0, Math.PI * 2);
  context.fill();

  context.fillStyle = "#fff5ea";
  context.beginPath();
  context.arc(width * 0.27, height * 0.34, width * 0.09, 0, Math.PI * 2);
  context.fill();

  context.fillStyle = "#12a97f";
  context.beginPath();
  context.moveTo(width * 0.06, height * 0.84);
  context.lineTo(width * 0.34, height * 0.3);
  context.lineTo(width * 0.5, height * 0.84);
  context.closePath();
  context.fill();

  context.fillStyle = "#4f78ff";
  context.beginPath();
  context.moveTo(width * 0.3, height * 0.84);
  context.lineTo(width * 0.56, height * 0.42);
  context.lineTo(width * 0.82, height * 0.84);
  context.closePath();
  context.fill();

  context.fillStyle = "#161616";
  context.beginPath();
  context.moveTo(width * 0.57, height * 0.74);
  context.bezierCurveTo(width * 0.58, height * 0.48, width * 0.68, height * 0.48, width * 0.72, height * 0.74);
  context.lineTo(width * 0.57, height * 0.74);
  context.fill();

  context.fillStyle = "#fffaf0";
  context.fillRect(width * 0.16, height * 0.58, width * 0.18, height * 0.12);

  context.fillStyle = "#161616";
  context.font = `800 ${Math.max(26, width * 0.05)}px "Bricolage Grotesque", sans-serif`;
  context.fillText("POP", width * 0.56, height * 0.17);
}

export function createSampleDataUrl() {
  const canvas = document.createElement("canvas");
  canvas.width = 900;
  canvas.height = 1180;
  const context = canvas.getContext("2d");
  drawSampleScene(context, canvas.width, canvas.height);
  return canvas.toDataURL("image/png");
}

export function getScaledGridSize(drawableSource, targetMaxSize) {
  const maxSize = Math.max(1, Math.round(Number(targetMaxSize) || 1));
  if (!drawableSource?.width || !drawableSource?.height) {
    return { width: maxSize, height: maxSize };
  }

  const scale = maxSize / Math.max(drawableSource.width, drawableSource.height);
  return {
    width: Math.max(1, Math.round(drawableSource.width * scale)),
    height: Math.max(1, Math.round(drawableSource.height * scale)),
  };
}

export function processImage({
  source,
  image,
  brand,
  maxColors,
  targetWidth,
  transparentBackground = false,
  emptyAlphaThreshold = 12,
}) {
  const drawableSource = source || image;
  const { width, height } = getScaledGridSize(drawableSource, targetWidth);
  const cellSummaries = buildCellSummaries({
    drawableSource,
    width,
    height,
    transparentBackground,
    emptyAlphaThreshold,
  });
  const fullPalette = enrichedPalettes[brand];
  const paletteUsage = new Map();
  const sourceCells = [];
  const labCache = new Map();

  for (let row = 0; row < height; row += 1) {
    for (let column = 0; column < width; column += 1) {
      const summary = cellSummaries[row * width + column];
      if (!summary) {
        sourceCells.push(null);
        continue;
      }

      const lab = getLabFromCache(summary.rgb, labCache);
      const importance = getPixelImportance(lab, summary.detailScore);

      sourceCells.push({ detailScore: summary.detailScore, lab });
      recordPaletteCandidates(paletteUsage, lab, fullPalette, importance);
    }
  }

  const paletteUsages = [...paletteUsage.values()];
  const activePalette = selectActivePaletteForUsage(
    paletteUsages,
    Math.min(maxColors, fullPalette.length),
    sourceCells.filter(Boolean).length
  );

  if (!activePalette.length) {
    activePalette.push(...fullPalette.slice(0, Math.min(maxColors, fullPalette.length)));
  }

  const matrix = [];

  for (let row = 0; row < height; row += 1) {
    const currentRow = [];
    for (let column = 0; column < width; column += 1) {
      const cell = sourceCells[row * width + column];
      if (!cell) {
        currentRow.push(null);
        continue;
      }

      const rankedColors = rankPaletteColors(cell.lab, activePalette, 2);
      const primary = rankedColors[0]?.color || null;
      const secondary = rankedColors[1];
      const canDither = activePalette.length >= 18
        && secondary
        && secondary.distance <= rankedColors[0].distance + Math.min(9, 4 + cell.detailScore / 18);
      const bayer = [
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
      ][row % 4][column % 4] / 16;
      const ditherStrength = Math.min(0.34, Math.max(0.08, cell.detailScore / 260));

      currentRow.push(canDither && bayer < ditherStrength ? secondary.color : primary);
    }
    matrix.push(currentRow);
  }

  const cleanedMatrix = activePalette.length >= 32
    ? matrix
    : smoothIsolatedCells(smoothIsolatedCells(matrix, width, height), width, height);
  const counts = buildCountsFromMatrix(cleanedMatrix, width, height);

  return {
    width,
    height,
    matrix: cleanedMatrix,
    counts,
  };
}

export function renderSourceCanvas(canvas, { image, source, frameLabel = "", clean = false }) {
  if (!canvas) return;
  const width = canvas.clientWidth || 320;
  const height = canvas.clientHeight || Math.round(width * 1.16);
  const context = setCanvasSize(canvas, width, height);

  if (!clean) {
    const background = context.createLinearGradient(0, 0, width, height);
    background.addColorStop(0, "rgba(255, 255, 255, 0.92)");
    background.addColorStop(1, "rgba(255, 244, 235, 0.86)");
    context.fillStyle = background;
    context.fillRect(0, 0, width, height);
  } else {
    context.clearRect(0, 0, width, height);
  }

  const drawableSource = source || image;
  if (!drawableSource) return;

  const fit = fitImageDimensions(drawableSource, width - 24, height - 24);
  if (!clean) {
    context.save();
    context.translate(12, 12);
    context.shadowColor = "rgba(22, 22, 22, 0.14)";
    context.shadowBlur = 24;
    context.shadowOffsetY = 18;
    context.fillStyle = "rgba(255, 255, 255, 0.82)";
    context.fillRect(fit.x, fit.y, fit.width, fit.height);
    context.restore();
  }

  context.drawImage(drawableSource, fit.x + 12, fit.y + 12, fit.width, fit.height);

  if (frameLabel) {
    context.fillStyle = "rgba(22, 22, 22, 0.55)";
    context.font = '600 12px "IBM Plex Mono", monospace';
    context.fillText(frameLabel, 18, height - 18);
  }
}

function buildVisibleArtworkBounds({
  matrix,
  gridWidth,
  gridHeight,
  hiddenCodes,
  hiddenCells,
  focusColorCode = "",
  centerVisible = false,
  cropBackground = false,
  backgroundDeltaE = 18,
  visiblePaddingCells = 0,
}) {
  const cropBackgroundCells = new Set();
  if (!centerVisible || !matrix.length || !gridWidth || !gridHeight) {
    return {
      cropBackgroundCells,
      visibleBounds: null,
    };
  }

  const focusColor = findMatrixColorByCode(matrix, focusColorCode) || matrix[0]?.[0] || null;
  let visibleBounds = null;

  if (cropBackground && focusColor) {
    const queue = [];
    const backgroundCandidateCodes = new Set([focusColor.code]);
    const edgeCounts = new Map();
    const edgeColors = new Map();
    let edgeSamples = 0;
    const collectEdge = (rowIndex, columnIndex) => {
      const key = rowIndex * gridWidth + columnIndex;
      const color = matrix[rowIndex]?.[columnIndex];
      if (!color || hiddenCodes.has(color.code) || hiddenCells.has(key)) return;
      edgeSamples += 1;
      edgeCounts.set(color.code, (edgeCounts.get(color.code) || 0) + 1);
      edgeColors.set(color.code, color);
    };

    for (let columnIndex = 0; columnIndex < gridWidth; columnIndex += 1) {
      collectEdge(0, columnIndex);
    }
    for (let rowIndex = 1; rowIndex < gridHeight; rowIndex += 1) {
      collectEdge(rowIndex, 0);
      collectEdge(rowIndex, gridWidth - 1);
    }

    edgeCounts.forEach((count, code) => {
      const color = edgeColors.get(code);
      const ratio = count / Math.max(edgeSamples, 1);
      const closeToFocus = color?.lab && focusColor.lab && deltaE2000(color.lab, focusColor.lab) <= 20;
      const commonEdgeBackground = ratio >= 0.08;
      const quietLightColor = color?.lab && color.lab.l >= 68 && getChroma(color.lab) <= 38;
      if (closeToFocus || ratio >= 0.18 || (commonEdgeBackground && quietLightColor)) {
        backgroundCandidateCodes.add(code);
      }
    });

    const isCropBackground = (color) => {
      if (!color) return false;
      if (hiddenCodes.has(color.code)) return false;
      if (backgroundCandidateCodes.has(color.code)) return true;
      if (color.code === focusColor.code) return true;
      if (!color.lab || !focusColor.lab) return false;
      return deltaE2000(color.lab, focusColor.lab) <= backgroundDeltaE;
    };
    const enqueue = (rowIndex, columnIndex) => {
      if (
        rowIndex < 0
        || rowIndex >= gridHeight
        || columnIndex < 0
        || columnIndex >= gridWidth
      ) {
        return;
      }

      const key = rowIndex * gridWidth + columnIndex;
      if (cropBackgroundCells.has(key) || hiddenCells.has(key)) return;
      if (!isCropBackground(matrix[rowIndex]?.[columnIndex])) return;

      cropBackgroundCells.add(key);
      queue.push([rowIndex, columnIndex]);
    };

    for (let columnIndex = 0; columnIndex < gridWidth; columnIndex += 1) {
      enqueue(0, columnIndex);
    }
    for (let rowIndex = 1; rowIndex < gridHeight; rowIndex += 1) {
      enqueue(rowIndex, 0);
      enqueue(rowIndex, gridWidth - 1);
    }

    for (let index = 0; index < queue.length; index += 1) {
      const [rowIndex, columnIndex] = queue[index];
      enqueue(rowIndex - 1, columnIndex);
      enqueue(rowIndex + 1, columnIndex);
      enqueue(rowIndex, columnIndex - 1);
      enqueue(rowIndex, columnIndex + 1);
    }
  }

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      const key = rowIndex * gridWidth + columnIndex;
      if (!color) return;
      if (hiddenCodes.has(color.code)) return;
      if (hiddenCells.has(key)) return;
      if (cropBackgroundCells.has(key)) return;

      if (!visibleBounds) {
        visibleBounds = {
          maxColumn: columnIndex,
          maxRow: rowIndex,
          minColumn: columnIndex,
          minRow: rowIndex,
        };
        return;
      }

      visibleBounds.minColumn = Math.min(visibleBounds.minColumn, columnIndex);
      visibleBounds.maxColumn = Math.max(visibleBounds.maxColumn, columnIndex);
      visibleBounds.minRow = Math.min(visibleBounds.minRow, rowIndex);
      visibleBounds.maxRow = Math.max(visibleBounds.maxRow, rowIndex);
    });
  });

  if (visibleBounds && visiblePaddingCells > 0) {
    const cellPadding = Math.max(0, Math.round(visiblePaddingCells));
    visibleBounds = {
      minColumn: Math.max(0, visibleBounds.minColumn - cellPadding),
      maxColumn: Math.min(gridWidth - 1, visibleBounds.maxColumn + cellPadding),
      minRow: Math.max(0, visibleBounds.minRow - cellPadding),
      maxRow: Math.min(gridHeight - 1, visibleBounds.maxRow + cellPadding),
    };
  }

  return {
    cropBackgroundCells,
    visibleBounds,
  };
}

export function renderBeadCanvas(canvas, {
  matrix,
  width: gridWidth,
  height: gridHeight,
  roundBeads,
  fitToViewport = false,
  centerVisible = false,
  hero = false,
  showGrid = false,
  transparentBackground = false,
  hiddenColorCodes = [],
  hiddenCellKeys = [],
  highlightColorCodes = [],
  focusColorCode = "",
  viewportPadding = null,
  backgroundDeltaE = 18,
  visiblePaddingCells = 0,
}) {
  if (!canvas || !matrix.length || !gridWidth || !gridHeight) return;

  const viewportElement = fitToViewport ? canvas.parentElement : null;
  const viewportSize = fitToViewport ? getViewportContentSize(viewportElement) : null;
  const width = viewportSize?.width || viewportElement?.clientWidth || canvas.clientWidth || 320;
  const availableHeight = viewportSize?.height || viewportElement?.clientHeight || canvas.clientHeight || Math.round(width * 1.04);
  const padding = Number.isFinite(viewportPadding)
    ? Math.max(0, viewportPadding)
    : hero ? 16 : 20;
  const hiddenCodes = new Set(hiddenColorCodes);
  const hiddenCells = new Set(hiddenCellKeys);
  const highlightCodes = new Set(highlightColorCodes);
  const hasHighlights = highlightCodes.size > 0;
  const {
    cropBackgroundCells,
    visibleBounds,
  } = buildVisibleArtworkBounds({
    matrix,
    gridWidth,
    gridHeight,
    hiddenCodes,
    hiddenCells,
    focusColorCode,
    centerVisible: centerVisible && fitToViewport,
    cropBackground: centerVisible && fitToViewport,
    backgroundDeltaE,
    visiblePaddingCells,
  });

  const fitGridWidth = visibleBounds
    ? visibleBounds.maxColumn - visibleBounds.minColumn + 1
    : gridWidth;
  const fitGridHeight = visibleBounds
    ? visibleBounds.maxRow - visibleBounds.minRow + 1
    : gridHeight;
  const drawMinColumn = visibleBounds ? visibleBounds.minColumn : 0;
  const drawMaxColumn = visibleBounds ? visibleBounds.maxColumn : gridWidth - 1;
  const drawMinRow = visibleBounds ? visibleBounds.minRow : 0;
  const drawMaxRow = visibleBounds ? visibleBounds.maxRow : gridHeight - 1;
  const cell = fitToViewport
    ? getPreviewCellSize({
      containerWidth: width,
      containerHeight: availableHeight,
      gridWidth: fitGridWidth,
      gridHeight: fitGridHeight,
      padding,
    })
    : Math.max(1, Math.floor(Math.max(1, width - padding * 2) / gridWidth));
  const artworkWidth = cell * fitGridWidth;
  const artworkHeight = cell * fitGridHeight;
  const height = fitToViewport ? availableHeight : artworkHeight + padding * 2;
  const context = setCanvasSize(canvas, width, height);
  const offsetX = Math.floor((width - artworkWidth) / 2);
  const verticalSpace = Math.max(0, height - artworkHeight);
  const offsetY = fitToViewport
    ? Math.floor(verticalSpace * (hero ? 0.38 : 0.5))
    : padding;

  context.clearRect(0, 0, width, height);
  if (!transparentBackground) {
    context.fillStyle = "rgba(255, 255, 255, 0.98)";
    context.fillRect(0, 0, width, height);
  }

  matrix.forEach((row, rowIndex) => {
    if (rowIndex < drawMinRow || rowIndex > drawMaxRow) return;

    row.forEach((color, columnIndex) => {
      if (columnIndex < drawMinColumn || columnIndex > drawMaxColumn) return;

      const key = rowIndex * gridWidth + columnIndex;
      const x = offsetX + (columnIndex - drawMinColumn) * cell;
      const y = offsetY + (rowIndex - drawMinRow) * cell;
      const radius = cell / 2;
      const isHiddenCell = hiddenCells.has(key) || cropBackgroundCells.has(key);
      const shouldDrawBead = Boolean(color && !hiddenCodes.has(color.code) && !isHiddenCell);

      if (!transparentBackground) {
        context.globalAlpha = 1;
        context.fillStyle = "#ffffff";
        context.fillRect(x, y, cell, cell);
      }

      if (!shouldDrawBead) return;

      const isHighlighted = highlightCodes.has(color.code);
      const beadAlpha = hasHighlights && !isHighlighted ? 0.16 : 1;
      const beadFinish = !showGrid ? getPreviewBeadFinish(color) : null;

      if (roundBeads) {
        context.globalAlpha = beadAlpha;
        if (!showGrid && beadFinish) {
          context.fillStyle = beadFinish.boardShadowColor;
          context.beginPath();
          context.arc(x + radius, y + radius + cell * 0.06, Math.max(1.6, radius - 0.6), 0, Math.PI * 2);
          context.fill();

          const beadFill = context.createRadialGradient(
            x + radius - cell * 0.18,
            y + radius - cell * 0.22,
            Math.max(1, radius * 0.2),
            x + radius,
            y + radius,
            Math.max(2, radius)
          );
          beadFill.addColorStop(0, beadFinish.highlightColor);
          beadFill.addColorStop(0.58, color.hex);
          beadFill.addColorStop(1, beadFinish.shadowColor);
          context.fillStyle = beadFill;
        } else {
          context.fillStyle = color.hex;
        }

        context.beginPath();
        context.arc(x + radius, y + radius, Math.max(2, radius - 0.8), 0, Math.PI * 2);
        context.fill();
      } else {
        context.globalAlpha = beadAlpha;
        if (!showGrid && beadFinish) {
          context.fillStyle = beadFinish.boardShadowColor;
          context.fillRect(x + 1.6, y + 2.2, Math.max(2, cell - 3.2), Math.max(2, cell - 3.2));

          const squareFill = context.createLinearGradient(x + 1, y + 1, x + cell - 1, y + cell - 1);
          squareFill.addColorStop(0, beadFinish.highlightColor);
          squareFill.addColorStop(0.52, color.hex);
          squareFill.addColorStop(1, beadFinish.shadowColor);
          context.fillStyle = squareFill;
        } else {
          context.fillStyle = color.hex;
        }

        context.fillRect(x + 1, y + 1, Math.max(2, cell - 2), Math.max(2, cell - 2));
      }

      if (!showGrid && beadFinish) {
        context.globalAlpha = beadAlpha;
        context.strokeStyle = beadFinish.rimColor;
        context.lineWidth = Math.max(0.75, cell * beadFinish.rimWidthFactor);
        if (roundBeads) {
          context.beginPath();
          context.arc(x + radius, y + radius, Math.max(2, radius - 0.6), 0, Math.PI * 2);
          context.stroke();
        } else {
          context.strokeRect(x + 1.2, y + 1.2, Math.max(2, cell - 2.4), Math.max(2, cell - 2.4));
        }
      }

      if (!hero && showGrid && cell >= 16) {
        context.fillStyle = getBrightness(color) > 0.62 ? "#17181a" : "#ffffff";
        context.font = `700 ${Math.max(7, Math.floor(cell * 0.28))}px "IBM Plex Mono", monospace`;
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.fillText(color.code, x + radius, y + radius + 0.5);
      }

      if (hasHighlights && isHighlighted) {
        context.globalAlpha = 1;
        context.strokeStyle = "rgba(8, 173, 134, 0.92)";
        context.lineWidth = Math.max(1.6, cell * 0.13);
        if (roundBeads) {
          context.beginPath();
          context.arc(x + radius, y + radius, Math.max(2, radius - 0.3), 0, Math.PI * 2);
          context.stroke();
        } else {
          context.strokeRect(x + 1.5, y + 1.5, Math.max(2, cell - 3), Math.max(2, cell - 3));
        }
      }
    });
  });

  if (!hero && showGrid) {
    context.globalAlpha = 1;
    context.strokeStyle = "rgba(16, 36, 31, 0.12)";
    context.lineWidth = 1;
    for (let index = 0; index <= fitGridWidth; index += 1) {
      const x = offsetX + index * cell;
      context.beginPath();
      context.moveTo(x, offsetY);
      context.lineTo(x, offsetY + artworkHeight);
      context.stroke();
    }
    for (let index = 0; index <= fitGridHeight; index += 1) {
      const y = offsetY + index * cell;
      context.beginPath();
      context.moveTo(offsetX, y);
      context.lineTo(offsetX + artworkWidth, y);
      context.stroke();
    }

    context.strokeStyle = "rgba(255, 90, 54, 0.44)";
    context.lineWidth = 2;
    for (let index = Math.ceil((drawMinColumn + 1) / 10) * 10; index <= drawMaxColumn; index += 10) {
      const x = offsetX + (index - drawMinColumn) * cell;
      context.beginPath();
      context.moveTo(x, offsetY);
      context.lineTo(x, offsetY + artworkHeight);
      context.stroke();
    }
    for (let index = Math.ceil((drawMinRow + 1) / 10) * 10; index <= drawMaxRow; index += 10) {
      const y = offsetY + (index - drawMinRow) * cell;
      context.beginPath();
      context.moveTo(offsetX, y);
      context.lineTo(offsetX + artworkWidth, y);
      context.stroke();
    }
  }
  context.globalAlpha = 1;
}

export function renderPatternCanvas(canvas, {
  matrix,
  width: gridWidth,
  height: gridHeight,
  roundBeads,
  showCodes,
  centerVisible = false,
  focusColorCode = "",
  hero = false,
  fitToViewport = false,
  transparentBackground = false,
  hiddenColorCodes = [],
  hiddenCellKeys = [],
  highlightColorCodes = [],
  viewportPadding = null,
  visiblePaddingCells = 0,
}) {
  if (!canvas || !matrix.length || !gridWidth || !gridHeight) return;

  const viewportElement = fitToViewport ? canvas.parentElement : null;
  const viewportSize = fitToViewport ? getViewportContentSize(viewportElement) : null;
  const width = viewportSize?.width || viewportElement?.clientWidth || canvas.clientWidth || 320;
  const availableHeight = viewportSize?.height || viewportElement?.clientHeight || canvas.clientHeight || Math.round(width * 1.04);
  const padding = Number.isFinite(viewportPadding)
    ? Math.max(0, viewportPadding)
    : hero ? 16 : 20;
  const hiddenCodes = new Set(hiddenColorCodes);
  const hiddenCells = new Set(hiddenCellKeys);
  const highlightCodes = new Set(highlightColorCodes);
  const hasHighlights = highlightCodes.size > 0;
  const {
    cropBackgroundCells,
    visibleBounds,
  } = buildVisibleArtworkBounds({
    matrix,
    gridWidth,
    gridHeight,
    hiddenCodes,
    hiddenCells,
    focusColorCode,
    centerVisible: centerVisible && fitToViewport,
    cropBackground: centerVisible && fitToViewport,
    backgroundDeltaE: 18,
    visiblePaddingCells,
  });

  const fitGridWidth = visibleBounds
    ? visibleBounds.maxColumn - visibleBounds.minColumn + 1
    : gridWidth;
  const fitGridHeight = visibleBounds
    ? visibleBounds.maxRow - visibleBounds.minRow + 1
    : gridHeight;
  const drawMinColumn = visibleBounds ? visibleBounds.minColumn : 0;
  const drawMaxColumn = visibleBounds ? visibleBounds.maxColumn : gridWidth - 1;
  const drawMinRow = visibleBounds ? visibleBounds.minRow : 0;
  const drawMaxRow = visibleBounds ? visibleBounds.maxRow : gridHeight - 1;
  const cell = fitToViewport
    ? getPreviewCellSize({
      containerWidth: width,
      containerHeight: availableHeight,
      gridWidth: fitGridWidth,
      gridHeight: fitGridHeight,
      padding,
    })
    : Math.max(1, Math.floor(Math.max(1, width - padding * 2) / gridWidth));
  const sheetWidth = fitGridWidth * cell;
  const sheetHeight = fitGridHeight * cell;
  const height = fitToViewport ? availableHeight : sheetHeight + padding * 2;
  const context = setCanvasSize(canvas, width, height);
  const verticalSpace = Math.max(0, height - sheetHeight);
  let offsetX = Math.floor((width - sheetWidth) / 2);
  let offsetY = fitToViewport
    ? Math.floor(verticalSpace * (hero ? 0.24 : 0.5))
    : padding;

  context.clearRect(0, 0, width, height);
  if (!transparentBackground) {
    context.fillStyle = "rgba(255, 255, 255, 0.98)";
    context.fillRect(0, 0, width, height);
  }

  matrix.forEach((row, rowIndex) => {
    if (rowIndex < drawMinRow || rowIndex > drawMaxRow) return;

    row.forEach((color, columnIndex) => {
      if (columnIndex < drawMinColumn || columnIndex > drawMaxColumn) return;

      const x = offsetX + (columnIndex - drawMinColumn) * cell;
      const y = offsetY + (rowIndex - drawMinRow) * cell;
      const radius = cell / 2;

      if (!transparentBackground) {
        context.fillStyle = "#ffffff";
        context.fillRect(x, y, cell, cell);
      }

      if (color && !hiddenCodes.has(color.code) && !hiddenCells.has(rowIndex * gridWidth + columnIndex)) {
        const isHighlighted = highlightCodes.has(color.code);
        context.globalAlpha = hasHighlights && !isHighlighted ? 0.16 : 1;
        context.fillStyle = color.hex;
        if (roundBeads) {
          context.beginPath();
          context.arc(x + radius, y + radius, Math.max(2, radius - 0.8), 0, Math.PI * 2);
          context.fill();
        } else {
          context.fillRect(x + 1, y + 1, Math.max(2, cell - 2), Math.max(2, cell - 2));
        }

        if (!hero && showCodes && cell >= 14) {
          context.fillStyle = getBrightness(color) > 0.62 ? "#17181a" : "#ffffff";
          context.font = `700 ${Math.max(6, Math.floor(cell * 0.28))}px "IBM Plex Mono", monospace`;
          context.textAlign = "center";
          context.textBaseline = "middle";
          context.fillText(color.code, x + radius, y + radius + 0.5);
        }

        if (hasHighlights && isHighlighted) {
          context.globalAlpha = 1;
          context.strokeStyle = "rgba(8, 173, 134, 0.92)";
          context.lineWidth = Math.max(1.4, cell * 0.12);
          if (roundBeads) {
            context.beginPath();
            context.arc(x + radius, y + radius, Math.max(2, radius - 0.2), 0, Math.PI * 2);
            context.stroke();
          } else {
            context.strokeRect(x + 1.5, y + 1.5, Math.max(2, cell - 3), Math.max(2, cell - 3));
          }
        }
      }

      context.globalAlpha = 1;
      context.strokeStyle = color ? "rgba(22, 22, 22, 0.08)" : "rgba(22, 22, 22, 0.045)";
      context.lineWidth = 1;
      context.strokeRect(x, y, cell, cell);
    });
  });

  context.strokeStyle = "rgba(255, 90, 54, 0.5)";
  context.lineWidth = hero ? 1 : 2;
  for (let index = Math.ceil((drawMinColumn + 1) / 10) * 10; index <= drawMaxColumn; index += 10) {
    const x = offsetX + (index - drawMinColumn) * cell;
    context.beginPath();
    context.moveTo(x, offsetY);
    context.lineTo(x, offsetY + sheetHeight);
    context.stroke();
  }
  for (let index = Math.ceil((drawMinRow + 1) / 10) * 10; index <= drawMaxRow; index += 10) {
    const y = offsetY + (index - drawMinRow) * cell;
    context.beginPath();
    context.moveTo(offsetX, y);
    context.lineTo(offsetX + sheetWidth, y);
    context.stroke();
  }
}

export function createExportCanvas({
  title,
  brand,
  width,
  height,
  counts,
  matrix,
  showCodes,
  roundBeads,
  hiddenColorCodes = [],
  hiddenCellKeys = [],
}) {
  const padding = 28;
  const cell = 22;
  const gap = 2;
  const statsWidth = 336;
  const gridWidth = width * (cell + gap) - gap;
  const gridHeight = height * (cell + gap) - gap;
  const canvasWidth = padding * 3 + gridWidth + statsWidth;
  const canvasHeight = Math.max(padding * 2 + gridHeight, 760);
  const canvas = document.createElement("canvas");
  canvas.width = canvasWidth;
  canvas.height = canvasHeight;
  const context = canvas.getContext("2d");
  const hiddenCodes = new Set(hiddenColorCodes);
  const hiddenCells = new Set(hiddenCellKeys);
  const totalBeads = counts.reduce((sum, entry) => sum + entry.count, 0);

  context.fillStyle = "#fff9ef";
  context.fillRect(0, 0, canvasWidth, canvasHeight);

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      const x = padding + columnIndex * (cell + gap);
      const y = padding + rowIndex * (cell + gap);
      context.fillStyle = "#ffffff";
      context.fillRect(x, y, cell, cell);
      if (!color) return;
      if (hiddenCodes.has(color.code)) return;
      if (hiddenCells.has(rowIndex * width + columnIndex)) return;

      if (roundBeads) {
        context.beginPath();
        context.fillStyle = color.hex;
        context.arc(x + cell / 2, y + cell / 2, cell / 2, 0, Math.PI * 2);
        context.fill();
      } else {
        context.fillStyle = color.hex;
        context.fillRect(x, y, cell, cell);
      }

      if (showCodes) {
        context.fillStyle = getBrightness(color) > 0.62 ? "#17181a" : "#ffffff";
        context.font = '700 8px "IBM Plex Mono", monospace';
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.fillText(color.code, x + cell / 2, y + cell / 2 + 0.5);
      }
    });
  });

  context.strokeStyle = "#ff5a36";
  context.lineWidth = 2;
  for (let index = 10; index < width; index += 10) {
    const x = padding + index * (cell + gap) - gap / 2;
    context.beginPath();
    context.moveTo(x, padding);
    context.lineTo(x, padding + gridHeight);
    context.stroke();
  }
  for (let index = 10; index < height; index += 10) {
    const y = padding + index * (cell + gap) - gap / 2;
    context.beginPath();
    context.moveTo(padding, y);
    context.lineTo(padding + gridWidth, y);
    context.stroke();
  }

  const statsX = padding * 2 + gridWidth;
  context.fillStyle = "#fffefb";
  context.fillRect(statsX, padding, statsWidth, canvasHeight - padding * 2);
  context.strokeStyle = "#ece2d4";
  context.strokeRect(statsX, padding, statsWidth, canvasHeight - padding * 2);

  context.fillStyle = "#171717";
  context.textAlign = "left";
  context.textBaseline = "alphabetic";
  context.font = '800 28px "Bricolage Grotesque", "Noto Sans SC", sans-serif';
  context.fillText(title, statsX + 20, padding + 40, statsWidth - 40);
  context.font = '600 13px "IBM Plex Mono", monospace';
  context.fillStyle = "#65594d";
  const hiddenParts = [];
  if (hiddenColorCodes.length) hiddenParts.push(`已隐藏 ${hiddenColorCodes.length} 色`);
  if (hiddenCellKeys.length) hiddenParts.push("已隐藏底色");
  const hiddenScope = hiddenParts.length ? ` · ${hiddenParts.join(" · ")}` : "";
  context.fillText(`${brand} · ${width}x${height} · ${counts.length} 色${hiddenScope}`, statsX + 20, padding + 68, statsWidth - 40);

  context.fillStyle = "#171717";
  context.font = '800 18px "Bricolage Grotesque", "Noto Sans SC", sans-serif';
  context.fillText(`豆子总数：${totalBeads.toLocaleString("zh-CN")}`, statsX + 20, padding + 112, statsWidth - 40);

  let y = padding + 146;
  counts.slice(0, 10).forEach((entry) => {
    const itemX = statsX + 20;
    const swatchSize = 22;
    const codeX = itemX + 34;
    const nameX = codeX + 64;
    const countX = statsX + statsWidth - 24;

    context.fillStyle = entry.hex;
    context.fillRect(itemX, y, swatchSize, swatchSize);
    context.strokeStyle = "#e5dacb";
    context.strokeRect(itemX, y, swatchSize, swatchSize);

    context.fillStyle = "#171717";
    context.font = '800 14px "IBM Plex Mono", monospace';
    context.textAlign = "left";
    context.textBaseline = "alphabetic";
    context.fillText(entry.code, codeX, y + 17);

    context.fillStyle = "#65594d";
    context.font = '600 12px "Noto Sans SC", sans-serif';
    context.fillText(entry.name, nameX, y + 17, Math.max(42, countX - nameX - 14));

    context.fillStyle = "#171717";
    context.font = '800 14px "IBM Plex Mono", monospace';
    context.textAlign = "right";
    context.fillText(String(entry.count), countX, y + 19);

    y += 34;
  });

  const watermarkText = "拼豆.cn";
  const watermarkPadding = 22;
  const watermarkHeight = 42;
  context.save();
  context.font = '900 24px "Bricolage Grotesque", "Noto Sans SC", sans-serif';
  const watermarkTextWidth = context.measureText(watermarkText).width;
  const watermarkWidth = Math.ceil(watermarkTextWidth + 78);
  const watermarkX = canvasWidth - padding - watermarkWidth;
  const watermarkY = canvasHeight - padding - watermarkHeight;
  context.fillStyle = "rgba(16, 36, 31, 0.06)";
  context.beginPath();
  context.roundRect(watermarkX, watermarkY, watermarkWidth, watermarkHeight, 21);
  context.fill();

  const dotY = watermarkY + watermarkHeight / 2;
  const dotX = watermarkX + watermarkPadding;
  [
    { x: 0, y: -6, color: "#ff5a36" },
    { x: 12, y: -6, color: "#f0c348" },
    { x: 0, y: 6, color: "#00a77e" },
    { x: 12, y: 6, color: "#7bc2ef" },
  ].forEach((dot) => {
    context.beginPath();
    context.fillStyle = dot.color;
    context.arc(dotX + dot.x, dotY + dot.y, 4.1, 0, Math.PI * 2);
    context.fill();
  });

  context.fillStyle = "rgba(16, 36, 31, 0.7)";
  context.textAlign = "right";
  context.textBaseline = "middle";
  context.fillText(watermarkText, watermarkX + watermarkWidth - 20, dotY + 0.5);
  context.restore();

  return canvas;
}

export function createSavedPreview({ matrix, width, height, roundBeads }) {
  const canvas = document.createElement("canvas");
  canvas.width = 540;
  canvas.height = 540;
  const context = canvas.getContext("2d");
  const padding = 42;
  const cell = Math.max(
    8,
    Math.min(
      Math.floor((canvas.width - padding * 2) / width),
      Math.floor((canvas.height - padding * 2) / height)
    )
  );
  const artworkWidth = cell * width;
  const artworkHeight = cell * height;
  const offsetX = Math.floor((canvas.width - artworkWidth) / 2);
  const offsetY = Math.floor((canvas.height - artworkHeight) / 2);

  const wash = context.createLinearGradient(0, 0, canvas.width, canvas.height);
  wash.addColorStop(0, "#fffaf2");
  wash.addColorStop(1, "#ffe7d2");
  context.fillStyle = wash;
  context.fillRect(0, 0, canvas.width, canvas.height);

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      if (!color) return;

      const x = offsetX + columnIndex * cell;
      const y = offsetY + rowIndex * cell;
      const radius = cell / 2;

      context.fillStyle = color.hex;
      if (roundBeads) {
        context.beginPath();
        context.arc(x + radius, y + radius, Math.max(2, radius - 0.8), 0, Math.PI * 2);
        context.fill();
      } else {
        context.fillRect(x + 1, y + 1, Math.max(2, cell - 2), Math.max(2, cell - 2));
      }
    });
  });

  return canvas.toDataURL("image/png");
}
