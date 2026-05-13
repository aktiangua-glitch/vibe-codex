<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { createSavedPreview, processImage } from "../lib/beadEngine.js";
import { communityCategories, communitySeeds } from "../data/communitySeeds.js";
import { buildPreparedSource } from "../lib/imagePrep.js";
import { filterCommunitySeeds } from "../lib/communityShowcase.js";
import { createCommunitySourceDataUrl, getCommunitySourceSubjectBox } from "../lib/communitySources.js";

const emit = defineEmits(["launch-seed"]);

const activeCategory = ref("全部");
const previewCache = reactive(Object.fromEntries(
  communitySeeds.map((seed) => [seed.id, { status: "idle" }])
));

const displaySeeds = computed(() => filterCommunitySeeds(communitySeeds, activeCategory.value));

const sourceDataUrlCache = new Map();
const sourceImageCache = new Map();

let warmupTimer = 0;
let idleHandle = 0;
let stopped = false;

function sumBeads(counts) {
  return counts.reduce((sum, item) => sum + item.count, 0);
}

function createImageFromDataUrl(dataUrl) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = reject;
    image.src = dataUrl;
  });
}

function getSourceDataUrl(sourceKind) {
  if (!sourceDataUrlCache.has(sourceKind)) {
    sourceDataUrlCache.set(sourceKind, createCommunitySourceDataUrl(sourceKind));
  }
  return sourceDataUrlCache.get(sourceKind);
}

async function getSourceImage(sourceKind) {
  if (!sourceImageCache.has(sourceKind)) {
    sourceImageCache.set(sourceKind, createImageFromDataUrl(getSourceDataUrl(sourceKind)));
  }
  return sourceImageCache.get(sourceKind);
}

function createPatternPreview({ matrix, width, height }) {
  const canvas = document.createElement("canvas");
  canvas.width = 280;
  canvas.height = 220;
  const context = canvas.getContext("2d");
  const paddingX = 26;
  const paddingTop = 38;
  const paddingBottom = 24;
  const cell = Math.max(
    4,
    Math.min(
      Math.floor((canvas.width - paddingX * 2) / Math.max(1, width)),
      Math.floor((canvas.height - paddingTop - paddingBottom) / Math.max(1, height))
    )
  );
  const artworkWidth = width * cell;
  const artworkHeight = height * cell;
  const offsetX = Math.floor((canvas.width - artworkWidth) / 2);
  const offsetY = Math.floor((canvas.height - paddingBottom - artworkHeight + paddingTop) / 2);

  const wash = context.createLinearGradient(0, 0, canvas.width, canvas.height);
  wash.addColorStop(0, "#fcfaf5");
  wash.addColorStop(1, "#eef4ff");
  context.fillStyle = wash;
  context.fillRect(0, 0, canvas.width, canvas.height);

  context.fillStyle = "rgba(23, 26, 31, 0.72)";
  context.font = '700 13px "IBM Plex Mono", monospace';
  context.fillText("PATTERN", 20, 22);

  context.fillStyle = "#ffffff";
  context.fillRect(offsetX - 8, offsetY - 8, artworkWidth + 16, artworkHeight + 16);
  context.strokeStyle = "rgba(29, 24, 19, 0.08)";
  context.strokeRect(offsetX - 8, offsetY - 8, artworkWidth + 16, artworkHeight + 16);

  matrix.forEach((row, rowIndex) => {
    row.forEach((color, columnIndex) => {
      const x = offsetX + columnIndex * cell;
      const y = offsetY + rowIndex * cell;
      context.fillStyle = color?.hex || "#ffffff";
      context.fillRect(x, y, cell, cell);
    });
  });

  context.strokeStyle = "rgba(255, 99, 61, 0.22)";
  context.lineWidth = 1;
  for (let columnIndex = 10; columnIndex < width; columnIndex += 10) {
    const x = offsetX + columnIndex * cell;
    context.beginPath();
    context.moveTo(x, offsetY);
    context.lineTo(x, offsetY + artworkHeight);
    context.stroke();
  }
  for (let rowIndex = 10; rowIndex < height; rowIndex += 10) {
    const y = offsetY + rowIndex * cell;
    context.beginPath();
    context.moveTo(offsetX, y);
    context.lineTo(offsetX + artworkWidth, y);
    context.stroke();
  }

  return canvas.toDataURL("image/png");
}

async function buildSeedPreview(seed) {
  const current = previewCache[seed.id];
  if (current?.status === "ready" || current?.status === "loading") return;

  previewCache[seed.id] = { status: "loading" };

  try {
    const sourceImage = await getSourceImage(seed.sourceKind);
    const prepared = buildPreparedSource(sourceImage, {
      cropRatio: seed.cropRatio,
      framing: seed.framing,
      offsetX: 0,
      offsetY: 0,
      styleIntensity: 0.72,
      styleMode: seed.styleMode,
      subjectBox: getCommunitySourceSubjectBox(seed.sourceKind),
      zoomAdjust: 1,
    });
    const result = processImage({
      source: prepared.canvas,
      brand: seed.brand,
      maxColors: seed.maxColors,
      targetWidth: seed.targetWidth,
      transparentBackground: false,
    });

    previewCache[seed.id] = {
      status: "ready",
      artworkDataUrl: createSavedPreview({
        matrix: result.matrix,
        width: result.width,
        height: result.height,
        roundBeads: true,
      }),
      patternDataUrl: createPatternPreview(result),
      width: result.width,
      height: result.height,
      colorCount: result.counts.length,
      totalBeads: sumBeads(result.counts),
    };
  } catch (error) {
    console.error(error);
    previewCache[seed.id] = { status: "error" };
  }
}

async function generateAllSeedPreviews() {
  for (const seed of communitySeeds) {
    if (stopped) return;
    await buildSeedPreview(seed);
    await new Promise((resolve) => window.setTimeout(resolve, 18));
  }
}

function schedulePreviewWarmup() {
  warmupTimer = window.setTimeout(() => {
    if (typeof window.requestIdleCallback === "function") {
      idleHandle = window.requestIdleCallback(() => {
        void generateAllSeedPreviews();
      }, { timeout: 1400 });
      return;
    }

    idleHandle = window.setTimeout(() => {
      void generateAllSeedPreviews();
    }, 180);
  }, 260);
}

function cancelWarmup() {
  window.clearTimeout(warmupTimer);

  if (typeof window.cancelIdleCallback === "function") {
    window.cancelIdleCallback(idleHandle);
  } else {
    window.clearTimeout(idleHandle);
  }
}

function setCategory(category) {
  activeCategory.value = category;
}

function launchUpload() {
  emit("launch-seed", {
    sourceKind: "upload",
    label: "我也做一张",
  });
}

function launchSeed(seed) {
  emit("launch-seed", {
    sourceKind: seed.sourceKind,
    label: seed.title,
    brand: seed.brand,
    targetWidth: seed.targetWidth,
    maxColors: seed.maxColors,
    cropRatio: seed.cropRatio,
    framing: seed.framing,
    styleMode: seed.styleMode,
  });
}

function getCardPreview(seed) {
  return previewCache[seed.id] || { status: "idle" };
}

function getSizeLabel(seed, preview) {
  if (preview.status === "ready") {
    return `${preview.width} × ${preview.height}`;
  }
  return `${seed.targetWidth} 目标边长`;
}

function getColorLabel(seed, preview) {
  if (preview.status === "ready") {
    return `${preview.colorCount} 色`;
  }
  return `${seed.maxColors} 色内`;
}

function getBeadLabel(preview) {
  if (preview.status === "ready") {
    return `${preview.totalBeads.toLocaleString("zh-CN")} 颗`;
  }
  return "示例图纸生成中";
}

onMounted(() => {
  schedulePreviewWarmup();
});

onBeforeUnmount(() => {
  stopped = true;
  cancelWarmup();
});
</script>

<template>
  <div class="community-shell">
    <section id="community" class="community-panel community-panel-gallery">
      <div class="community-gallery-head">
        <div class="section-heading community-heading">
          <h2>大家最近爱做这些题材。</h2>
          <p>按类型切换看看成品感、图纸规格和题材方向。顺眼的直接照着试，不顺手再回到上传入口自己做。</p>
        </div>

        <div class="community-head-actions">
          <button class="action-button action-button-primary" type="button" @click="launchUpload">
            我也做一张
          </button>
        </div>
      </div>

      <div class="community-filter-row" aria-label="作品分类筛选">
        <button
          v-for="category in communityCategories"
          :key="category"
          class="community-filter-chip"
          :class="{ 'is-active': activeCategory === category }"
          type="button"
          :aria-pressed="activeCategory === category"
          @click="setCategory(category)"
        >
          {{ category }}
        </button>
      </div>

      <div class="community-gallery-grid">
        <article
          v-for="seed in displaySeeds"
          :key="seed.id"
          class="community-card community-card-gallery"
        >
          <div class="community-card-media">
            <div class="community-media-main">
              <img
                v-if="getCardPreview(seed).status === 'ready'"
                class="community-media-image"
                :src="getCardPreview(seed).artworkDataUrl"
                :alt="`${seed.title} 成品预览`"
              />
              <div v-else-if="getCardPreview(seed).status === 'error'" class="community-media-fallback">
                <strong>[作品预览]</strong>
                <span>先照着参数试，生成后会看到完整成品。</span>
              </div>
              <div v-else class="community-media-skeleton"></div>
            </div>

            <div class="community-pattern-float">
              <img
                v-if="getCardPreview(seed).status === 'ready'"
                class="community-pattern-image"
                :src="getCardPreview(seed).patternDataUrl"
                :alt="`${seed.title} 图纸预览`"
              />
              <div v-else class="community-pattern-placeholder">
                <span>图纸</span>
              </div>
            </div>
          </div>

          <div class="community-card-body">
            <div class="community-card-topline">
              <span class="mini-tag">{{ seed.category }}</span>
              <small>{{ getBeadLabel(getCardPreview(seed)) }}</small>
            </div>
            <h3>{{ seed.title }}</h3>
            <p>{{ seed.note }}</p>
            <div class="community-meta-row">
              <span>{{ seed.brand }}</span>
              <span>{{ getSizeLabel(seed, getCardPreview(seed)) }}</span>
              <span>{{ getColorLabel(seed, getCardPreview(seed)) }}</span>
            </div>
            <button class="action-button action-button-ghost action-button-wide" type="button" @click="launchSeed(seed)">
              照着试试
            </button>
          </div>
        </article>
      </div>
    </section>
  </div>
</template>
