<script setup lang="ts">
import BrandChip from './BrandChip.vue'

defineProps<{
  heroPrimaryTitle: string
  heroPrimaryDescription: string
  heroPrimaryBadge: string
  heroFlow: string[]
  heroHint: string
  heroChips: string[]
}>()

const emit = defineEmits<{
  primary: []
}>()
</script>

<template>
  <section class="hero-panel">
    <BrandChip />

    <div class="hero-panel-body">
      <div class="hero-copy">
        <p class="eyebrow">跨平台文件互传</p>
        <h1><span>扫码就能传</span></h1>
        <p class="hero-text">电脑亮码，对方一扫就进来。连上后，双方都能直接发。</p>
      </div>

      <div class="hero-actions">
        <button
          type="button"
          class="button button-primary button-hero"
          @click="emit('primary')"
        >
          <span class="button-copy">
            <strong>{{ heroPrimaryTitle }}</strong>
            <small>{{ heroPrimaryDescription }}</small>
          </span>
          <span class="button-badge">{{ heroPrimaryBadge }}</span>
        </button>

        <div class="hero-flow-card">
          <div class="hero-flow">
            <div
              v-for="(step, index) in heroFlow"
              :key="step"
              class="hero-flow-item"
            >
              <small>{{ `0${index + 1}` }}</small>
              <strong>{{ step }}</strong>
            </div>
          </div>

          <p class="hero-note">
            <span class="hero-note-dot" aria-hidden="true" />
            <span>{{ heroHint }}</span>
          </p>
        </div>

        <div class="hero-points">
          <span v-for="chip in heroChips" :key="chip">{{ chip }}</span>
        </div>
      </div>
    </div>
  </section>
</template>
