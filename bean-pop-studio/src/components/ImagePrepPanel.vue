<script setup>
import { ref } from "vue";

defineProps({
  cropRatio: {
    type: String,
    required: true,
  },
  framing: {
    type: String,
    required: true,
  },
  imageLoaded: {
    type: Boolean,
    required: true,
  },
  prepHint: {
    type: String,
    required: true,
  },
  projectName: {
    type: String,
    required: true,
  },
  ratios: {
    type: Object,
    required: true,
  },
  styleIntensity: {
    type: Number,
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
  subjectConfidence: {
    type: Number,
    required: true,
  },
  zoomAdjust: {
    type: Number,
    required: true,
  },
  offsetX: {
    type: Number,
    required: true,
  },
  offsetY: {
    type: Number,
    required: true,
  },
});

const emit = defineEmits([
  "file-selected",
  "load-sample",
  "reset-focus",
  "update:cropRatio",
  "update:framing",
  "update:offsetX",
  "update:offsetY",
  "update:projectName",
  "update:styleIntensity",
  "update:styleMode",
  "update:zoomAdjust",
]);

const fileInput = ref(null);
const prepCanvas = ref(null);
const isDragOver = ref(false);

function openPicker() {
  fileInput.value?.click();
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

function getCanvas() {
  return prepCanvas.value;
}

defineExpose({ getCanvas });
</script>

<template>
  <section class="studio-card studio-card-controls prep-card">
    <div class="card-topline">
      <span class="card-kicker">照片和画面</span>
      <strong>换照片，或让主体更好看</strong>
    </div>

    <div
      class="upload-zone"
      :class="{ 'is-dragover': isDragOver }"
      tabindex="0"
      role="button"
      aria-label="上传照片"
      @click="openPicker"
      @dragenter.prevent="isDragOver = true"
      @dragover.prevent="isDragOver = true"
      @dragleave.prevent="isDragOver = false"
      @dragend.prevent="isDragOver = false"
      @drop.prevent="handleDrop"
    >
      <input
        ref="fileInput"
        class="sr-only"
        type="file"
        accept="image/*"
        @change="handleChange"
      />
      <span class="upload-label">立即开始</span>
      <h3>把照片拖进来，马上变图纸</h3>
      <p>先自动出一版拼豆效果。满意后，再微调尺寸、颜色和画面取景。</p>
      <div class="upload-actions">
        <button class="action-button action-button-primary" type="button" @click.stop="openPicker">
          上传照片
        </button>
      </div>
    </div>

    <label class="field-block">
      <span class="field-label">给这张图纸起个名</span>
      <input
        class="text-input"
        :value="projectName"
        type="text"
        maxlength="32"
        placeholder="例如：Lucky 的拼豆小像"
        @input="$emit('update:projectName', $event.target.value)"
      />
    </label>

    <div class="prep-preview-shell">
      <div class="prep-preview-head">
        <div>
          <strong>自动取好的画面</strong>
          <small>{{ prepHint }}</small>
        </div>
        <span class="mini-tag">{{ subjectConfidence >= 0.48 ? "主体清晰" : "可手动调整" }}</span>
      </div>
      <canvas ref="prepCanvas" class="prep-canvas"></canvas>
    </div>

    <div class="field-block">
      <div class="field-titleline">
        <span class="field-label">画面比例</span>
        <button class="inline-reset" type="button" :disabled="!imageLoaded" @click="$emit('reset-focus')">
          自动居中
        </button>
      </div>
      <div class="choice-row choice-row-wrap">
        <button
          v-for="(ratio, key) in ratios"
          :key="key"
          class="chip-button"
          :class="{ 'is-active': cropRatio === key }"
          type="button"
          :disabled="!imageLoaded"
          @click="$emit('update:cropRatio', key)"
        >
          {{ ratio.label }}
        </button>
      </div>
    </div>

    <details class="advanced-panel">
      <summary>
        <span>想让主体更好看？</span>
        <small>不调也可以直接用</small>
      </summary>

      <div class="field-block">
        <span class="field-label">主体大小</span>
        <div class="choice-row choice-row-wrap">
          <button
            v-for="(item, key) in { airy: '留白', balanced: '平衡', tight: '靠近主体' }"
            :key="key"
            class="chip-button"
            :class="{ 'is-active': framing === key }"
            type="button"
            :disabled="!imageLoaded"
            @click="$emit('update:framing', key)"
          >
            {{ item }}
          </button>
        </div>
      </div>

      <div class="field-block">
        <span class="field-label">移动画面</span>
        <div class="slider-stack">
          <label class="slider-field">
            <span>放大缩小</span>
            <input
              class="range-input"
              :value="zoomAdjust"
              type="range"
              min="0.85"
              max="1.8"
              step="0.01"
              :disabled="!imageLoaded"
              @input="$emit('update:zoomAdjust', Number($event.target.value))"
            />
            <small>{{ zoomAdjust.toFixed(2) }}x</small>
          </label>
          <label class="slider-field">
            <span>左右位置</span>
            <input
              class="range-input"
              :value="offsetX"
              type="range"
              min="-1"
              max="1"
              step="0.01"
              :disabled="!imageLoaded"
              @input="$emit('update:offsetX', Number($event.target.value))"
            />
            <small>{{ offsetX.toFixed(2) }}</small>
          </label>
          <label class="slider-field">
            <span>上下位置</span>
            <input
              class="range-input"
              :value="offsetY"
              type="range"
              min="-1"
              max="1"
              step="0.01"
              :disabled="!imageLoaded"
              @input="$emit('update:offsetY', Number($event.target.value))"
            />
            <small>{{ offsetY.toFixed(2) }}</small>
          </label>
        </div>
      </div>
    </details>

    <div class="field-block">
      <div class="field-titleline">
        <span class="field-label">选择成品风格</span>
        <small class="field-note">影响线条、颜色和最后的气质</small>
      </div>
      <div class="style-grid">
        <button
          v-for="(style, key) in stylePresets"
          :key="key"
          class="style-tile"
          :class="{ 'is-active': styleMode === key }"
          type="button"
          :disabled="!imageLoaded"
          @click="$emit('update:styleMode', key)"
        >
          <div class="style-tile-head">
            <strong>{{ style.label }}</strong>
            <span>{{ style.tag }}</span>
          </div>
          <small>{{ style.description }}</small>
        </button>
      </div>
      <label class="slider-field">
        <span>风格明显一点</span>
        <input
          class="range-input"
          :value="styleIntensity"
          type="range"
          min="0"
          max="1"
          step="0.01"
          :disabled="!imageLoaded || styleMode === 'none'"
          @input="$emit('update:styleIntensity', Number($event.target.value))"
        />
        <small>{{ Math.round(styleIntensity * 100) }}%</small>
      </label>
    </div>
  </section>
</template>
