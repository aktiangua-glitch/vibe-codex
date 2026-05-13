<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import CommunityShowcase from "./components/CommunityShowcase.vue";
import GenerationDialog from "./components/GenerationDialog.vue";
import { communityCategories } from "./data/communitySeeds.js";
import { brandMeta, getBrandOutputColorLimit } from "./data/palettes.js";
import {
  createExportCanvas,
  createSampleDataUrl,
  createSavedPreview,
  getScaledGridSize,
  processImage,
  renderBeadCanvas,
  renderPatternCanvas,
  renderSourceCanvas,
  sortCountsOuterFirst,
} from "./lib/beadEngine.js";
import {
  analyzeImageSubject,
  buildPreparedSource,
  cropRatioPresets,
  framingPresets,
  stylePresets,
} from "./lib/imagePrep.js";
import { createCommunitySourceDataUrl, getCommunitySourceSubjectBox } from "./lib/communitySources.js";
import { getRecommendedTargetWidth } from "./lib/recommendation.js";
import {
  loadSavedProjects,
  persistSavedProjects,
} from "./lib/storage.js";

const config = reactive({
  brand: "MARD",
  hideBackgroundBeads: false,
  hiddenColorCodes: [],
  maxColors: 12,
  projectName: "",
  roundBeads: true,
  showCodes: true,
  stageView: "result",
  targetWidth: 32,
});

const MAX_TARGET_SIZE = 145;
const BOARD_GUIDE_SIZES = [29, 58, 87, 116, 145];

const prep = reactive({
  cropRatio: "auto",
  framing: "balanced",
  offsetX: 0,
  offsetY: 0,
  styleIntensity: 0.72,
  styleMode: "clean_ink",
  zoomAdjust: 1,
});

const rawImage = ref(null);
const imageLabel = ref("示例照片");
const imageDataUrl = ref("");
const preparedSource = ref(null);
const subjectBox = ref({
  x: 0.2,
  y: 0.14,
  width: 0.6,
  height: 0.72,
  confidence: 0,
});

const generationDialogRef = ref(null);

const heroSourceCanvas = ref(null);
const heroResultCanvas = ref(null);
const heroPatternCanvas = ref(null);
const heroDemoSource = ref(null);
const headerVisible = ref(false);
const activeDemoStep = ref("source");
const demoPaused = ref(false);
const flowOpen = ref(false);
const flowStep = ref("upload");
const mergePreviewColorCodes = ref([]);
const editHistory = ref([]);
const editSessionKey = ref(0);
const canvasEditMode = ref("");

const result = reactive({
  counts: [],
  height: 0,
  matrix: [],
  width: 0,
});
const heroDemoResult = reactive({
  counts: [],
  height: 0,
  matrix: [],
  width: 0,
});

const currentProjectId = ref(null);
const savedProjects = ref([]);
const actionState = ref("");
const processing = ref(false);
const toasts = ref([]);

let processTimer = 0;
let renderFrame = 0;
let demoCycleTimer = 0;
let canvasResizeObserver = null;
let toastCounter = 0;

const demoSteps = ["source", "result", "sheet"];
const flowSteps = ["upload", "tune", "result"];
const heroDemoLabel = "红帽小勇者";
const currentPath = typeof window !== "undefined" ? window.location.pathname : "/";
const isCommunityPage = /^\/community(?:\/|$)/.test(currentPath);
const homePageHref = isCommunityPage ? "../" : "#top";
const communityPageHref = isCommunityPage ? "#community" : "./community/";

if (typeof document !== "undefined") {
  document.documentElement.classList.toggle("is-community-route", isCommunityPage);
  document.body.classList.toggle("is-community-route", isCommunityPage);
}

function sumBeadCounts(counts) {
  return counts.reduce((sum, item) => sum + item.count, 0);
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

function replaceMatrixColor(matrix, sourceCode, targetColor) {
  return matrix.map((row) => row.map((color) => {
    if (!color || color.code !== sourceCode) return color;
    return targetColor;
  }));
}

function cloneMatrixColor(color) {
  if (!color) return color;
  return {
    ...color,
    rgb: color.rgb ? { ...color.rgb } : color.rgb,
    lab: color.lab ? { ...color.lab } : color.lab,
  };
}

function cloneMatrix(matrix) {
  return matrix.map((row) => row.map((color) => cloneMatrixColor(color)));
}

function cloneCounts(counts) {
  return counts.map((item) => cloneMatrixColor(item));
}

function pushEditSnapshot() {
  editHistory.value = [
    ...editHistory.value,
    {
      counts: cloneCounts(result.counts),
      hiddenColorCodes: [...config.hiddenColorCodes],
      matrix: cloneMatrix(result.matrix),
    },
  ].slice(-30);
}

function clearEditState() {
  editHistory.value = [];
  mergePreviewColorCodes.value = [];
  editSessionKey.value += 1;
}

const backgroundColorCode = computed(() => (
  subjectBox.value?.hasTransparentBackground
    ? ""
    : detectBackgroundColorCode(result.matrix, result.width, result.height)
));
const previewFocusColorCode = computed(() => {
  const edgeCode = detectBackgroundColorCode(result.matrix, result.width, result.height);
  if (edgeCode) return edgeCode;

  const total = sumBeadCounts(result.counts);
  const dominant = result.counts.slice().sort((left, right) => right.count - left.count)[0];
  return dominant && dominant.count / Math.max(total, 1) >= 0.34 ? dominant.code : "";
});
const activeHiddenColorCodes = computed(() => {
  return config.hiddenColorCodes.filter((code) => result.counts.some((entry) => entry.code === code));
});
const shouldFocusVisibleArtwork = computed(() => (
  canvasEditMode.value !== "paint" && config.hideBackgroundBeads
));
const backgroundHiddenCellKeys = computed(() => {
  if (!config.hideBackgroundBeads || !backgroundColorCode.value) return [];
  return detectBackgroundCellKeys(result.matrix, result.width, result.height, backgroundColorCode.value);
});
const visibleCounts = computed(() => {
  if (!activeHiddenColorCodes.value.length && !backgroundHiddenCellKeys.value.length) return result.counts;
  return buildVisibleCountsFromMatrix(
    result.matrix,
    activeHiddenColorCodes.value,
    backgroundHiddenCellKeys.value,
    result.width
  );
});
const totalBeads = computed(() => sumBeadCounts(visibleCounts.value));
const heroDemoTotalBeads = computed(() => sumBeadCounts(heroDemoResult.counts));
const selectedBrandColorLimit = computed(() => getBrandOutputColorLimit(config.brand) || 36);
const tuneSizePresets = computed(() => BOARD_GUIDE_SIZES.filter((size) => size <= MAX_TARGET_SIZE));
const recommendedTargetWidth = computed(() => {
  return getRecommendedTargetWidth({
    boardSizes: BOARD_GUIDE_SIZES,
    imageHeight: rawImage.value?.height || 0,
    imageWidth: rawImage.value?.width || 0,
    maxTargetSize: MAX_TARGET_SIZE,
    subjectBox: subjectBox.value,
  });
});
const recommendedGridSize = computed(() => {
  if (!preparedSource.value || !recommendedTargetWidth.value) {
    return {
      width: recommendedTargetWidth.value,
      height: recommendedTargetWidth.value,
    };
  }
  return getScaledGridSize(preparedSource.value, recommendedTargetWidth.value);
});
const recommendedAspectLabel = computed(() => (
  `${recommendedGridSize.value.width} × ${recommendedGridSize.value.height}`
));
const recommendedMaxColors = computed(() => {
  const limit = selectedBrandColorLimit.value;
  const source = rawImage.value;
  if (!source?.width || !source?.height) return Math.min(18, limit);

  const longSide = Math.max(source.width, source.height);
  const shortSide = Math.max(1, Math.min(source.width, source.height));
  const aspect = longSide / shortSide;
  let count = 16;

  if (recommendedTargetWidth.value >= 116) count = 28;
  else if (recommendedTargetWidth.value >= 87) count = 24;
  else if (recommendedTargetWidth.value >= 58) count = 18;
  else count = 12;

  if (longSide >= 2800) count += 4;
  if (aspect >= 1.5) count += 2;
  if (subjectBox.value?.confidence < 0.42) count += 2;

  return Math.max(8, Math.min(limit, Math.round(count / 2) * 2));
});
const projectTitle = computed(() => {
  const customTitle = config.projectName.trim();
  if (customTitle) return customTitle;
  return imageLabel.value.replace(/\.[a-z0-9]+$/i, "");
});
const aspectLabel = computed(() => {
  if (!preparedSource.value || !config.targetWidth) {
    return `${config.targetWidth} × ${config.targetWidth}`;
  }
  const size = getScaledGridSize(preparedSource.value, config.targetWidth);
  return `${size.width} × ${size.height}`;
});
const hasResult = computed(() => result.matrix.length > 0);
const preparedLabel = computed(() => {
  return `${cropRatioPresets[prep.cropRatio].label} · ${stylePresets[prep.styleMode].tag}`;
});
const heroGuide = computed(() => {
  return {
    badge: "打开就能试",
    text: "上传一张照片，直接看到成品、色号和图纸。",
  };
});
const heroCommunityTags = computed(() => communityCategories.filter((item) => item !== "全部"));

function resetHiddenColorState() {
  config.hiddenColorCodes = [];
  config.hideBackgroundBeads = false;
}

function pushToast(message) {
  const id = `${Date.now()}-${toastCounter += 1}`;
  toasts.value.push({ id, message });
  window.setTimeout(() => {
    toasts.value = toasts.value.filter((item) => item.id !== id);
  }, 2600);
}

function scheduleRender() {
  cancelAnimationFrame(renderFrame);
  renderFrame = requestAnimationFrame(() => {
    renderAllCanvases();
  });
}

function renderAllCanvases() {
  const dialogCanvases = generationDialogRef.value?.getCanvases?.() || {};

  renderSourceCanvas(heroSourceCanvas.value, {
    source: heroDemoSource.value,
    clean: true,
  });
  renderBeadCanvas(heroResultCanvas.value, {
    matrix: heroDemoResult.matrix,
    width: heroDemoResult.width,
    height: heroDemoResult.height,
    roundBeads: true,
    hero: true,
    transparentBackground: true,
  });
  renderPatternCanvas(heroPatternCanvas.value, {
    matrix: heroDemoResult.matrix,
    width: heroDemoResult.width,
    height: heroDemoResult.height,
    roundBeads: true,
    showCodes: true,
    hero: true,
    fitToViewport: true,
    transparentBackground: true,
  });

  renderSourceCanvas(dialogCanvases.source, { source: rawImage.value });
  renderSourceCanvas(dialogCanvases.prepared, { source: preparedSource.value || rawImage.value });
  renderBeadCanvas(dialogCanvases.result, {
    matrix: result.matrix,
    width: result.width,
    height: result.height,
    roundBeads: config.roundBeads,
    hero: false,
    fitToViewport: true,
    showGrid: canvasEditMode.value === "paint",
    hiddenColorCodes: activeHiddenColorCodes.value,
    hiddenCellKeys: backgroundHiddenCellKeys.value,
    highlightColorCodes: mergePreviewColorCodes.value,
    centerVisible: shouldFocusVisibleArtwork.value,
    focusColorCode: shouldFocusVisibleArtwork.value ? previewFocusColorCode.value : "",
    viewportPadding: canvasEditMode.value === "paint" ? 8 : 16,
    backgroundDeltaE: 18,
    visiblePaddingCells: canvasEditMode.value === "paint" ? 0 : 0,
  });
  renderPatternCanvas(dialogCanvases.pattern, {
    matrix: result.matrix,
    width: result.width,
    height: result.height,
    roundBeads: config.roundBeads,
    showCodes: true,
    centerVisible: shouldFocusVisibleArtwork.value,
    fitToViewport: true,
    hero: false,
    hiddenColorCodes: activeHiddenColorCodes.value,
    hiddenCellKeys: backgroundHiddenCellKeys.value,
    highlightColorCodes: mergePreviewColorCodes.value,
    focusColorCode: shouldFocusVisibleArtwork.value ? previewFocusColorCode.value : "",
    viewportPadding: canvasEditMode.value === "paint" ? 8 : 16,
    visiblePaddingCells: canvasEditMode.value === "paint" ? 0 : 2,
  });
}

async function processCurrentImage() {
  if (!rawImage.value) return;

  processing.value = true;
  await nextTick();

  const prepared = buildPreparedSource(rawImage.value, {
    cropRatio: prep.cropRatio,
    framing: prep.framing,
    offsetX: prep.offsetX,
    offsetY: prep.offsetY,
    styleIntensity: prep.styleIntensity,
    styleMode: prep.styleMode,
    subjectBox: subjectBox.value,
    zoomAdjust: prep.zoomAdjust,
  });

  preparedSource.value = prepared.canvas;

  const nextResult = processImage({
    source: prepared.canvas,
    brand: config.brand,
    maxColors: config.maxColors,
    targetWidth: config.targetWidth,
    transparentBackground: Boolean(subjectBox.value?.hasTransparentBackground),
  });

  result.width = nextResult.width;
  result.height = nextResult.height;
  result.matrix = nextResult.matrix;
  result.counts = nextResult.counts;
  clearEditState();
  config.hiddenColorCodes = config.hiddenColorCodes.filter((code) => nextResult.counts.some((entry) => entry.code === code));
  if (config.hideBackgroundBeads && !detectBackgroundColorCode(nextResult.matrix, nextResult.width, nextResult.height)) {
    config.hideBackgroundBeads = false;
  }

  processing.value = false;
  await nextTick();
  scheduleRender();
}

function queueProcess(delay = 120) {
  window.clearTimeout(processTimer);
  processTimer = window.setTimeout(() => {
    processCurrentImage();
  }, delay);
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

function createImageFromDataUrl(dataUrl) {
  return new Promise((resolve, reject) => {
    const nextImage = new Image();
    nextImage.onload = () => resolve(nextImage);
    nextImage.onerror = reject;
    nextImage.src = dataUrl;
  });
}

function createMarketingDemoDataUrl() {
  const canvas = document.createElement("canvas");
  canvas.width = 560;
  canvas.height = 700;
  const ctx = canvas.getContext("2d");

  const fillEllipse = (x, y, radiusX, radiusY, color, rotation = 0) => {
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.ellipse(x, y, radiusX, radiusY, rotation, 0, Math.PI * 2);
    ctx.fill();
  };

  const strokePath = (color, width, draw) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    draw();
    ctx.stroke();
  };

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  ctx.fillStyle = "#ff5a36";
  ctx.beginPath();
  ctx.roundRect(172, 122, 216, 86, 42);
  ctx.fill();
  ctx.beginPath();
  ctx.roundRect(122, 180, 316, 64, 32);
  ctx.fill();

  fillEllipse(280, 314, 136, 126, "#f4b47d");
  fillEllipse(280, 374, 104, 72, "#ffe0bf");
  fillEllipse(188, 326, 35, 28, "#f4b47d");
  fillEllipse(372, 326, 35, 28, "#f4b47d");

  ctx.fillStyle = "#1b2230";
  ctx.beginPath();
  ctx.roundRect(210, 314, 34, 54, 17);
  ctx.roundRect(316, 314, 34, 54, 17);
  ctx.fill();
  fillEllipse(220, 326, 7, 10, "#ffffff");
  fillEllipse(326, 326, 7, 10, "#ffffff");
  fillEllipse(280, 370, 19, 14, "#784b32");

  strokePath("#1b2230", 8, () => {
    ctx.moveTo(260, 392);
    ctx.quadraticCurveTo(280, 408, 300, 392);
  });

  fillEllipse(214, 376, 30, 16, "#ff8f77", -0.08);
  fillEllipse(346, 376, 30, 16, "#ff8f77", 0.08);

  strokePath("#b24d31", 13, () => {
    ctx.moveTo(206, 258);
    ctx.quadraticCurveTo(250, 280, 288, 266);
    ctx.quadraticCurveTo(330, 278, 372, 252);
  });

  ctx.fillStyle = "#456ed8";
  ctx.beginPath();
  ctx.moveTo(174, 512);
  ctx.quadraticCurveTo(280, 448, 386, 512);
  ctx.lineTo(410, 626);
  ctx.quadraticCurveTo(280, 672, 150, 626);
  ctx.closePath();
  ctx.fill();

  ctx.fillStyle = "#00a77e";
  ctx.beginPath();
  ctx.moveTo(170, 488);
  ctx.quadraticCurveTo(280, 540, 390, 488);
  ctx.lineTo(382, 544);
  ctx.quadraticCurveTo(280, 588, 178, 544);
  ctx.closePath();
  ctx.fill();

  fillEllipse(150, 570, 46, 58, "#ff5a36", -0.34);
  fillEllipse(410, 570, 46, 58, "#ff5a36", 0.34);
  fillEllipse(144, 566, 24, 26, "#f0c348", -0.34);
  fillEllipse(416, 566, 24, 26, "#f0c348", 0.34);

  ctx.fillStyle = "#f0c348";
  ctx.beginPath();
  ctx.moveTo(280, 478);
  ctx.lineTo(302, 524);
  ctx.lineTo(352, 532);
  ctx.lineTo(316, 566);
  ctx.lineTo(324, 616);
  ctx.lineTo(280, 592);
  ctx.lineTo(236, 616);
  ctx.lineTo(244, 566);
  ctx.lineTo(208, 532);
  ctx.lineTo(258, 524);
  ctx.closePath();
  ctx.fill();

  fillEllipse(116, 470, 36, 44, "#f0c348", -0.35);
  fillEllipse(444, 470, 36, 44, "#f0c348", 0.35);
  strokePath("#ff5a36", 10, () => {
    ctx.moveTo(104, 446);
    ctx.lineTo(126, 494);
    ctx.moveTo(456, 446);
    ctx.lineTo(434, 494);
  });

  return canvas.toDataURL("image/png");
}

async function loadImageFromDataUrl(dataUrl, label, {
  autoRecommendSize = true,
  preserveCurrentProject = false,
  subjectOverride = null,
} = {}) {
  const loadedImage = await createImageFromDataUrl(dataUrl);
  rawImage.value = loadedImage;
  imageLabel.value = label;
  imageDataUrl.value = dataUrl;
  subjectBox.value = subjectOverride || await analyzeImageSubject(loadedImage);

  if (!preserveCurrentProject) {
    currentProjectId.value = null;
    resetHiddenColorState();
    clearEditState();
    if (autoRecommendSize) {
      config.targetWidth = recommendedTargetWidth.value;
      config.maxColors = recommendedMaxColors.value;
    }
  }

  if (!config.projectName.trim()) {
    config.projectName = label.replace(/\.[a-z0-9]+$/i, "");
  }

  await processCurrentImage();
}

async function bootSampleImage() {
  config.projectName = "示例拼豆图纸";
  config.targetWidth = 32;
  config.maxColors = 12;
  config.brand = "MARD";
  prep.cropRatio = "auto";
  prep.framing = "balanced";
  prep.styleMode = "clean_ink";
  prep.zoomAdjust = 1;
  prep.offsetX = 0;
  prep.offsetY = 0;
  await loadImageFromDataUrl(createSampleDataUrl(), "示例照片", { autoRecommendSize: false });
}

async function bootMarketingDemo() {
  const demoImage = await createImageFromDataUrl(createMarketingDemoDataUrl());
  const prepared = buildPreparedSource(demoImage, {
    cropRatio: "portrait",
    framing: "balanced",
    offsetX: 0,
    offsetY: -0.02,
    styleIntensity: 0.92,
    styleMode: "clean_ink",
    subjectBox: {
      x: 0.16,
      y: 0.08,
      width: 0.68,
      height: 0.74,
      confidence: 0.9,
    },
    zoomAdjust: 1,
  });
  const demo = processImage({
    source: prepared.canvas,
    brand: "MARD",
    maxColors: 14,
    targetWidth: 40,
    transparentBackground: true,
  });

  heroDemoSource.value = demoImage;
  heroDemoResult.width = demo.width;
  heroDemoResult.height = demo.height;
  heroDemoResult.matrix = demo.matrix;
  heroDemoResult.counts = demo.counts;
}

async function handleSelectedFile(file) {
  if (!file) return;

  const reader = new FileReader();
  reader.onload = async () => {
    const dataUrl = String(reader.result);
    config.projectName = file.name.replace(/\.[a-z0-9]+$/i, "");
    await loadImageFromDataUrl(dataUrl, file.name);
    config.stageView = "result";
    flowOpen.value = true;
    flowStep.value = "result";
    pushToast("图纸已生成，结果在弹窗里。");
  };
  reader.readAsDataURL(file);
}

function triggerHeroUpload() {
  openGenerationFlow("upload");
}

async function handleLaunchCommunitySeed(payload) {
  if (!payload || payload.sourceKind === "upload") {
    triggerHeroUpload();
    return;
  }

  try {
    config.projectName = payload.label || "社区示例图纸";
    config.brand = payload.brand || "MARD";
    config.maxColors = payload.maxColors || 12;
    config.targetWidth = payload.targetWidth || 32;
    prep.cropRatio = payload.cropRatio || "auto";
    prep.framing = payload.framing || "balanced";
    prep.styleMode = payload.styleMode || "clean_ink";
    prep.zoomAdjust = 1;
    prep.offsetX = 0;
    prep.offsetY = 0;

    await loadImageFromDataUrl(
      createCommunitySourceDataUrl(payload.sourceKind),
      `${payload.label || "社区示例"}.png`,
      {
        autoRecommendSize: false,
        subjectOverride: getCommunitySourceSubjectBox(payload.sourceKind),
      }
    );

    config.stageView = "result";
    flowOpen.value = true;
    flowStep.value = "result";
    pushToast(`已加载“${payload.label}”示例，可以继续调整。`);
  } catch (error) {
    console.error(error);
    pushToast("示例加载失败，请直接上传照片试试。");
    triggerHeroUpload();
  }
}

function openGenerationFlow(step) {
  const nextStep = step || (hasResult.value ? "tune" : "upload");
  flowStep.value = flowSteps.includes(nextStep) ? nextStep : "upload";
  flowOpen.value = true;
  nextTick(scheduleRender);
}

function closeGenerationFlow() {
  flowOpen.value = false;
  canvasEditMode.value = "";
  mergePreviewColorCodes.value = [];
  nextTick(scheduleRender);
}

function setFlowStep(step) {
  if (!flowSteps.includes(step)) return;
  if (step === "tune" && !rawImage.value) {
    pushToast("先上传一张照片。");
    return;
  }
  if (step === "result" && !hasResult.value) {
    pushToast("先生成图纸，再看结果。");
    return;
  }
  flowStep.value = step;
  if (step === "result") {
    config.stageView = "result";
  }
  nextTick(scheduleRender);
}

function handleFlowNext(step) {
  if (typeof step === "string") {
    setFlowStep(step);
    return;
  }

  const currentIndex = flowSteps.indexOf(flowStep.value);
  const nextStep = flowSteps[Math.min(currentIndex + 1, flowSteps.length - 1)];
  setFlowStep(nextStep);
}

function handleFlowPrevious() {
  const currentIndex = flowSteps.indexOf(flowStep.value);
  const previousStep = flowSteps[Math.max(currentIndex - 1, 0)];
  setFlowStep(previousStep);
}

function finishGenerationFlow() {
  flowOpen.value = false;
  config.stageView = "result";
  canvasEditMode.value = "";
  mergePreviewColorCodes.value = [];
  nextTick(scheduleRender);
}

function saveCurrentProject() {
  if (!hasResult.value || !imageDataUrl.value) {
    pushToast("先生成一张图纸。");
    return;
  }

  actionState.value = "save";
  try {
    const existingIndex = currentProjectId.value
      ? savedProjects.value.findIndex((item) => item.id === currentProjectId.value)
      : -1;

    const project = {
      id: existingIndex >= 0 ? currentProjectId.value : String(Date.now()),
      title: projectTitle.value,
      imageLabel: imageLabel.value,
      imageDataUrl: imageDataUrl.value,
      previewDataUrl: createSavedPreview({
        matrix: result.matrix,
        width: result.width,
        height: result.height,
        roundBeads: config.roundBeads,
      }),
      brand: config.brand,
      counts: result.counts.map((entry) => ({
        code: entry.code,
        count: entry.count,
        hex: entry.hex,
        name: entry.name,
      })),
      height: result.height,
      maxColors: config.maxColors,
      prep: {
        cropRatio: prep.cropRatio,
        framing: prep.framing,
        offsetX: prep.offsetX,
        offsetY: prep.offsetY,
        styleIntensity: prep.styleIntensity,
        styleMode: prep.styleMode,
        zoomAdjust: prep.zoomAdjust,
      },
      roundBeads: config.roundBeads,
      showCodes: config.showCodes,
      targetWidth: config.targetWidth,
      updatedAt: new Date().toISOString(),
      width: result.width,
    };

    if (existingIndex >= 0) {
      savedProjects.value.splice(existingIndex, 1);
    }
    savedProjects.value.unshift(project);
    currentProjectId.value = project.id;
    persistSavedProjects(savedProjects.value);
    pushToast(existingIndex >= 0 ? "已更新这张图纸。" : "已保存这张图纸。");
  } catch (error) {
    console.error(error);
    pushToast("保存失败，请再试一次。");
  } finally {
    actionState.value = "";
  }
}

function exportPng() {
  if (!hasResult.value) {
    pushToast("先生成一张图纸。");
    return;
  }
  actionState.value = "png";

  try {
    const canvas = createExportCanvas({
      title: projectTitle.value,
      brand: config.brand,
      width: result.width,
      height: result.height,
      counts: visibleCounts.value,
      matrix: result.matrix,
      showCodes: true,
      roundBeads: config.roundBeads,
      hiddenColorCodes: activeHiddenColorCodes.value,
      hiddenCellKeys: backgroundHiddenCellKeys.value,
    });

    canvas.toBlob((blob) => {
      if (!blob) {
        actionState.value = "";
        pushToast("下载失败，请再试一次。");
        return;
      }
      const fileSafeTitle = projectTitle.value.replace(/\s+/g, "-");
      const hiddenSuffix = activeHiddenColorCodes.value.length || backgroundHiddenCellKeys.value.length ? "-hidden" : "";
      downloadBlob(blob, `${fileSafeTitle || "pingdou-cn"}-${config.brand.toLowerCase()}-${result.width}x${result.height}${hiddenSuffix}.png`);
      actionState.value = "";
      pushToast("图纸图片已开始下载。");
    }, "image/png");
  } catch (error) {
    console.error(error);
    actionState.value = "";
    pushToast("下载失败，请再试一次。");
  }
}

function handlePreviewColors(codes) {
  mergePreviewColorCodes.value = Array.isArray(codes) ? codes : [];
  scheduleRender();
}

function handleCanvasEditMode(mode) {
  canvasEditMode.value = mode || "";
  scheduleRender();
}

function handleMergeColor({ from, to }) {
  const sourceCodes = (Array.isArray(from) ? from : [from]).filter((code) => code && code !== to);
  if (!sourceCodes.length || !to) return;

  const targetColor = result.counts.find((entry) => entry.code === to);
  if (!targetColor) return;

  pushEditSnapshot();
  result.matrix = sourceCodes.reduce((matrix, code) => replaceMatrixColor(matrix, code, targetColor), result.matrix);
  result.counts = buildVisibleCountsFromMatrix(result.matrix, [], [], result.width);
  config.hiddenColorCodes = config.hiddenColorCodes.filter((code) => !sourceCodes.includes(code));
  mergePreviewColorCodes.value = [];
  pushToast(`已把 ${sourceCodes.length} 色合并到 ${to}。`);
  scheduleRender();
}

function handlePaintRegion({
  startRow,
  endRow,
  startColumn,
  endColumn,
  colorCode,
}) {
  if (
    startRow < 0
    || startColumn < 0
    || endRow >= result.height
    || endColumn >= result.width
    || !colorCode
  ) {
    return;
  }

  const targetColor = result.counts.find((entry) => entry.code === colorCode);
  if (!targetColor) return;

  const {
    count: _count,
    outerScore: _outerScore,
    outerWeight: _outerWeight,
    ...matrixColor
  } = targetColor;
  let selectedCells = 0;
  let targetCells = 0;
  for (let rowIndex = startRow; rowIndex <= endRow; rowIndex += 1) {
    for (let columnIndex = startColumn; columnIndex <= endColumn; columnIndex += 1) {
      selectedCells += 1;
      if (result.matrix[rowIndex]?.[columnIndex]?.code === colorCode) {
        targetCells += 1;
      }
    }
  }

  const shouldErase = selectedCells > 0 && selectedCells === targetCells;
  let changedCount = 0;
  const nextMatrix = result.matrix.map((row, currentRowIndex) => {
    if (currentRowIndex < startRow || currentRowIndex > endRow) return row;

    return row.map((color, currentColumnIndex) => {
      if (
        currentColumnIndex < startColumn
        || currentColumnIndex > endColumn
      ) {
        return color;
      }
      if (shouldErase) {
        changedCount += 1;
        return null;
      }
      if (color?.code === colorCode) return color;
      changedCount += 1;
      return matrixColor;
    });
  });

  if (!changedCount) return;

  pushEditSnapshot();
  result.matrix = nextMatrix;
  result.counts = buildVisibleCountsFromMatrix(result.matrix, [], [], result.width);
  scheduleRender();
}

function handleUndoEdit() {
  const previous = editHistory.value.at(-1);
  if (!previous) return;

  editHistory.value = editHistory.value.slice(0, -1);
  result.matrix = cloneMatrix(previous.matrix);
  result.counts = cloneCounts(previous.counts);
  config.hiddenColorCodes = [...previous.hiddenColorCodes];
  mergePreviewColorCodes.value = [];
  scheduleRender();
}

watch(
  () => [
    config.brand,
    config.maxColors,
    config.targetWidth,
    prep.cropRatio,
    prep.framing,
    prep.offsetX,
    prep.offsetY,
    prep.styleIntensity,
    prep.styleMode,
    prep.zoomAdjust,
  ],
  () => {
    if (!rawImage.value) return;
    queueProcess();
  }
);

watch(
  selectedBrandColorLimit,
  (limit) => {
    if (config.maxColors > limit) {
      config.maxColors = limit;
    }
  }
);

watch(
  () => [config.showCodes, config.roundBeads, config.stageView, config.hiddenColorCodes, config.hideBackgroundBeads],
  async () => {
    await nextTick();
    scheduleRender();
  }
);

function handleResize() {
  scheduleRender();
}

function handleScroll() {
  headerVisible.value = window.scrollY > 180;
}

function setDemoStep(step, { pause = false } = {}) {
  if (!demoSteps.includes(step)) return;
  activeDemoStep.value = step;
  demoPaused.value = pause;
}

function startDemoCycle() {
  window.clearInterval(demoCycleTimer);
  demoCycleTimer = window.setInterval(() => {
    if (demoPaused.value) return;
    const currentIndex = demoSteps.indexOf(activeDemoStep.value);
    activeDemoStep.value = demoSteps[(currentIndex + 1) % demoSteps.length];
  }, 2200);
}

function pauseDemo(step) {
  setDemoStep(step, { pause: true });
}

function resumeDemo() {
  demoPaused.value = false;
}

onMounted(async () => {
  savedProjects.value = loadSavedProjects();
  window.addEventListener("resize", handleResize);
  window.addEventListener("scroll", handleScroll, { passive: true });
  canvasResizeObserver = new ResizeObserver(() => {
    scheduleRender();
  });
  if (!isCommunityPage) {
    await bootMarketingDemo();
    await bootSampleImage();
  }
  [
    heroSourceCanvas.value,
    heroResultCanvas.value,
    heroPatternCanvas.value,
  ].filter(Boolean).forEach((canvas) => {
    canvasResizeObserver.observe(canvas);
  });
  handleScroll();
  window.setTimeout(handleScroll, 120);
  window.setTimeout(scheduleRender, 180);
  if (!isCommunityPage) {
    startDemoCycle();
  }
});

onBeforeUnmount(() => {
  if (typeof document !== "undefined") {
    document.documentElement.classList.remove("is-community-route");
    document.body.classList.remove("is-community-route");
  }
  window.removeEventListener("resize", handleResize);
  window.removeEventListener("scroll", handleScroll);
  canvasResizeObserver?.disconnect();
  window.clearTimeout(processTimer);
  window.clearInterval(demoCycleTimer);
  cancelAnimationFrame(renderFrame);
});
</script>

<template>
  <div class="app-shell" :class="{ 'is-community-page': isCommunityPage }">
    <a class="site-corner-brand" :href="homePageHref">
      <span class="brand-mark"></span>
      <span class="hero-masthead-copy">
        <strong>拼豆.cn</strong>
        <small>在线出图纸</small>
      </span>
    </a>

    <header v-if="!isCommunityPage" class="site-header" :class="{ 'is-visible': headerVisible }">
      <a class="brand-lockup" :href="homePageHref">
        <span class="brand-mark"></span>
        <span class="brand-copy">
          <strong>拼豆.cn</strong>
          <small>照片转拼豆图纸</small>
        </span>
      </a>
      <nav class="site-nav">
        <a :href="communityPageHref">社区灵感</a>
        <a href="#studio">怎么开拼</a>
      </nav>
      <button
        class="action-button action-button-primary header-cta"
        type="button"
        @click="triggerHeroUpload"
      >
        开始制作
      </button>
    </header>

    <main id="top" class="site-main" :class="{ 'site-main-community': isCommunityPage }">
      <template v-if="isCommunityPage">
        <CommunityShowcase @launch-seed="handleLaunchCommunitySeed" />
      </template>

      <template v-else>
        <section id="studio" class="studio-panel studio-panel-prime">
          <div class="studio-welcome">
            <div class="studio-welcome-copy">
              <div class="welcome-heading">
                <h1>
                  <em class="hero-line hero-line-focus">照片变拼豆</em>
                  <em class="hero-line hero-line-focus">马上出图纸</em>
                </h1>
                <p class="studio-lead">
                  一键预览成品、色号和颗数，下载就能开拼。
                </p>
              </div>

              <div class="hero-side-stack">
                <a class="hero-community-entry" :href="communityPageHref">
                  <div class="hero-community-entry-copy">
                    <span class="quickstart-kicker hero-community-kicker">社区灵感页</span>
                    <strong>先看看大家最近都在做什么</strong>
                    <div class="hero-community-entry-meta" aria-label="社区题材">
                      <span
                        v-for="tag in heroCommunityTags"
                        :key="tag"
                        class="hero-community-tag"
                      >
                        {{ tag }}
                      </span>
                    </div>
                  </div>
                  <span class="hero-community-entry-cta">去社区页</span>
                </a>

                <div class="hero-launcher hero-launcher-primary">
                  <div class="hero-launcher-copy">
                    <span class="quickstart-kicker">{{ heroGuide.badge }}</span>
                    <p>{{ heroGuide.text }}</p>
                  </div>
                  <div class="hero-actions">
                    <button class="action-button action-button-primary" type="button" @click="triggerHeroUpload">
                      上传照片生成图纸
                    </button>
                  </div>
                  <small class="hero-action-note">先看效果，再决定尺寸、颜色和品牌。</small>
                </div>
              </div>
            </div>

            <div
              class="studio-welcome-side conversion-demo"
              :class="[`is-step-${activeDemoStep}`, { 'is-paused': demoPaused }]"
              @mouseleave="resumeDemo"
            >
              <div class="demo-topbar">
                <span>实时演示</span>
                <strong>照片到图纸，一气呵成</strong>
              </div>

              <div class="demo-stage" aria-label="照片转拼豆图纸的演示">
                <article
                  class="demo-panel demo-panel-source"
                  :class="{ 'is-active': activeDemoStep === 'source' }"
                  @mouseenter="pauseDemo('source')"
                  @focusin="pauseDemo('source')"
                >
                  <div class="canvas-caption">
                    <span>01 选照片</span>
                    <strong>{{ heroDemoLabel }}</strong>
                  </div>
                  <canvas ref="heroSourceCanvas" class="canvas-surface"></canvas>
                </article>

                <article
                  class="demo-panel demo-panel-result"
                  :class="{ 'is-active': activeDemoStep === 'result' }"
                  @mouseenter="pauseDemo('result')"
                  @focusin="pauseDemo('result')"
                >
                  <div class="canvas-caption">
                    <span>02 看成品</span>
                    <strong>{{ heroDemoResult.width }} × {{ heroDemoResult.height }}</strong>
                  </div>
                  <canvas ref="heroResultCanvas" class="canvas-surface"></canvas>
                </article>

                <article
                  class="demo-panel demo-panel-sheet"
                  :class="{ 'is-active': activeDemoStep === 'sheet' }"
                  @mouseenter="pauseDemo('sheet')"
                  @focusin="pauseDemo('sheet')"
                >
                  <div class="canvas-caption">
                    <span>03 拿图纸</span>
                    <strong>{{ heroDemoResult.counts.length }} 色 · {{ heroDemoTotalBeads.toLocaleString("zh-CN") }} 颗</strong>
                  </div>
                  <canvas ref="heroPatternCanvas" class="canvas-mini-sheet"></canvas>
                  <div class="demo-sheet-codes" aria-label="示例图纸色号">
                    <span
                      v-for="item in heroDemoResult.counts.slice(0, 4)"
                      :key="item.code"
                      :title="`${item.code} ${item.name} · ${item.count} 颗`"
                    >
                      <i :style="{ background: item.hex }" aria-hidden="true"></i>
                      {{ item.code }}
                      <b>{{ item.count }}</b>
                    </span>
                  </div>
                </article>
              </div>

              <div class="demo-flowbar">
                <div class="demo-flow-progress"></div>
                <button
                  class="demo-flow-step"
                  :class="{ 'is-active': activeDemoStep === 'source' }"
                  type="button"
                  @click="pauseDemo('source')"
                  @mouseenter="pauseDemo('source')"
                  @focus="pauseDemo('source')"
                >
                  选照片
                </button>
                <button
                  class="demo-flow-step"
                  :class="{ 'is-active': activeDemoStep === 'result' }"
                  type="button"
                  @click="pauseDemo('result')"
                  @mouseenter="pauseDemo('result')"
                  @focus="pauseDemo('result')"
                >
                  看效果
                </button>
                <button
                  class="demo-flow-step"
                  :class="{ 'is-active': activeDemoStep === 'sheet' }"
                  type="button"
                  @click="pauseDemo('sheet')"
                  @mouseenter="pauseDemo('sheet')"
                  @focus="pauseDemo('sheet')"
                >
                  拿图纸
                </button>
              </div>

              <div class="demo-output-row">
                <div>
                  <span>图纸尺寸</span>
                  <strong>{{ heroDemoResult.width }} × {{ heroDemoResult.height }}</strong>
                </div>
                <div>
                  <span>色号数量</span>
                  <strong>{{ heroDemoResult.counts.length }} 色</strong>
                </div>
                <div>
                  <span>预计颗数</span>
                  <strong>{{ heroDemoTotalBeads.toLocaleString("zh-CN") }} 颗</strong>
                </div>
              </div>
            </div>
          </div>
        </section>
      </template>
    </main>

    <GenerationDialog
      ref="generationDialogRef"
      :action-state="actionState"
      :aspect-label="aspectLabel"
      :background-color-code="backgroundColorCode"
      :brand="config.brand"
      :brand-meta="brandMeta"
      :can-undo="editHistory.length > 0"
      :counts="result.counts"
      :crop-ratio="prep.cropRatio"
      :current-step="flowStep"
      :edit-session-key="editSessionKey"
      :framing="prep.framing"
      :framings="framingPresets"
      :has-result="hasResult"
      :height="result.height"
      :hide-background-beads="config.hideBackgroundBeads"
      :hidden-color-codes="config.hiddenColorCodes"
      :max-colors="config.maxColors"
      :max-color-limit="selectedBrandColorLimit"
      :max-target-width="MAX_TARGET_SIZE"
      :open="flowOpen"
      :prepared-label="preparedLabel"
      :processing="processing"
      :recommended-aspect-label="recommendedAspectLabel"
      :recommended-max-colors="recommendedMaxColors"
      :ratios="cropRatioPresets"
      :recommended-target-width="recommendedTargetWidth"
      :round-beads="config.roundBeads"
      :show-codes="config.showCodes"
      :size-presets="tuneSizePresets"
      :stage-view="config.stageView"
      :style-mode="prep.styleMode"
      :style-presets="stylePresets"
      :target-width="config.targetWidth"
      :total-beads="totalBeads"
      :width="result.width"
      :visible-color-count="visibleCounts.length"
      @close="closeGenerationFlow"
      @edit-mode-change="handleCanvasEditMode"
      @export-png="exportPng"
      @file-selected="handleSelectedFile"
      @finish="finishGenerationFlow"
      @next-step="handleFlowNext"
      @paint-region="handlePaintRegion"
      @preview-colors="handlePreviewColors"
      @preview-layout-change="scheduleRender"
      @previous-step="handleFlowPrevious"
      @undo-edit="handleUndoEdit"
      @update:brand="config.brand = $event"
      @update:cropRatio="prep.cropRatio = $event"
      @update:framing="prep.framing = $event"
      @update:hideBackgroundBeads="config.hideBackgroundBeads = $event"
      @update:hiddenColorCodes="config.hiddenColorCodes = $event"
      @update:maxColors="config.maxColors = $event"
      @update:roundBeads="config.roundBeads = $event"
      @update:showCodes="config.showCodes = $event"
      @update:stageView="config.stageView = $event"
      @update:styleMode="prep.styleMode = $event"
      @update:targetWidth="config.targetWidth = $event"
      @merge-color="handleMergeColor"
    />

    <transition-group name="toast" tag="div" class="toast-stack">
      <div v-for="item in toasts" :key="item.id" class="toast-item">
        {{ item.message }}
      </div>
    </transition-group>
  </div>
</template>
