<script setup>
defineProps({
  actionState: {
    type: String,
    default: "",
  },
  brand: {
    type: String,
    required: true,
  },
  counts: {
    type: Array,
    required: true,
  },
  hasResult: {
    type: Boolean,
    required: true,
  },
  projectSubtitle: {
    type: String,
    required: true,
  },
  projectTitle: {
    type: String,
    required: true,
  },
  recommendation: {
    type: Object,
    required: true,
  },
  savedProjectsCount: {
    type: Number,
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
  isSaved: {
    type: Boolean,
    required: true,
  },
});

defineEmits(["export-png", "save-project"]);
</script>

<template>
  <aside class="studio-card studio-card-sidebar">
    <div class="card-topline">
      <span class="card-kicker">保存下载</span>
      <strong>满意就保存，开拼前下载</strong>
    </div>

    <div class="summary-hero">
      <div class="summary-headline">
        <p class="summary-kicker">这张图纸</p>
        <span class="mini-tag">{{ isSaved ? "已放入作品夹" : "还没保存" }}</span>
      </div>
      <h3>{{ projectTitle }}</h3>
      <p>{{ projectSubtitle }}</p>
    </div>

    <div class="stat-grid">
      <article class="stat-card">
        <span>尺寸</span>
        <strong>{{ width }} × {{ height }}</strong>
      </article>
      <article class="stat-card">
        <span>豆子颜色</span>
        <strong>{{ counts.length }} 色</strong>
      </article>
      <article class="stat-card">
        <span>预计颗数</span>
        <strong>{{ totalBeads.toLocaleString("zh-CN") }}</strong>
      </article>
      <article class="stat-card">
        <span>配色品牌</span>
        <strong>{{ brand }}</strong>
      </article>
    </div>

    <div class="recommend-card">
      <div class="recommend-head">
        <span>制作建议</span>
        <strong>{{ recommendation.title }}</strong>
      </div>
      <p>{{ recommendation.text }}</p>
      <div class="tag-row">
        <span v-for="tag in recommendation.tags" :key="tag" class="mini-tag">{{ tag }}</span>
      </div>
      <div class="workload-banner">
        <strong>{{ workload.label }}</strong>
        <small>{{ workload.note }}</small>
      </div>
    </div>

    <div class="sidebar-actions sidebar-actions-prime">
      <button
        class="action-button action-button-primary action-button-wide"
        type="button"
        :disabled="!hasResult"
        @click="$emit('save-project')"
      >
        {{ actionState === "save" ? "正在保存..." : isSaved ? "更新图纸" : "保存图纸" }}
      </button>
      <button
        class="action-button action-button-ghost action-button-wide"
        type="button"
        :disabled="!hasResult"
        @click="$emit('export-png')"
      >
        {{ actionState === "png" ? "正在准备图片..." : "下载可打印图纸" }}
      </button>
    </div>

    <div class="palette-section">
      <div class="section-mini-head">
        <strong>色号清单</strong>
        <span>{{ counts.length }} 色</span>
      </div>

      <div v-if="counts.length" class="palette-list">
        <div v-for="item in counts" :key="item.code" class="palette-item">
          <span class="palette-swatch" :style="{ background: item.hex }"></span>
          <div class="palette-copy">
            <strong>{{ item.code }}</strong>
            <small>{{ item.name }}</small>
          </div>
          <span class="palette-count">{{ item.count }}</span>
        </div>
      </div>

      <div v-else class="empty-inline">
        先上传一张照片，色号和颗数会出现在这里。
      </div>
    </div>

    <div class="save-state-card">
      <strong>{{ isSaved ? "这张图纸已经保存" : "建议先保存这张图纸" }}</strong>
      <p>
        {{
          isSaved
            ? `作品夹里已有 ${savedProjectsCount} 张图纸，可以随时回来继续改取景、颜色和色号。`
            : "保存后会记住取景、颜色和色号，下次回来可以直接继续改。"
        }}
      </p>
    </div>
  </aside>
</template>
