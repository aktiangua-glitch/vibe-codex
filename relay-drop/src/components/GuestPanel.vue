<script setup lang="ts">
import BrandChip from './BrandChip.vue'
import type { TransferMetaFormatter, TransferRecord } from '../types/relay'

defineProps<{
  guestHeadline: string
  guestSubHeadline: string
  guestPageDescription: string
  guestPrimaryDescription: string
  sessionNotice: string
  currentDeviceName: string
  remoteDeviceName: string
  connectionBadge: string
  isConnected: boolean
  guestOutgoingTransfers: TransferRecord[]
  guestIncomingTransfers: TransferRecord[]
  formatTransferMeta: TransferMetaFormatter
}>()

const emit = defineEmits<{
  'trigger-file': []
  download: [record: TransferRecord]
}>()
</script>

<template>
  <div class="sender-shell">
    <section class="sender-panel">
      <BrandChip />

      <div class="sender-copy">
        <p class="eyebrow">双向互传会话</p>
        <h1>
          <span>{{ guestHeadline }}</span>
          <span class="headline-secondary">{{ guestSubHeadline }}</span>
        </h1>
        <p class="hero-text sender-text">{{ guestPageDescription }}</p>
      </div>

      <div class="sender-toolbar">
        <button
          type="button"
          class="button button-primary button-hero"
          @click="emit('trigger-file')"
        >
          <span class="button-copy">
            <strong>选择文件发送</strong>
            <small>{{ guestPrimaryDescription }}</small>
          </span>
          <span class="button-badge">发送</span>
        </button>

        <p class="sender-note">{{ sessionNotice }}</p>

        <div class="hero-points">
          <span>{{ currentDeviceName }}</span>
          <span>{{ isConnected ? remoteDeviceName : '正在接入会话' }}</span>
          <span>{{ connectionBadge }}</span>
        </div>
      </div>

      <div class="sender-stage-grid">
        <article class="sender-stage-card">
          <div class="sender-stage-head">
            <div>
              <p class="device-kicker">你这边发出的文件</p>
              <h3>{{ currentDeviceName }}</h3>
            </div>
            <button
              type="button"
              class="device-chip device-chip-action"
              @click="emit('trigger-file')"
            >
              发文件
            </button>
          </div>

          <div class="sender-stage">
            <ul v-if="guestOutgoingTransfers.length" class="file-list sender-list">
              <li
                v-for="record in guestOutgoingTransfers"
                :key="`guest-send-${record.key}`"
              >
                <span class="file-main">
                  <span
                    class="file-dot"
                    :class="{
                      'file-dot-active': record.status === 'sending',
                      'file-dot-done': record.status === 'complete',
                    }"
                  />
                  <span class="file-name" :title="record.name">{{ record.label }}</span>
                </span>
                <small>{{ formatTransferMeta(record, 'sender') }}</small>
              </li>
            </ul>

            <div v-else class="sender-placeholder">
              <strong>连上了，现在可以直接发给对方。</strong>
              <small>点上面的按钮选文件，手机会立刻收到。</small>
            </div>
          </div>
        </article>

        <article class="sender-stage-card">
          <div class="sender-stage-head">
            <div>
              <p class="device-kicker">对方发过来的文件</p>
              <h3>{{ remoteDeviceName }}</h3>
            </div>
            <span class="device-chip">{{ isConnected ? '在线' : '重连中' }}</span>
          </div>

          <div class="sender-stage">
            <ul
              v-if="guestIncomingTransfers.length"
              class="file-list file-list-receiver"
            >
              <li
                v-for="(record, index) in guestIncomingTransfers"
                :key="`guest-receive-${record.key}`"
                :style="{ '--item-delay': `${index * 120}ms` }"
              >
                <span class="file-main">
                  <span
                    class="file-dot"
                    :class="{
                      'file-dot-active': record.status === 'receiving',
                      'file-dot-done': record.status === 'complete',
                    }"
                  />
                  <span class="file-name" :title="record.name">{{ record.label }}</span>
                </span>
                <button
                  v-if="record.downloadUrl"
                  type="button"
                  class="file-action"
                  @click="emit('download', record)"
                >
                  下载
                </button>
                <small v-else>{{ formatTransferMeta(record, 'receiver') }}</small>
              </li>
            </ul>

            <div v-else class="sender-placeholder">
              <strong>连上以后，对方也能马上把文件回传给你。</strong>
              <small>收到的文件会直接出现在这里。</small>
            </div>
          </div>
        </article>
      </div>
    </section>
  </div>
</template>
