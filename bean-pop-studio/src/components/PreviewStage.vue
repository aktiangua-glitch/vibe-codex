<script setup>
import { ref } from "vue";

defineProps({
  brand: {
    type: String,
    required: true,
  },
  countsLength: {
    type: Number,
    required: true,
  },
  imageLabel: {
    type: String,
    required: true,
  },
  preparedLabel: {
    type: String,
    required: true,
  },
  presetLabel: {
    type: String,
    required: true,
  },
  processing: {
    type: Boolean,
    default: false,
  },
  projectPulse: {
    type: Object,
    required: true,
  },
  projectTitle: {
    type: String,
    required: true,
  },
  stageHint: {
    type: String,
    required: true,
  },
  stageView: {
    type: String,
    required: true,
  },
  totalBeads: {
    type: Number,
    required: true,
  },
  workload: {
    type: Object,
    required: true,
  },
  width: {
    type: Number,
    required: true,
  },
  height: {
    type: Number,
    required: true,
  },
});

defineEmits(["open-flow", "update:stageView"]);

const originalCanvas = ref(null);
const preparedCanvas = ref(null);
const resultCanvas = ref(null);
const patternCanvas = ref(null);

function getCanvases() {
  return {
    original: originalCanvas.value,
    prepared: preparedCanvas.value,
    result: resultCanvas.value,
    pattern: patternCanvas.value,
  };
}

defineExpose({ getCanvases });
</script>

<template>
  <section class="studio-card studio-card-stage">
    <div class="stage-result-strip">
      <div class="stage-current-copy">
        <span>当前结果</span>
        <strong>{{ projectTitle }}</strong>
        <small>{{ projectPulse.note }}</small>
      </div>
      <div class="stage-simple-metrics">
        <article>
          <span>成品大小</span>
          <strong>{{ width }} × {{ height }}</strong>
        </article>
        <article>
          <span>用色</span>
          <strong>{{ countsLength }} 色</strong>
        </article>
        <article>
          <span>豆子数</span>
          <strong>{{ totalBeads.toLocaleString("zh-CN") }}</strong>
        </article>
      </div>
      <button class="action-button action-button-ghost" type="button" @click="$emit('open-flow')">
        换照片或微调
      </button>
    </div>

    <div class="stage-toolbar">
      <div class="stage-switch">
        <button
          class="stage-button"
          :class="{ 'is-active': stageView === 'original' }"
          type="button"
          @click="$emit('update:stageView', 'original')"
        >
          照片
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
          拼豆效果
        </button>
        <button
          class="stage-button"
          :class="{ 'is-active': stageView === 'sheet' }"
          type="button"
          @click="$emit('update:stageView', 'sheet')"
        >
          图纸
        </button>
      </div>
      <p class="stage-hint">{{ stageHint }}</p>
      <div class="tag-row stage-chip-row">
        <span class="mini-tag">{{ preparedLabel }}</span>
        <span class="mini-tag">{{ brand }} 配色</span>
        <span class="mini-tag">{{ countsLength }} 色</span>
      </div>
    </div>

    <div class="stage-board">
      <div v-if="processing" class="stage-processing">
        <strong>正在更新拼豆效果</strong>
        <span>马上看到新的成品感和图纸。</span>
      </div>

      <article v-show="stageView === 'original'" class="stage-pane">
        <div class="canvas-caption">
          <span>原照片</span>
          <strong>{{ imageLabel }}</strong>
        </div>
        <canvas ref="originalCanvas" class="canvas-surface"></canvas>
      </article>

      <article v-show="stageView === 'prepared'" class="stage-pane">
        <div class="canvas-caption">
          <span>取景画面</span>
          <strong>{{ preparedLabel }}</strong>
        </div>
        <canvas ref="preparedCanvas" class="canvas-surface"></canvas>
      </article>

      <article v-show="stageView === 'result'" class="stage-pane">
        <div class="canvas-caption">
          <span>拼豆效果</span>
          <strong>{{ brand }} · {{ countsLength }} 色</strong>
        </div>
        <canvas ref="resultCanvas" class="canvas-surface"></canvas>
      </article>

      <article v-show="stageView === 'sheet'" class="stage-pane">
        <div class="canvas-caption">
          <span>制作图纸</span>
          <strong>{{ width }} × {{ height }} · {{ totalBeads.toLocaleString("zh-CN") }} 颗</strong>
        </div>
        <canvas ref="patternCanvas" class="canvas-sheet"></canvas>
      </article>
    </div>
  </section>
</template>
