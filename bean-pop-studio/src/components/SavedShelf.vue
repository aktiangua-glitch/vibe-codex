<script setup>
defineProps({
  compact: {
    type: Boolean,
    default: false,
  },
  presets: {
    type: Object,
    required: true,
  },
  projects: {
    type: Array,
    required: true,
  },
});

defineEmits(["delete-project", "open-project"]);

function formatDate(value) {
  if (!value) return "刚刚保存";
  return new Intl.DateTimeFormat("zh-CN", {
    month: "numeric",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(value));
}
</script>

<template>
  <section class="shelf-panel" :class="{ 'is-compact': compact }" id="shelf">
    <div class="section-heading">
      <p class="eyebrow">作品夹</p>
      <h2>喜欢的图纸都在这里。</h2>
      <p>多试几张照片，先把有感觉的存下来。之后想改取景、颜色或重新下载，都可以继续。</p>
    </div>

    <div v-if="projects.length" class="shelf-grid">
      <article v-for="project in compact ? projects.slice(0, 2) : projects" :key="project.id" class="shelf-card">
        <img class="shelf-thumb" :src="project.previewDataUrl" :alt="`${project.title} 缩略图`" />
        <div class="shelf-copy">
          <div class="shelf-topline">
            <span class="mini-tag">{{ presets[project.preset]?.label || "作品" }}</span>
            <small>{{ formatDate(project.updatedAt) }}</small>
          </div>
          <h3>{{ project.title }}</h3>
          <p>{{ project.brand }} · {{ project.width }} × {{ project.height }} · {{ project.counts.length }} 色</p>
        </div>
        <div class="shelf-actions">
          <button class="action-button action-button-ghost" type="button" @click="$emit('open-project', project.id)">
            继续修改
          </button>
          <button class="action-button action-button-ghost is-danger" type="button" @click="$emit('delete-project', project.id)">
            删除
          </button>
        </div>
      </article>
    </div>

    <div v-else class="empty-shelf">
      <strong>还没有保存的图纸。</strong>
      <p>{{ compact ? "保存后会出现在这里。" : "做出满意的图纸后点保存，这里会留下它。" }}</p>
    </div>
  </section>
</template>
