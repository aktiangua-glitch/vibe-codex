<script setup>
defineProps({
  aspectLabel: {
    type: String,
    required: true,
  },
  brand: {
    type: String,
    required: true,
  },
  brandMeta: {
    type: Object,
    required: true,
  },
  maxColors: {
    type: Number,
    required: true,
  },
  preset: {
    type: String,
    required: true,
  },
  presets: {
    type: Object,
    required: true,
  },
  processing: {
    type: Boolean,
    default: false,
  },
  roundBeads: {
    type: Boolean,
    required: true,
  },
  showCodes: {
    type: Boolean,
    required: true,
  },
  targetWidth: {
    type: Number,
    required: true,
  },
});

const emit = defineEmits([
  "generate",
  "update:brand",
  "update:maxColors",
  "update:preset",
  "update:roundBeads",
  "update:showCodes",
  "update:targetWidth",
]);
</script>

<template>
  <aside class="studio-card studio-card-controls">
    <div class="card-topline">
      <span class="card-kicker">图纸设置</span>
      <strong>定尺寸、颜色和豆子品牌</strong>
    </div>

    <div class="field-block">
      <span class="field-label">你想做成哪种作品</span>
      <div class="preset-grid">
        <button
          v-for="(item, key) in presets"
          :key="key"
          class="preset-tile"
          :class="{ 'is-active': preset === key }"
          type="button"
          @click="$emit('update:preset', key)"
        >
          <strong>{{ item.label }}</strong>
          <small>{{ item.subtitle }}</small>
        </button>
      </div>
    </div>

    <label class="field-block">
      <span class="field-label">成品做多大</span>
      <input
        class="range-input"
        :value="targetWidth"
        type="range"
        min="24"
        max="96"
        step="1"
        @input="$emit('update:targetWidth', Number($event.target.value))"
      />
      <div class="field-inline">
        <strong>{{ targetWidth }} 格宽</strong>
        <small>{{ aspectLabel }}</small>
      </div>
    </label>

    <div class="field-block">
      <span class="field-label">选择豆子品牌</span>
      <div class="choice-stack">
        <button
          v-for="(meta, key) in brandMeta"
          :key="key"
          class="choice-pill"
          :class="{ 'is-active': brand === key }"
          type="button"
          @click="$emit('update:brand', key)"
        >
          <span>{{ key }}</span>
          <small>{{ meta.note }}</small>
        </button>
      </div>
    </div>

    <div class="field-block">
      <span class="field-label">颜色控制在多少种</span>
      <div class="choice-row">
        <button
          v-for="count in [12, 18, 24]"
          :key="count"
          class="chip-button"
          :class="{ 'is-active': maxColors === count }"
          type="button"
          @click="$emit('update:maxColors', count)"
        >
          {{ count }} 色
        </button>
      </div>
    </div>

    <div class="toggle-grid">
      <label class="toggle-pill">
        <input
          :checked="showCodes"
          type="checkbox"
          @change="$emit('update:showCodes', $event.target.checked)"
        />
        <span>色号</span>
      </label>
      <label class="toggle-pill">
        <input
          :checked="roundBeads"
          type="checkbox"
          @change="$emit('update:roundBeads', $event.target.checked)"
        />
        <span>圆豆</span>
      </label>
    </div>

    <button class="action-button action-button-dark action-button-wide" type="button" @click="$emit('generate')">
      {{ processing ? "正在更新..." : "更新拼豆效果" }}
    </button>
  </aside>
</template>
