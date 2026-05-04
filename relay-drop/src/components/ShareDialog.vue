<script setup lang="ts">
import type {
  SessionState,
  TransferDirectionFormatter,
  TransferMetaFormatter,
  TransferRecord,
} from '../types/relay'

defineProps<{
  isConnected: boolean
  sessionState: SessionState
  guestOutgoingTransfers: TransferRecord[]
  qrCodeDataUrl: string
  sessionLink: string
  dialogTitle: string
  dialogDescription: string
  statusHeadline: string
  statusDescription: string
  hasAnyTransfers: boolean
  dialogTransfers: TransferRecord[]
  currentDeviceName: string
  remoteDeviceName: string
  formatTransferMeta: TransferMetaFormatter
  formatDirection: TransferDirectionFormatter
}>()

const emit = defineEmits<{
  close: []
  'trigger-file': []
  download: [record: TransferRecord]
}>()
</script>

<template>
  <div class="dialog-backdrop" @click="emit('close')">
    <div
      class="share-dialog"
      :class="{
        'share-dialog-connected': isConnected,
        'share-dialog-active': sessionState === 'transferring' || sessionState === 'done',
      }"
      @click.stop
    >
      <button type="button" class="dialog-close" @click="emit('close')">关闭</button>

      <div class="dialog-grid">
        <div class="dialog-qr-column">
          <template v-if="isConnected">
            <p class="dialog-label">这边也能直接发</p>

            <div class="dialog-send-card">
              <button
                type="button"
                class="button button-primary dialog-send-button"
                @click="emit('trigger-file')"
              >
                <span class="button-copy">
                  <strong>选择文件发给对方</strong>
                  <small>会话已连上，电脑这边现在也能直接发给手机</small>
                </span>
                <span class="button-badge">发送</span>
              </button>

              <div class="dialog-send-note">
                <span class="dialog-status-dot dialog-status-dot-live" />
                <small>谁先选文件谁先发，传输进度会立刻同步到两边。</small>
              </div>

              <ul
                v-if="guestOutgoingTransfers.length"
                class="file-list dialog-file-list dialog-send-list"
              >
                <li
                  v-for="(record, index) in guestOutgoingTransfers"
                  :key="`dialog-send-${record.key}`"
                  :style="{ '--item-delay': `${index * 90}ms` }"
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

              <div v-else class="dialog-send-empty">
                <strong>还没有发出的文件</strong>
                <small>现在选中文件，就会直接发到对方手机。</small>
              </div>
            </div>
          </template>

          <template v-else>
            <p class="dialog-label">扫码加入双向会话</p>

            <div class="dialog-qr-frame">
              <a
                class="dialog-qr-shell dialog-qr-link"
                :href="sessionLink"
                target="_blank"
                rel="noreferrer"
              >
                <img
                  v-if="qrCodeDataUrl"
                  :src="qrCodeDataUrl"
                  alt="接收页二维码"
                  class="dialog-qr-image"
                  loading="eager"
                  decoding="sync"
                  fetchpriority="high"
                />
              </a>
            </div>

            <p class="dialog-qr-note">微信扫一扫也能直接加入</p>
          </template>
        </div>

        <div class="dialog-copy-column">
          <p class="dialog-label">当前会话</p>
          <h3>{{ dialogTitle }}</h3>
          <p class="dialog-text">{{ dialogDescription }}</p>

          <div class="dialog-meta">
            <span>免安装</span>
            <span>跨平台</span>
            <span>直接送达</span>
          </div>

          <div v-if="isConnected" class="dialog-session-bridge-card">
            <div class="dialog-session-node">
              <span class="dialog-session-node-label">当前电脑</span>
              <strong>{{ currentDeviceName }}</strong>
            </div>

            <div class="dialog-session-bridge" aria-hidden="true">
              <span class="dialog-session-line" />
              <span class="dialog-session-pulse" />
            </div>

            <div class="dialog-session-node dialog-session-node-remote">
              <span class="dialog-session-node-label">已加入设备</span>
              <strong>{{ remoteDeviceName }}</strong>
            </div>
          </div>

          <div
            class="dialog-status-card"
            :class="{
              'dialog-status-card-error': sessionState === 'error',
              'dialog-status-card-live': isConnected && sessionState !== 'error',
            }"
          >
            <div class="dialog-status-head">
              <span class="dialog-status-dot" :class="{ 'dialog-status-dot-live': isConnected }" />
              <strong>{{ statusHeadline }}</strong>
            </div>
            <small>{{ statusDescription }}</small>
          </div>

          <div class="dialog-flow">
            <span
              class="dialog-flow-step"
              :class="{ 'dialog-flow-step-active': sessionState !== 'booting' && sessionState !== 'error' }"
            >
              会话就绪
            </span>
            <span
              class="dialog-flow-step"
              :class="{ 'dialog-flow-step-active': isConnected || sessionState === 'transferring' || sessionState === 'done' }"
            >
              对方加入
            </span>
            <span
              class="dialog-flow-step"
              :class="{ 'dialog-flow-step-active': sessionState === 'transferring' || sessionState === 'done' }"
            >
              开始互传
            </span>
          </div>

          <div class="dialog-transfer-card">
            <div class="dialog-transfer-head">
              <strong>
                {{
                  hasAnyTransfers
                    ? '当前传输列表'
                    : isConnected
                      ? '双向会话已连上'
                      : '扫码后这里会开始显示文件'
                }}
              </strong>
              <small>
                {{
                  hasAnyTransfers
                    ? `${dialogTransfers.length} 个文件在这条会话里流转过`
                    : isConnected
                      ? '现在双方都可以直接发，第一份文件会立刻出现在这里'
                      : '建立链接后，文件名和进度会出现在这里'
                }}
              </small>
            </div>

            <ul v-if="hasAnyTransfers" class="file-list dialog-file-list">
              <li
                v-for="(record, index) in dialogTransfers"
                :key="`dialog-file-${record.key}`"
                :style="{ '--item-delay': `${index * 100}ms` }"
              >
                <span class="file-main">
                  <span
                    class="file-dot"
                    :class="{
                      'file-dot-active': record.status === 'receiving' || record.status === 'sending',
                      'file-dot-done': record.status === 'complete',
                    }"
                  />
                  <span class="file-name" :title="record.name">{{ record.label }}</span>
                </span>

                <span class="dialog-file-meta">
                  <small class="dialog-direction-pill">{{ formatDirection(record.direction) }}</small>
                  <button
                    v-if="record.downloadUrl"
                    type="button"
                    class="file-action"
                    @click="emit('download', record)"
                  >
                    下载
                  </button>
                  <small v-else>
                    {{
                      formatTransferMeta(
                        record,
                        record.direction === 'incoming' ? 'receiver' : 'sender',
                      )
                    }}
                  </small>
                </span>
              </li>
            </ul>

            <div v-else-if="isConnected" class="dialog-transfer-empty">
              <strong>现在双方都可以发，等第一份文件</strong>
              <small>谁先选文件，这里就会先显示谁的文件名和进度。</small>
            </div>

            <div v-else class="dialog-transfer-placeholder" aria-hidden="true">
              <span v-for="slot in 3" :key="`dialog-slot-${slot}`" class="dialog-transfer-slot" />
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
