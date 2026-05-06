<script setup>
import { computed, reactive, ref, watch } from "vue";
import { getPreviewCellSize } from "../lib/beadEngine.js";

const props = defineProps({
  actionState: {
    type: String,
    default: "",
  },
  aspectLabel: {
    type: String,
    required: true,
  },
  backgroundColorCode: {
    type: String,
    default: "",
  },
  brand: {
    type: String,
    required: true,
  },
  brandMeta: {
    type: Object,
    required: true,
  },
  counts: {
    type: Array,
    required: true,
  },
  currentStep: {
    type: String,
    required: true,
  },
  canUndo: {
    type: Boolean,
    default: false,
  },
  cropRatio: {
    type: String,
    required: true,
  },
  framing: {
    type: String,
    required: true,
  },
  framings: {
    type: Object,
    required: true,
  },
  editSessionKey: {
    type: Number,
    default: 0,
  },
  height: {
    type: Number,
    required: true,
  },
  hiddenColorCodes: {
    type: Array,
    default: () => [],
  },
  hideBackgroundBeads: {
    type: Boolean,
    default: false,
  },
  hasResult: {
    type: Boolean,
    required: true,
  },
  maxColors: {
    type: Number,
    required: true,
  },
  maxColorLimit: {
    type: Number,
    default: 36,
  },
  maxTargetWidth: {
    type: Number,
    default: 120,
  },
  open: {
    type: Boolean,
    required: true,
  },
  preparedLabel: {
    type: String,
    required: true,
  },
  processing: {
    type: Boolean,
    default: false,
  },
  recommendedAspectLabel: {
    type: String,
    required: true,
  },
  recommendedMaxColors: {
    type: Number,
    required: true,
  },
  ratios: {
    type: Object,
    required: true,
  },
  recommendedTargetWidth: {
    type: Number,
    default: 48,
  },
  roundBeads: {
    type: Boolean,
    required: true,
  },
  sizePresets: {
    type: Array,
    default: () => [],
  },
  showCodes: {
    type: Boolean,
    required: true,
  },
  stageView: {
    type: String,
    required: true,
  },
  styleMode: {
    type: String,
    required: true,
  },
  stylePresets: {
    type: Object,
    required: true,
  },
  targetWidth: {
    type: Number,
    required: true,
  },
  totalBeads: {
    type: Number,
    required: true,
  },
  visibleColorCount: {
    type: Number,
    required: true,
  },
  width: {
    type: Number,
    required: true,
  },
});

const emit = defineEmits([
  "close",
  "edit-mode-change",
  "export-png",
  "file-selected",
  "finish",
  "merge-color",
  "next-step",
  "paint-region",
  "preview-colors",
  "preview-layout-change",
  "previous-step",
  "undo-edit",
  "update:brand",
  "update:cropRatio",
  "update:framing",
  "update:hideBackgroundBeads",
  "update:hiddenColorCodes",
  "update:maxColors",
  "update:roundBeads",
  "update:showCodes",
  "update:stageView",
  "update:styleMode",
  "update:targetWidth",
]);

const fileInput = ref(null);
const isDragOver = ref(false);
const sourceCanvas = ref(null);
const preparedCanvas = ref(null);
const resultCanvas = ref(null);
const patternCanvas = ref(null);
const mergeMode = ref(false);
const mergeSelectedCodes = ref([]);
const paintMode = ref(false);
const paintColorCode = ref("");
const toolDockPinned = ref(false);
const paintDragStart = ref(null);
const paintDragEnd = ref(null);
const paintSelectionMetrics = ref(null);
const activeHiddenColorCodes = computed(() => {
  return [...new Set(props.hiddenColorCodes)];
});
const hiddenColorCount = computed(() => activeHiddenColorCodes.value.length);
const currentColorLabel = computed(() => `${props.visibleColorCount}`);
const currentBeadCountLabel = computed(() => props.totalBeads.toLocaleString("zh-CN"));
const toolDockActionCount = computed(() => {
  let count = 0;
  if (props.backgroundColorCode) count += 1;
  if (!paintMode.value) count += 1;
  if (!mergeMode.value) count += 1;
  if (paintMode.value || (props.canUndo && !mergeMode.value)) count += 1;
  if (hiddenColorCount.value && !mergeMode.value && !paintMode.value) count += 1;
  return count;
});
const toolDockExpanded = computed(() => toolDockPinned.value || mergeMode.value || paintMode.value);
const toolDockHandleLabel = computed(() => (toolDockExpanded.value ? "收起" : "工具"));
const toolDockHandleMeta = computed(() => (
  paintMode.value ? "改点中" : mergeMode.value ? "合并中" : `${toolDockActionCount.value} 项`
));
const sizePresetOptions = computed(() => {
  const candidates = [
    ...props.sizePresets,
    props.recommendedTargetWidth,
    props.targetWidth,
  ];
  return [...new Set(candidates.filter((size) => (
    Number.isFinite(size)
    && size >= 18
    && size <= props.maxTargetWidth
  )))].sort((left, right) => left - right);
});
const colorPresetOptions = computed(() => {
  const candidates = [12, 18, 24, 32, props.maxColorLimit];
  return [...new Set(candidates.filter((count) => (
    Number.isFinite(count)
    && count >= 4
    && count <= props.maxColorLimit
  )))].sort((left, right) => left - right);
});
const mergeToolbarText = computed(() => {
  if (mergeMode.value) {
    return mergeSelectedCodes.value.length ? `已选 ${mergeSelectedCodes.value.length} 色` : "点选要合并的颜色";
  }
  if (paintMode.value) {
    return paintColorCode.value ? `改成 ${paintColorCode.value}` : "选择色号";
  }
  return "色号清单";
});
const mergeTargetItem = computed(() => {
  if (mergeSelectedCodes.value.length < 2) return null;
  const selectedCodes = new Set(mergeSelectedCodes.value);
  return props.counts
    .filter((item) => selectedCodes.has(item.code))
    .slice()
    .sort((left, right) => right.count - left.count)[0] || null;
});
const mergeSourceCodes = computed(() => {
  if (!mergeTargetItem.value) return [];
  return mergeSelectedCodes.value.filter((code) => code !== mergeTargetItem.value.code);
});
const canConfirmMerge = computed(() => (
  mergeMode.value
  && mergeTargetItem.value
  && mergeSourceCodes.value.length > 0
));
const paintSelectionStyle = computed(() => {
  if (!paintDragStart.value || !paintDragEnd.value || !paintSelectionMetrics.value) return null;

  const start = paintDragStart.value;
  const end = paintDragEnd.value;
  const metrics = paintSelectionMetrics.value;
  const minColumn = Math.min(start.columnIndex, end.columnIndex);
  const maxColumn = Math.max(start.columnIndex, end.columnIndex);
  const minRow = Math.min(start.rowIndex, end.rowIndex);
  const maxRow = Math.max(start.rowIndex, end.rowIndex);

  return {
    left: `${metrics.canvasOffsetLeft + metrics.offsetX + minColumn * metrics.cell}px`,
    top: `${metrics.canvasOffsetTop + metrics.offsetY + minRow * metrics.cell}px`,
    width: `${(maxColumn - minColumn + 1) * metrics.cell}px`,
    height: `${(maxRow - minRow + 1) * metrics.cell}px`,
  };
});

const steps = [
  { key: "upload", label: "上传照片" },
  { key: "tune", label: "调整参数" },
  { key: "result", label: "拿到图纸" },
];

function toggleToolDockPinned(event) {
  if (mergeMode.value || paintMode.value) return;
  toolDockPinned.value = !toolDockPinned.value;
  if (!toolDockPinned.value) {
    event?.currentTarget?.blur?.();
  }
}

function openPicker() {
  fileInput.value?.click();
}

function clampNumber(value, min, max, fallback) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return fallback;
  return Math.min(max, Math.max(min, Math.round(parsed)));
}

function updateTargetWidth(value) {
  emit("update:targetWidth", clampNumber(value, 18, props.maxTargetWidth, props.targetWidth));
}

function updateMaxColors(value) {
  emit("update:maxColors", clampNumber(value, 4, props.maxColorLimit, props.maxColors));
}

function forwardFile(file) {
  if (!file) return;
  emit("file-selected", file);
}

function handleChange(event) {
  const [file] = event.target.files || [];
  forwardFile(file);
  event.target.value = "";
}

function handleDrop(event) {
  const [file] = event.dataTransfer?.files || [];
  isDragOver.value = false;
  forwardFile(file);
}

function isColorHidden(code) {
  return activeHiddenColorCodes.value.includes(code);
}

function toggleHiddenColor(code) {
  const nextCodes = new Set(props.hiddenColorCodes);
  if (nextCodes.has(code)) {
    nextCodes.delete(code);
  } else {
    nextCodes.add(code);
  }
  emit("update:hiddenColorCodes", [...nextCodes]);
}

function showAllColors() {
  emit("update:hiddenColorCodes", []);
}

function syncPreviewHighlights() {
  if (mergeMode.value) {
    emit("preview-colors", mergeSelectedCodes.value);
    return;
  }
  emit("preview-colors", []);
}

function syncEditMode() {
  emit("edit-mode-change", paintMode.value ? "paint" : mergeMode.value ? "merge" : "");
}

function resetPaintDrag() {
  paintDragStart.value = null;
  paintDragEnd.value = null;
  paintSelectionMetrics.value = null;
}

function resetInteractionState() {
  mergeMode.value = false;
  mergeSelectedCodes.value = [];
  paintMode.value = false;
  paintColorCode.value = "";
  toolDockPinned.value = false;
  resetPaintDrag();
  syncPreviewHighlights();
  syncEditMode();
}

function setMergeMode(nextMode) {
  mergeMode.value = nextMode;
  if (nextMode) {
    paintMode.value = false;
    resetPaintDrag();
  }
  if (!nextMode) {
    mergeSelectedCodes.value = [];
  }
  syncPreviewHighlights();
  syncEditMode();
}

function toggleMergeMode() {
  setMergeMode(!mergeMode.value);
}

function isMergeSelected(code) {
  return mergeSelectedCodes.value.includes(code);
}

function isPaintSelected(code) {
  return paintMode.value && paintColorCode.value === code;
}

function setPaintMode(nextMode) {
  paintMode.value = nextMode;
  if (nextMode) {
    mergeMode.value = false;
    mergeSelectedCodes.value = [];
    if (!paintColorCode.value && props.counts[0]?.code) {
      paintColorCode.value = props.counts[0].code;
    }
    if (props.stageView !== "sheet") {
      emit("update:stageView", "sheet");
    }
  } else {
    resetPaintDrag();
  }
  syncPreviewHighlights();
  syncEditMode();
}

function togglePaintMode() {
  setPaintMode(!paintMode.value);
}

function toggleMergeSelection(code) {
  const nextCodes = new Set(mergeSelectedCodes.value);
  if (nextCodes.has(code)) {
    nextCodes.delete(code);
  } else {
    nextCodes.add(code);
  }
  mergeSelectedCodes.value = [...nextCodes];
  syncPreviewHighlights();
}

function handlePaletteClick(code) {
  if (mergeMode.value) {
    toggleMergeSelection(code);
    return;
  }

  if (paintMode.value) {
    paintColorCode.value = code;
    syncPreviewHighlights();
    return;
  }

  toggleHiddenColor(code);
}

function confirmMergeColor() {
  if (!canConfirmMerge.value) return;
  emit("merge-color", { from: mergeSourceCodes.value, to: mergeTargetItem.value.code });
  setMergeMode(false);
}

function getCanvasMetrics(event, mode) {
  const canvas = event.currentTarget;
  const rect = canvas.getBoundingClientRect();
  const canvasWidth = rect.width || canvas.clientWidth;
  const canvasHeight = rect.height || canvas.clientHeight;
  const padding = 8;
  const cell = getPreviewCellSize({
    containerWidth: canvasWidth,
    containerHeight: canvasHeight,
    gridWidth: props.width,
    gridHeight: props.height,
    padding,
  });
  const artworkWidth = props.width * cell;
  const artworkHeight = props.height * cell;
  const offsetX = Math.floor((canvasWidth - artworkWidth) / 2);
  const offsetY = Math.floor((canvasHeight - artworkHeight) / 2);

  return {
    canvasOffsetLeft: canvas.offsetLeft,
    canvasOffsetTop: canvas.offsetTop,
    canvasWidth,
    canvasHeight,
    cell,
    offsetX,
    offsetY,
  };
}

function getCanvasCellFromPointer(event, mode) {
  if (!paintMode.value || !paintColorCode.value || !props.width || !props.height) return null;

  const canvas = event.currentTarget;
  const rect = canvas.getBoundingClientRect();
  const canvasWidth = rect.width || canvas.clientWidth;
  const canvasHeight = rect.height || canvas.clientHeight;
  const pointerX = (event.clientX - rect.left) * (canvasWidth / rect.width);
  const pointerY = (event.clientY - rect.top) * (canvasHeight / rect.height);
  const metrics = getCanvasMetrics(event, mode);
  const columnIndex = Math.floor((pointerX - metrics.offsetX) / metrics.cell);
  const rowIndex = Math.floor((pointerY - metrics.offsetY) / metrics.cell);

  if (
    rowIndex < 0
    || rowIndex >= props.height
    || columnIndex < 0
    || columnIndex >= props.width
  ) {
    return null;
  }

  return { rowIndex, columnIndex, metrics };
}

function handlePaintPointerDown(event, mode) {
  const cell = getCanvasCellFromPointer(event, mode);
  if (!cell) return;
  event.preventDefault();
  event.currentTarget.setPointerCapture?.(event.pointerId);
  paintSelectionMetrics.value = cell.metrics;
  paintDragStart.value = {
    rowIndex: cell.rowIndex,
    columnIndex: cell.columnIndex,
  };
  paintDragEnd.value = {
    rowIndex: cell.rowIndex,
    columnIndex: cell.columnIndex,
  };
}

function handlePaintPointerMove(event, mode) {
  if (!paintDragStart.value) return;
  const cell = getCanvasCellFromPointer(event, mode);
  if (!cell) return;
  event.preventDefault();
  paintSelectionMetrics.value = cell.metrics;
  paintDragEnd.value = {
    rowIndex: cell.rowIndex,
    columnIndex: cell.columnIndex,
  };
}

function handlePaintPointerUp(event, mode) {
  if (!paintDragStart.value || !paintDragEnd.value) return;
  const cell = getCanvasCellFromPointer(event, mode);
  if (cell) {
    paintDragEnd.value = {
      rowIndex: cell.rowIndex,
      columnIndex: cell.columnIndex,
    };
  }

  const start = paintDragStart.value;
  const end = paintDragEnd.value;
  event.preventDefault();
  event.currentTarget.releasePointerCapture?.(event.pointerId);
  emit("paint-region", {
    startRow: Math.min(start.rowIndex, end.rowIndex),
    endRow: Math.max(start.rowIndex, end.rowIndex),
    startColumn: Math.min(start.columnIndex, end.columnIndex),
    endColumn: Math.max(start.columnIndex, end.columnIndex),
    colorCode: paintColorCode.value,
  });
  resetPaintDrag();
}

function handlePaintPointerCancel() {
  resetPaintDrag();
}

function getCanvases() {
  return {
    source: sourceCanvas.value,
    prepared: preparedCanvas.value,
    result: resultCanvas.value,
    pattern: patternCanvas.value,
  };
}

defineExpose({
  getCanvases,
  getHighlightedColorCodes: () => {
    if (mergeMode.value) return mergeSelectedCodes.value;
    return [];
  },
});

watch(
  () => props.counts.map((item) => item.code).join("|"),
  () => {
    const validCodes = new Set(props.counts.map((item) => item.code));
    if (mergeMode.value) {
      mergeSelectedCodes.value = mergeSelectedCodes.value.filter((code) => validCodes.has(code));
    }
    if (paintMode.value && !validCodes.has(paintColorCode.value)) {
      paintColorCode.value = props.counts[0]?.code || "";
    }
    syncPreviewHighlights();
  }
);

watch(
  () => props.editSessionKey,
  () => {
    resetInteractionState();
  }
);

watch(
  () => props.stageView,
  () => {
    if (!mergeMode.value && !paintMode.value) {
      toolDockPinned.value = false;
    }
  }
);

watch(
  () => props.open,
  (nextOpen) => {
    if (!nextOpen) return;
    toolDockPinned.value = false;
  }
);
</script>

<template>
  <Teleport to="body">
    <Transition name="flow-dialog">
      <div v-if="open" class="flow-dialog-backdrop" @click.self="$emit('close')">
        <section
          class="flow-dialog"
          :class="`is-step-${currentStep}`"
          role="dialog"
          aria-modal="true"
          aria-labelledby="flow-dialog-title"
          @keydown.esc="$emit('close')"
        >
          <div class="flow-dialog-head">
            <div>
              <span class="card-kicker">快速制作</span>
              <h2 id="flow-dialog-title">
                {{ currentStep === "result" ? "图纸已生成" : currentStep === "tune" ? "调整图纸" : "上传照片" }}
              </h2>
            </div>
            <button class="flow-close" type="button" aria-label="关闭" @click="$emit('close')">×</button>
          </div>

          <div v-if="currentStep !== 'result'" class="flow-steps" aria-label="制作步骤">
            <button
              v-for="(step, index) in steps"
              :key="step.key"
              class="flow-step"
              :class="{ 'is-active': currentStep === step.key }"
              type="button"
              @click="$emit('next-step', step.key)"
            >
              <span>{{ String(index + 1).padStart(2, "0") }}</span>
              <strong>{{ step.label }}</strong>
            </button>
          </div>

          <input
            ref="fileInput"
            class="sr-only"
            type="file"
            accept="image/*"
            @change="handleChange"
          />

          <div v-show="currentStep === 'upload'" class="flow-panel flow-panel-upload">
            <div class="flow-copy upload-primer">
              <button class="upload-kicker" type="button" @click="openPicker">
                上传照片
              </button>
              <h3>上传照片生成图纸</h3>
              <p>自动生成成品预览、色号和颗数。满意就下载开拼。</p>
            </div>
            <button
              class="flow-upload-zone"
              :class="{ 'is-dragover': isDragOver }"
              type="button"
              @click="openPicker"
              @dragenter.prevent="isDragOver = true"
              @dragover.prevent="isDragOver = true"
              @dragleave.prevent="isDragOver = false"
              @dragend.prevent="isDragOver = false"
              @drop.prevent="handleDrop"
            >
              <span class="upload-zone-orbit" aria-hidden="true"></span>
              <span class="upload-zone-icon" aria-hidden="true">
                <span></span>
              </span>
              <strong>{{ processing ? "正在生成..." : "选择照片" }}</strong>
              <span>拖到这里也可以</span>
              <small>支持 JPG / PNG / WebP</small>
            </button>
          </div>

          <div v-show="currentStep === 'tune'" class="flow-panel flow-panel-tune">
            <div class="flow-tune-grid">
              <label class="flow-control-block flow-control-block-primary">
                <div class="field-titleline">
                  <span class="field-label">最长边尺寸</span>
                  <button
                    class="parameter-badge parameter-badge-button"
                    type="button"
                    @click="updateTargetWidth(recommendedTargetWidth)"
                  >
                    图片推荐 {{ recommendedTargetWidth }}
                  </button>
                </div>
                <input
                  class="range-input"
                  :value="targetWidth"
                  type="range"
                  min="18"
                  :max="maxTargetWidth"
                  step="1"
                  @input="updateTargetWidth($event.target.value)"
                />
                <div class="field-inline">
                  <strong>{{ aspectLabel }} · 最长边 {{ targetWidth }} 格</strong>
                  <input
                    class="parameter-number"
                    :value="targetWidth"
                    aria-label="最长边尺寸"
                    inputmode="numeric"
                    type="number"
                    min="18"
                    :max="maxTargetWidth"
                    step="1"
                    @change="updateTargetWidth($event.target.value)"
                  />
                </div>
                <div class="choice-row choice-row-wrap">
                  <button
                    v-for="size in sizePresetOptions"
                    :key="size"
                    class="chip-button"
                    :class="{ 'is-active': targetWidth === size }"
                    type="button"
                    @click="updateTargetWidth(size)"
                  >
                    {{ size }} 格
                  </button>
                </div>
              </label>

              <label class="flow-control-block flow-control-block-primary">
                <div class="field-titleline">
                  <span class="field-label">最大颜色数</span>
                  <div class="choice-row">
                    <button
                      class="parameter-badge parameter-badge-button"
                      type="button"
                      @click="updateMaxColors(recommendedMaxColors)"
                    >
                      图片推荐 {{ recommendedMaxColors }}
                    </button>
                    <span class="parameter-badge">当前品牌 {{ maxColorLimit }} 色</span>
                  </div>
                </div>
                <input
                  class="range-input"
                  :value="maxColors"
                  type="range"
                  min="4"
                  :max="maxColorLimit"
                  step="1"
                  @input="updateMaxColors($event.target.value)"
                />
                <div class="field-inline">
                  <strong>{{ maxColors }} 色</strong>
                  <input
                    class="parameter-number"
                    :value="maxColors"
                    aria-label="最大颜色数"
                    inputmode="numeric"
                    type="number"
                    min="4"
                    :max="maxColorLimit"
                    step="1"
                    @change="updateMaxColors($event.target.value)"
                  />
                </div>
                <div class="choice-row choice-row-wrap">
                  <button
                    v-for="count in colorPresetOptions"
                    :key="count"
                    class="chip-button"
                    :class="{ 'is-active': maxColors === count }"
                    type="button"
                    @click="updateMaxColors(count)"
                  >
                    {{ count }} 色
                  </button>
                </div>
              </label>

              <div class="flow-control-block">
                <span class="field-label">豆子品牌</span>
                <div class="choice-row choice-row-wrap brand-choice-row">
                  <button
                    v-for="(meta, key) in brandMeta"
                    :key="key"
                    class="chip-button brand-chip"
                    :class="{ 'is-active': brand === key }"
                    type="button"
                    :title="meta.note"
                    @click="$emit('update:brand', key)"
                  >
                    {{ key }}
                  </button>
                </div>
              </div>

              <div class="flow-control-block">
                <span class="field-label">画面比例</span>
                <div class="choice-row choice-row-wrap">
                  <button
                    v-for="(ratio, key) in ratios"
                    :key="key"
                    class="chip-button"
                    :class="{ 'is-active': cropRatio === key }"
                    type="button"
                    @click="$emit('update:cropRatio', key)"
                  >
                    {{ ratio.label }}
                  </button>
                </div>
              </div>

              <div class="flow-control-block">
                <span class="field-label">取景方式</span>
                <div class="choice-row choice-row-wrap">
                  <button
                    v-for="(framingOption, key) in framings"
                    :key="key"
                    class="chip-button"
                    :class="{ 'is-active': framing === key }"
                    type="button"
                    @click="$emit('update:framing', key)"
                  >
                    {{ framingOption.label }}
                  </button>
                </div>
              </div>

              <div class="flow-control-block">
                <span class="field-label">风格处理</span>
                <div class="choice-row choice-row-wrap">
                  <button
                    v-for="(style, key) in stylePresets"
                    :key="key"
                    class="chip-button"
                    :class="{ 'is-active': styleMode === key }"
                    type="button"
                    @click="$emit('update:styleMode', key)"
                  >
                    {{ style.label }}
                  </button>
                </div>
              </div>
            </div>

          </div>

          <div v-show="currentStep === 'result'" class="flow-panel flow-panel-result">
            <div class="flow-result-layout" :class="{ 'is-paint-active': paintMode }">
              <section class="flow-result-preview" :class="{ 'is-paint-active': paintMode }">
                <div class="flow-preview-switch">
                  <button
                    class="stage-button"
                    :class="{ 'is-active': stageView === 'original' }"
                    type="button"
                    @click="$emit('update:stageView', 'original')"
                  >
                    原照片
                  </button>
                  <button
                    class="stage-button"
                    :class="{ 'is-active': stageView === 'prepared' }"
                    type="button"
                    @click="$emit('update:stageView', 'prepared')"
                  >
                    取景
                  </button>
                  <button
                    class="stage-button"
                    :class="{ 'is-active': stageView === 'result' }"
                    type="button"
                    @click="$emit('update:stageView', 'result')"
                  >
                    成品感
                  </button>
                  <button
                    class="stage-button"
                    :class="{ 'is-active': stageView === 'sheet' }"
                    type="button"
                    @click="$emit('update:stageView', 'sheet')"
                  >
                    制作图纸
                  </button>
                </div>

                <div
                  class="flow-preview-canvas"
                  :class="{
                    'is-paint-active': paintMode,
                    'is-sheet-view': stageView === 'sheet',
                  }"
                >
                  <div
                    class="canvas-tool-dock"
                    :class="{ 'is-editing': mergeMode || paintMode, 'is-pinned': toolDockPinned }"
                  >
                    <button
                      class="canvas-tool-handle"
                      :aria-expanded="toolDockExpanded"
                      :disabled="mergeMode || paintMode"
                      type="button"
                      @click="toggleToolDockPinned"
                    >
                      <span class="canvas-tool-handle-glyph" aria-hidden="true"></span>
                      <span class="canvas-tool-handle-copy">
                        <strong>{{ toolDockHandleLabel }}</strong>
                        <small>{{ toolDockHandleMeta }}</small>
                      </span>
                    </button>
                    <div class="canvas-tool-dock-body">
                      <button
                        v-if="backgroundColorCode"
                        class="canvas-inline-switch"
                        :class="{ 'is-active': hideBackgroundBeads }"
                        :aria-label="hideBackgroundBeads ? '显示底色' : '隐藏底色'"
                        :aria-pressed="hideBackgroundBeads"
                        type="button"
                        @click="$emit('update:hideBackgroundBeads', !hideBackgroundBeads)"
                      >
                        <span class="canvas-inline-switch-track" aria-hidden="true"></span>
                        <span class="canvas-inline-switch-text">底色</span>
                      </button>
                      <div class="canvas-tool-actions" aria-label="画布编辑工具">
                        <button
                          v-if="!paintMode"
                          class="canvas-tool-button"
                          :class="{ 'is-active': mergeMode }"
                          type="button"
                          @click="toggleMergeMode"
                        >
                          {{ mergeMode ? "取消合并" : "合并颜色" }}
                        </button>
                        <button
                          v-if="!mergeMode"
                          class="canvas-tool-button"
                          :class="{ 'is-active': paintMode }"
                          type="button"
                          @click="togglePaintMode"
                        >
                          {{ paintMode ? "完成改点" : "改豆点" }}
                        </button>
                        <button
                          v-if="paintMode || (canUndo && !mergeMode)"
                          class="canvas-tool-button"
                          type="button"
                          :disabled="!canUndo"
                          @click="$emit('undo-edit')"
                        >
                          撤销
                        </button>
                        <button
                          v-if="hiddenColorCount && !mergeMode && !paintMode"
                          class="canvas-tool-button"
                          type="button"
                          @click="showAllColors"
                        >
                          显示全部
                        </button>
                      </div>
                    </div>
                  </div>
                  <div v-if="paintMode" class="paint-mode-chip">
                    <span>改豆点</span>
                    <strong>{{ paintColorCode || "选色号" }}</strong>
                  </div>
                  <div
                    class="flow-preview-scroll"
                    :class="{
                      'is-result-view': stageView === 'result',
                      'is-sheet-view': stageView === 'sheet',
                    }"
                  >
                    <canvas
                      v-show="stageView === 'result'"
                      ref="resultCanvas"
                      class="canvas-surface"
                      @pointercancel="handlePaintPointerCancel"
                      @pointerdown="handlePaintPointerDown($event, 'result')"
                      @pointermove="handlePaintPointerMove($event, 'result')"
                      @pointerup="handlePaintPointerUp($event, 'result')"
                    ></canvas>
                    <canvas
                      v-show="stageView === 'sheet'"
                      ref="patternCanvas"
                      class="canvas-sheet"
                      @pointercancel="handlePaintPointerCancel"
                      @pointerdown="handlePaintPointerDown($event, 'sheet')"
                      @pointermove="handlePaintPointerMove($event, 'sheet')"
                      @pointerup="handlePaintPointerUp($event, 'sheet')"
                    ></canvas>
                    <div
                      v-if="paintSelectionStyle"
                      class="paint-selection-box"
                      :style="paintSelectionStyle"
                      aria-hidden="true"
                    ></div>
                    <canvas v-show="stageView === 'original'" ref="sourceCanvas" class="canvas-surface"></canvas>
                    <canvas v-show="stageView === 'prepared'" ref="preparedCanvas" class="canvas-surface"></canvas>
                  </div>
                  <div v-if="processing" class="stage-processing">
                    <strong>正在更新结果</strong>
                    <span>新的成品感和图纸马上出现。</span>
                  </div>
                </div>
              </section>

              <aside
                class="flow-result-summary"
                :class="{ 'is-merge-active': mergeMode, 'is-paint-active': paintMode }"
              >
                <div class="flow-result-cards">
                  <button
                    class="flow-result-card flow-result-card-button flow-result-overview"
                    type="button"
                    aria-label="调整图纸参数"
                    @click="$emit('next-step', 'tune')"
                  >
                    <div class="flow-result-overview-head">
                      <div class="flow-result-overview-copy">
                        <span>图纸规格</span>
                        <small>点一下回到参数页微调</small>
                      </div>
                      <span class="flow-result-overview-trigger">调整参数</span>
                    </div>
                    <div class="flow-result-overview-strip" aria-hidden="true">
                      <div class="flow-result-overview-metric flow-result-overview-metric-size">
                        <span>尺寸</span>
                        <strong class="flow-result-overview-size">
                          <b>{{ props.width }}</b>
                          <i>×</i>
                          <b>{{ props.height }}</b>
                        </strong>
                      </div>
                      <div class="flow-result-overview-metric">
                        <span>颜色</span>
                        <strong>{{ currentColorLabel }}</strong>
                      </div>
                      <div class="flow-result-overview-metric">
                        <span>颗数</span>
                        <strong>{{ currentBeadCountLabel }}</strong>
                      </div>
                    </div>
                  </button>
                </div>

                <div v-if="!mergeMode && !paintMode" class="flow-color-strip" :class="{ 'is-filtering': hiddenColorCount }">
                  <button
                    v-for="item in counts.slice(0, 12)"
                    :key="item.code"
                    class="flow-color-dot"
                    :class="{
                      'is-muted': isColorHidden(item.code),
                    }"
                    type="button"
                    :aria-label="isColorHidden(item.code) ? `显示 ${item.code}` : `隐藏 ${item.code}`"
                    :aria-pressed="isColorHidden(item.code)"
                    :style="{ background: item.hex }"
                    :title="isColorHidden(item.code) ? `${item.code} 已隐藏，点击显示` : `${item.code} · 点击隐藏`"
                    @click="toggleHiddenColor(item.code)"
                  ></button>
                </div>

                <div v-if="mergeMode || paintMode" class="flow-palette-toolbar" :class="{ 'is-merging': mergeMode, 'is-painting': paintMode }">
                  <div class="palette-toolbar-copy">
                    <span>{{ mergeToolbarText }}</span>
                    <small v-if="mergeMode">
                      {{ mergeTargetItem ? `保留 ${mergeTargetItem.code}，合并其余选中颜色` : "选择 2 个以上相近色" }}
                    </small>
                    <small v-else-if="paintMode">框选区域，可补豆点</small>
                  </div>
                </div>

                <div class="flow-palette-list" :class="{ 'is-merge-list': mergeMode, 'is-paint-list': paintMode }">
                  <button
                    v-for="item in counts"
                    :key="item.code"
                    class="palette-item palette-item-button"
                    :class="{
                      'is-muted': isColorHidden(item.code),
                      'is-merge-mode': mergeMode,
                      'is-merge-selected': isMergeSelected(item.code),
                      'is-merge-target': mergeMode && mergeTargetItem?.code === item.code,
                      'is-paint-mode': paintMode,
                      'is-paint-selected': isPaintSelected(item.code),
                    }"
                    type="button"
                    :aria-label="mergeMode ? `选择 ${item.code} 参与合并` : paintMode ? `使用 ${item.code} 改豆点` : isColorHidden(item.code) ? `显示 ${item.code}` : `隐藏 ${item.code}`"
                    :aria-pressed="mergeMode ? isMergeSelected(item.code) : paintMode ? isPaintSelected(item.code) : isColorHidden(item.code)"
                    :title="mergeMode ? `${item.code} · 点击选择或取消` : paintMode ? `${item.code} · 点击设为改色` : isColorHidden(item.code) ? `${item.code} 已隐藏，点击显示` : `${item.code} · 点击隐藏`"
                    @click="handlePaletteClick(item.code)"
                  >
                    <span class="palette-swatch" :style="{ background: item.hex }"></span>
                    <div class="palette-copy">
                      <strong>{{ item.code }}</strong>
                      <small>{{ item.name }}</small>
                    </div>
                    <span class="palette-count">{{ item.count }}</span>
                  </button>
                </div>

                <div v-if="mergeMode" class="merge-action-bar">
                  <div>
                    <span>{{ mergeSelectedCodes.length ? `已选 ${mergeSelectedCodes.length} 色` : "点选列表里的相近颜色" }}</span>
                    <strong v-if="mergeSourceCodes.length">
                      保留 {{ mergeTargetItem.code }}，合并 {{ mergeSourceCodes.length }} 色
                    </strong>
                    <strong v-else>再选一个相近色</strong>
                  </div>
                  <button
                    type="button"
                    :disabled="!canConfirmMerge"
                    @click="confirmMergeColor"
                  >
                    合并
                  </button>
                </div>

                <div v-if="!mergeMode && !paintMode" class="bead-style-card">
                  <div class="bead-style-head">
                    <span class="bead-style-title">预览样式</span>
                    <small>{{ roundBeads ? "圆豆预览" : "方格图纸" }}</small>
                  </div>
                  <div class="bead-style-segment" role="group" aria-label="预览样式">
                    <button
                      class="bead-style-option bead-style-option-square"
                      :class="{ 'is-active': !roundBeads }"
                      type="button"
                      @click="$emit('update:roundBeads', false)"
                    >
                      <span class="bead-style-icon" aria-hidden="true"></span>
                      <span>方格</span>
                    </button>
                    <button
                      class="bead-style-option bead-style-option-round"
                      :class="{ 'is-active': roundBeads }"
                      type="button"
                      @click="$emit('update:roundBeads', true)"
                    >
                      <span class="bead-style-icon" aria-hidden="true"></span>
                      <span>圆豆</span>
                    </button>
                  </div>
                </div>

                <div class="flow-result-actions">
                  <button
                    class="action-button action-button-ghost action-button-wide action-button-tune"
                    type="button"
                    @click="$emit('previous-step')"
                  >
                    调整参数
                  </button>
                  <button
                    class="action-button action-button-primary action-button-wide"
                    type="button"
                    :disabled="!hasResult"
                    @click="$emit('export-png')"
                  >
                    {{ actionState === "png" ? "正在准备..." : "下载图纸" }}
                  </button>
                </div>
              </aside>
            </div>
          </div>

          <div v-if="currentStep !== 'result'" class="flow-dialog-actions">
            <button
              class="action-button action-button-ghost"
              type="button"
              :disabled="currentStep === 'upload'"
              @click="$emit('previous-step')"
            >
              上一步
            </button>
            <button
              v-if="currentStep !== 'result'"
              class="action-button action-button-primary"
              type="button"
              :disabled="currentStep === 'upload' && !width"
              @click="$emit('next-step')"
            >
              {{ currentStep === "upload" ? "继续选择" : processing ? "正在更新..." : "查看结果" }}
            </button>
            <button
              v-else
              class="action-button action-button-primary"
              type="button"
              @click="$emit('finish')"
            >
              完成，回到页面
            </button>
          </div>
        </section>
      </div>
    </Transition>
  </Teleport>
</template>
