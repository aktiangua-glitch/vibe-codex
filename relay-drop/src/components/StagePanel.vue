<script setup lang="ts">
import type {
  SessionState,
  TransferMetaFormatter,
  TransferRecord,
} from '../types/relay'

defineProps<{
  stageEyebrow: string
  stageTitle: string
  stageDescription: string
  isHost: boolean
  isConnected: boolean
  isLiveSession: boolean
  hasIncomingLiveTransfers: boolean
  qrCodeDataUrl: string
  currentTransfer?: TransferRecord
  sessionState: SessionState
  transferSpeedLabel: string
  statusHeadline: string
  transferProgress: number
  beamTransfers: TransferRecord[]
  currentDeviceName: string
  remoteDeviceName: string
  remoteDeviceRole: string
  senderStageTransfers: TransferRecord[]
  receiverStageTransfers: TransferRecord[]
  formatTransferMeta: TransferMetaFormatter
}>()

const emit = defineEmits<{
  'open-qr': []
  'trigger-file': []
  download: [record: TransferRecord]
}>()
</script>

<template>
  <section class="stage-panel">
    <div class="stage-bar">
      <div class="stage-bar-copy">
        <p class="eyebrow stage-eyebrow">{{ stageEyebrow }}</p>
        <h2>{{ stageTitle }}</h2>
      </div>

      <button
        v-if="isHost"
        type="button"
        class="stage-qr-trigger"
        @click="emit('open-qr')"
      >
        <span class="stage-qr-icon" aria-hidden="true" />
        <span>会话二维码</span>

        <div v-if="qrCodeDataUrl" class="stage-qr-popover">
          <div class="stage-qr-preview">
            <img
              :src="qrCodeDataUrl"
              alt="扫码接收二维码"
              class="stage-qr-image"
              loading="eager"
              decoding="sync"
              fetchpriority="high"
            />
          </div>
          <p>扫一扫，直接加入同一条会话</p>
        </div>
      </button>

      <span v-else class="stage-guest-chip">
        {{ isConnected ? '会话已连上' : '自动重连中' }}
      </span>
    </div>

    <p class="stage-text">{{ stageDescription }}</p>

    <div class="scene">
      <div class="beam-shell" :class="{ 'beam-shell-live': isLiveSession }">
        <div class="beam-grid" />
        <div class="beam-line" />

        <svg class="beam-lanes" viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
          <path class="beam-lane-track" d="M18 68 C30 65 42 61 54 53 C62 47 68 44 73 43" />
          <path class="beam-lane-track" d="M18.4 78 C31 74 44 68 57 60 C65 57 70 55.5 74 55" />
          <path class="beam-lane-track" d="M18.8 88 C32 83 46 77 60 69 C67 67 71 66.5 75 66" />
          <path class="beam-lane-flow" d="M18 68 C30 65 42 61 54 53 C62 47 68 44 73 43" />
          <path class="beam-lane-flow" d="M18.4 78 C31 74 44 68 57 60 C65 57 70 55.5 74 55" />
          <path class="beam-lane-flow" d="M18.8 88 C32 83 46 77 60 69 C67 67 71 66.5 75 66" />
          <circle class="beam-lane-dot" cx="73" cy="43" r="1.9" />
          <circle class="beam-lane-dot" cx="74" cy="55" r="1.9" />
          <circle class="beam-lane-dot" cx="75" cy="66" r="1.9" />
        </svg>

        <div class="beam-glow" />

        <div class="beam-anchor" aria-hidden="true">
          <span class="beam-anchor-ring" />
          <span class="beam-anchor-core" />
        </div>

        <div v-if="isLiveSession" class="beam-footer">
          <div class="beam-footer-row">
            <span class="beam-footer-pill">{{ currentTransfer?.label ?? '文件传输中' }}</span>
            <small v-if="sessionState === 'done'">已完成</small>
            <small v-else>{{ transferSpeedLabel || statusHeadline }}</small>
          </div>

          <div class="beam-progress">
            <span :style="{ width: `${transferProgress}%` }" />
          </div>
        </div>
      </div>

      <div class="beam-flight-layer" aria-hidden="true">
        <span
          v-for="(record, index) in beamTransfers"
          :key="`${record.key}-${index}`"
          class="beam-file"
          :class="{ 'beam-file-reverse': record.direction === 'incoming' }"
          :style="{ '--delay': `${index * 0.96}s`, '--row': `${index}` }"
        >
          {{ record.label }}
        </span>
      </div>

      <article class="device-card device-card-sender">
        <div class="device-head">
          <div>
            <p class="device-kicker">你这边</p>
            <h3>{{ currentDeviceName }}</h3>
          </div>
          <button
            type="button"
            class="device-chip device-chip-action"
            :disabled="!isConnected"
            @click="emit('trigger-file')"
          >
            {{ isConnected ? '发送文件' : '等待连上' }}
          </button>
        </div>

        <div class="device-screen">
          <ul class="file-list">
            <li
              v-for="record in senderStageTransfers"
              :key="`send-${record.key}`"
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
        </div>
      </article>

      <article class="device-card device-card-receiver">
        <div class="device-head">
          <div>
            <p class="device-kicker">对方那边</p>
            <h3>{{ remoteDeviceName }}</h3>
          </div>
          <span class="device-chip">
            {{ isConnected ? remoteDeviceRole : '等待加入' }}
          </span>
        </div>

        <div
          class="device-screen device-screen-receiver"
          :class="{ 'device-screen-receiver-live': hasIncomingLiveTransfers }"
        >
          <ul v-if="hasIncomingLiveTransfers" class="file-list file-list-receiver">
            <li
              v-for="(record, index) in receiverStageTransfers"
              :key="`receive-${record.key}`"
              class="receiver-row"
              :class="{
                'receiver-row-hot': record.status === 'receiving' || record.status === 'complete',
              }"
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

          <ul v-else class="file-list file-list-preview-receiver" aria-hidden="true">
            <li
              v-for="(record, index) in receiverStageTransfers"
              :key="`preview-receive-${record.key}`"
              class="receiver-row receiver-row-preview"
              :style="{
                '--preview-delay': `${index * 280}ms`,
                '--impact-delay': `${index * 0.96}s`,
              }"
            >
              <span class="file-main">
                <span class="file-dot file-dot-preview" />
                <span class="file-name" :title="record.name">{{ record.label }}</span>
              </span>
              <small>{{ record.sizeLabel }}</small>
            </li>
          </ul>
        </div>
      </article>
    </div>
  </section>
</template>
