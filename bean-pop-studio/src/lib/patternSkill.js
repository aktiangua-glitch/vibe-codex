import { getBrandOutputColorLimit } from "../data/palettes.js";
import {
  createExportCanvas,
  createSavedPreview,
  processImage,
  sortCountsOuterFirst,
} from "./beadEngine.js";
import {
  analyzeImageSubject,
  buildPreparedSource,
} from "./imagePrep.js";
import {
  DEFAULT_BOARD_GUIDE_SIZES,
  getRecommendedMaxColors,
  getRecommendedTargetWidth,
} from "./recommendation.js";

const DEFAULT_OPTIONS = {
  backgroundMode: "auto",
  brand: "MARD",
  cropRatio: "auto",
  emptyAlphaThreshold: 12,
  filename: "pindou-pattern",
  framing: "balanced",
  maxTargetSize: 145,
  offsetX: 0,
  offsetY: 0,
  projectName: "",
  roundBeads: true,
  showCodes: true,
  styleIntensity: 0.72,
  styleMode: "clean_ink",
  zoomAdjust: 1,
};

function isBlobLike(value) {
  return typeof Blob !== "undefined" && value instanceof Blob;
}

function isCanvasLike(value) {
  return value && typeof value.getContext === "function" && value.width && value.height;
}

function isImageLike(value) {
  return value && typeof HTMLImageElement !== "undefined" && value instanceof HTMLImageElement;
}

function loadImageElement(src, revokeUrl = null) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.crossOrigin = "anonymous";
    image.onload = () => {
      if (revokeUrl) URL.revokeObjectURL(revokeUrl);
      resolve(image);
    };
    image.onerror = () => {
      if (revokeUrl) URL.revokeObjectURL(revokeUrl);
      reject(new Error("Unable to load image input"));
    };
    image.src = src;
  });
}

export async function loadPatternImage(input) {
  if (!input) {
    throw new Error("Missing image input");
  }

  if (isCanvasLike(input)) return input;

  if (isImageLike(input)) {
    if (input.complete && input.naturalWidth) return input;
    await input.decode();
    return input;
  }

  if (isBlobLike(input)) {
    const objectUrl = URL.createObjectURL(input);
    return loadImageElement(objectUrl, objectUrl);
  }

  if (typeof input === "string") {
    return loadImageElement(input);
  }

  throw new Error("Unsupported image input. Use File, Blob, data URL, image URL, HTMLImageElement or canvas.");
}

function normalizeOptions(options = {}) {
  return {
    ...DEFAULT_OPTIONS,
    ...options,
  };
}

function detectBackgroundColorCode(matrix, width, height) {
  if (!matrix.length || !width || !height) return "";

  const edgeCounts = new Map();
  let samples = 0;
  const addCell = (rowIndex, columnIndex) => {
    const color = matrix[rowIndex]?.[columnIndex];
    samples += 1;
    if (!color?.code) return;
    edgeCounts.set(color.code, (edgeCounts.get(color.code) || 0) + 1);
  };

  for (let columnIndex = 0; columnIndex < width; columnIndex += 1) {
    addCell(0, columnIndex);
    addCell(height - 1, columnIndex);
  }
  for (let rowIndex = 1; rowIndex < height - 1; rowIndex += 1) {
    addCell(rowIndex, 0);
    addCell(rowIndex, width - 1);
  }

  let dominantCode = "";
  let dominantCount = 0;
  edgeCounts.forEach((count, code) => {
    if (count > dominantCount) {
      dominantCode = code;
      dominantCount = count;
    }
  });

  return samples && dominantCount / samples >= 0.35 ? dominantCode : "";
}

function detectBackgroundCellKeys(matrix, width, height, backgroundCode) {
  if (!matrix.length || !width || !height || !backgroundCode) return [];

  const hidden = new Set();
  const queue = [];
  const enqueue = (rowIndex, columnIndex) => {
    if (rowIndex < 0 || rowIndex >= height || columnIndex < 0 || columnIndex >= width) return;
    const key = rowIndex * width + columnIndex;
    if (hidden.has(key)) return;
    if (matrix[rowIndex]?.[columnIndex]?.code !== backgroundCode) return;
    hidden.add(key);
    queue.push([rowIndex, columnIndex]);
  };

  for (let columnIndex = 0; columnIndex < width; columnIndex += 1) {
    enqueue(0, columnIndex);
    enqueue(height - 1, columnIndex);
  }
  for (let rowIndex = 1; rowIndex < height - 1; rowIndex += 1) {
    enqueue(rowIndex, 0);
    enqueue(rowIndex, width - 1);
  }

  for (let index = 0; index < queue.length; index += 1) {
    const [rowIndex, columnIndex] = queue[index];
    enqueue(rowIndex - 1, columnIndex);
    enqueue(rowIndex + 1, columnIndex);
    enqueue(rowIndex, columnIndex - 1);
    enqueue(rowIndex, columnIndex + 1);
  }

  return [...hidden];
}

function buildVisibleCountsFromMatrix(matrix, hiddenColorCodes, hiddenCellKeys, width) {
  const hiddenCodes = new Set(hiddenColorCodes);
  const hiddenCells = new Set(hiddenCellKeys);
  const counts = new Map();
  const height = matrix.length;

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      if (!color) return;
      if (hiddenCodes.has(color.code)) return;
      if (hiddenCells.has(rowIndex * width + columnIndex)) return;

      const edgeDistance = Math.min(
        rowIndex,
        columnIndex,
        height - 1 - rowIndex,
        width - 1 - columnIndex
      );
      const maxDistance = Math.max(1, Math.min(width, height) / 2);
      const outerScore = 1 - Math.min(edgeDistance / maxDistance, 1);
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

function getImageLabel(input, fallback) {
  if (isBlobLike(input) && input.name) return input.name;
  if (typeof input === "string") {
    const clean = input.split("?")[0].split("#")[0];
    const lastPart = clean.split("/").filter(Boolean).at(-1);
    if (lastPart) return decodeURIComponent(lastPart);
  }
  return fallback;
}

function shouldHideBackground(backgroundMode, subjectBox, backgroundCode) {
  if (backgroundMode === false || backgroundMode === "none") return false;
  if (!backgroundCode) return false;
  if (backgroundMode === true || backgroundMode === "always") return true;
  return Boolean(subjectBox?.hasTransparentBackground);
}

function createExportMeta({
  backgroundColorCode,
  brand,
  counts,
  height,
  hiddenCellKeys,
  maxColors,
  targetWidth,
  title,
  width,
}) {
  const totalBeads = counts.reduce((sum, entry) => sum + entry.count, 0);

  return {
    backgroundColorCode,
    brand,
    colorCount: counts.length,
    height,
    hiddenBackgroundCells: hiddenCellKeys.length,
    maxColors,
    targetWidth,
    title,
    totalBeads,
    width,
  };
}

function canvasToBlob(canvas, type = "image/png", quality) {
  return new Promise((resolve) => {
    canvas.toBlob((blob) => resolve(blob), type, quality);
  });
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  window.setTimeout(() => URL.revokeObjectURL(url), 200);
}

function serializeCounts(counts) {
  return counts.map((entry) => ({
    code: entry.code,
    count: entry.count,
    hex: entry.hex,
    name: entry.name,
  }));
}

function serializeMatrix(matrix) {
  return matrix.map((row) => row.map((color) => (color ? color.code : null)));
}

export async function generatePindouPattern(input, options = {}) {
  const config = normalizeOptions(options);
  const image = await loadPatternImage(input);
  const subjectBox = config.subjectBox || await analyzeImageSubject(image);
  const brandColorLimit = getBrandOutputColorLimit(config.brand);
  const targetWidth = Math.round(config.targetWidth || getRecommendedTargetWidth({
    boardSizes: config.boardSizes || DEFAULT_BOARD_GUIDE_SIZES,
    imageHeight: image.height,
    imageWidth: image.width,
    maxTargetSize: config.maxTargetSize,
    subjectBox,
  }));
  const maxColors = Math.round(config.maxColors || getRecommendedMaxColors({
    brandColorLimit,
    imageHeight: image.height,
    imageWidth: image.width,
    subjectBox,
    targetWidth,
  }));

  const prepared = buildPreparedSource(image, {
    cropRatio: config.cropRatio,
    framing: config.framing,
    offsetX: config.offsetX,
    offsetY: config.offsetY,
    styleIntensity: config.styleIntensity,
    styleMode: config.styleMode,
    subjectBox,
    zoomAdjust: config.zoomAdjust,
  });

  const result = processImage({
    source: prepared.canvas,
    brand: config.brand,
    emptyAlphaThreshold: config.emptyAlphaThreshold,
    maxColors,
    targetWidth,
    transparentBackground: Boolean(subjectBox?.hasTransparentBackground),
  });

  const backgroundColorCode = detectBackgroundColorCode(result.matrix, result.width, result.height);
  const hiddenCellKeys = shouldHideBackground(config.backgroundMode, subjectBox, backgroundColorCode)
    ? detectBackgroundCellKeys(result.matrix, result.width, result.height, backgroundColorCode)
    : [];
  const visibleCounts = buildVisibleCountsFromMatrix(result.matrix, [], hiddenCellKeys, result.width);
  const label = getImageLabel(input, config.filename);
  const title = config.projectName || label.replace(/\.[a-z0-9]+$/i, "") || "拼豆图纸";
  const exportCanvas = createExportCanvas({
    title,
    brand: config.brand,
    width: result.width,
    height: result.height,
    counts: visibleCounts,
    matrix: result.matrix,
    showCodes: config.showCodes,
    roundBeads: config.roundBeads,
    hiddenCellKeys,
  });
  const previewCanvas = createSavedPreview({
    matrix: result.matrix,
    width: result.width,
    height: result.height,
    roundBeads: config.roundBeads,
  });
  const meta = createExportMeta({
    backgroundColorCode,
    brand: config.brand,
    counts: visibleCounts,
    height: result.height,
    hiddenCellKeys,
    maxColors,
    targetWidth,
    title,
    width: result.width,
  });

  return {
    ...meta,
    counts: serializeCounts(visibleCounts),
    exportCanvas,
    hiddenCellKeys,
    matrix: serializeMatrix(result.matrix),
    patternDataUrl: exportCanvas.toDataURL("image/png"),
    patternBlob: () => canvasToBlob(exportCanvas, "image/png"),
    previewCanvas,
    previewDataUrl: previewCanvas.toDataURL("image/png"),
    rawResult: result,
    subjectBox,
  };
}

export async function downloadPindouPattern(input, options = {}) {
  const pattern = await generatePindouPattern(input, options);
  const blob = await pattern.patternBlob();
  if (!blob) {
    throw new Error("Unable to create pattern PNG blob");
  }
  const safeTitle = pattern.title.replace(/\s+/g, "-") || "pindou-pattern";
  const filename = options.downloadName || `${safeTitle}-${pattern.brand.toLowerCase()}-${pattern.width}x${pattern.height}.png`;

  downloadBlob(blob, filename);
  return pattern;
}

export const PINDOU_PATTERN_SKILL_DEFAULTS = DEFAULT_OPTIONS;
