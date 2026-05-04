import { computed, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue'
import { Peer, type DataConnection } from 'peerjs'
import { toDataURL } from 'qrcode'
import type {
  PendingTransfer,
  RelayMode,
  RelayPayload,
  SessionState,
  TransferDirection,
  TransferRecord,
} from '../types/relay'
import {
  FILE_CHUNK_SIZE,
  HERO_CHIPS,
  HERO_FLOW,
  DEMO_BEAM_TRANSFERS,
  DEMO_FILE_NAMES,
  createPreviewTransferRecord,
  createQueuedFile,
  createQueuedTransferRecord,
  createRoomId,
  createShortRandomId,
  createTransferKey,
  detectCurrentDevice,
  formatDirection,
  formatTransferMeta,
  formatTransferSpeed,
  isLocalHost,
  sanitizeRoomId,
} from '../utils/relay'

interface BufferedIncomingFile {
  id: string
  name: string
  size: number
  mimeType: string
}

function isRelayPayload(value: unknown): value is RelayPayload {
  return Boolean(value && typeof value === 'object' && 'kind' in value)
}

function getLastItem<T>(items: T[]): T | undefined {
  return items[items.length - 1]
}

function resolvePeerPort() {
  if (window.location.port) {
    return Number(window.location.port)
  }

  return window.location.protocol === 'https:' ? 443 : 80
}

function sliceArrayBuffer(
  source: ArrayBufferLike,
  byteOffset = 0,
  byteLength = source.byteLength - byteOffset,
): ArrayBuffer {
  const output = new Uint8Array(byteLength)
  output.set(new Uint8Array(source, byteOffset, byteLength))
  return output.buffer
}

async function normalizeChunkPayload(chunk: unknown): Promise<ArrayBuffer | null> {
  if (chunk instanceof ArrayBuffer) {
    return chunk
  }

  if (chunk instanceof Blob) {
    return chunk.arrayBuffer()
  }

  if (ArrayBuffer.isView(chunk)) {
    const view = chunk as ArrayBufferView
    return sliceArrayBuffer(view.buffer, view.byteOffset, view.byteLength)
  }

  return null
}

export function useRelayTransfer() {
  const mode = ref<RelayMode>('host')
  const dialogOpen = ref(false)
  const fileInput = ref<HTMLInputElement | null>(null)

  const roomId = ref('')
  const baseUrl = ref('https://relay.example/')
  const hostname = ref('')
  const qrCodeDataUrl = ref('')
  const isLocalAddress = ref(false)

  const isConnected = ref(false)
  const errorMessage = ref('')
  const sessionState = ref<SessionState>('booting')
  const sessionMessage = ref('正在创建双向会话')

  const currentDeviceName = ref('当前设备')
  const remoteDeviceName = ref('待连接设备')
  const remoteDeviceRole = ref('等待加入')

  const transferSpeedLabel = ref('')
  const transferProgress = ref(0)
  const transferTotalBytes = ref(0)
  const transferCompletedBytes = ref(0)
  const activeTransferKey = ref<string | null>(null)

  const transferRecords = ref<TransferRecord[]>([])
  const pendingTransfers = ref<PendingTransfer[]>([])

  const peer = shallowRef<Peer | null>(null)
  const dataConnection = shallowRef<DataConnection | null>(null)

  const incomingChunkBuffers = new Map<string, ArrayBuffer[]>()
  const incomingManifestFiles = new Map<string, BufferedIncomingFile>()
  const incomingReceivedBytes = new Map<string, number>()
  const downloadUrls = new Set<string>()

  let reconnectTimer: number | null = null
  let throughputStartedAt = 0
  let sendingQueueLocked = false

  const isGuest = computed(() => mode.value === 'guest')
  const isHost = computed(() => mode.value === 'host')
  const sessionLink = computed(() => `${baseUrl.value}?room=${roomId.value}`)
  const hostPeerId = computed(() => `relay-session-${roomId.value}`)

  const outgoingTransfers = computed(() =>
    transferRecords.value.filter((record) => record.direction === 'outgoing'),
  )
  const incomingTransfers = computed(() =>
    transferRecords.value.filter((record) => record.direction === 'incoming'),
  )

  const previewOutgoingTransfers = computed(() =>
    DEMO_FILE_NAMES.map((name) => createPreviewTransferRecord(name, 'outgoing')),
  )
  const previewIncomingTransfers = computed(() =>
    DEMO_FILE_NAMES.map((name) => createPreviewTransferRecord(name, 'incoming')),
  )
  const previewBeamTransfers = computed(() =>
    DEMO_BEAM_TRANSFERS.map((item) =>
      createPreviewTransferRecord(item.name, item.direction as TransferDirection),
    ),
  )

  const senderStageTransfers = computed(() =>
    outgoingTransfers.value.length
      ? outgoingTransfers.value.slice(-3)
      : previewOutgoingTransfers.value,
  )
  const receiverStageTransfers = computed(() =>
    incomingTransfers.value.length
      ? incomingTransfers.value.slice(-3)
      : previewIncomingTransfers.value,
  )
  const guestOutgoingTransfers = computed(() => outgoingTransfers.value.slice(-3))
  const guestIncomingTransfers = computed(() => incomingTransfers.value.slice(-3))
  const dialogTransfers = computed(() => transferRecords.value.slice(-3))

  const beamTransfers = computed(() => {
    const activeTransfers = transferRecords.value.filter((record) =>
      ['sending', 'receiving', 'queued', 'complete'].includes(record.status),
    )

    return activeTransfers.length
      ? activeTransfers.slice(-3)
      : previewBeamTransfers.value
  })

  const currentTransfer = computed<TransferRecord | undefined>(() => {
    if (activeTransferKey.value) {
      return transferRecords.value.find((record) => record.key === activeTransferKey.value)
    }

    return getLastItem(transferRecords.value) ?? beamTransfers.value[0]
  })

  const hasIncomingLiveTransfers = computed(() => incomingTransfers.value.length > 0)
  const hasAnyTransfers = computed(() => transferRecords.value.length > 0)
  const hasQueuedOutgoingTransfers = computed(() =>
    transferRecords.value.some(
      (record) => record.direction === 'outgoing' && record.status === 'queued',
    ),
  )
  const isTransferring = computed(() =>
    transferRecords.value.some(
      (record) => record.status === 'sending' || record.status === 'receiving',
    ),
  )
  const isLiveSession = computed(
    () => isConnected.value || isTransferring.value || hasAnyTransfers.value,
  )

  const connectionBadge = computed(() => (isConnected.value ? '双向已连上' : '等待对方加入'))
  const heroPrimaryTitle = computed(() =>
    isConnected.value ? '立即发文件' : hasQueuedOutgoingTransfers.value ? '文件已就位' : '开始互传',
  )
  const heroPrimaryDescription = computed(() =>
    isConnected.value
      ? `现在从${currentDeviceName.value}直接发给对方`
      : hasQueuedOutgoingTransfers.value
        ? '对方进入后这一批会自动发出'
        : '对方扫码后直接进入同一会话',
  )
  const heroPrimaryBadge = computed(() => (isConnected.value ? '发送' : '开始'))
  const heroHint = computed(() =>
    isConnected.value
      ? '现在两边都能直接发。'
      : hasQueuedOutgoingTransfers.value
        ? '对方一进来，这一批会自动发出。'
        : '对方扫进来后，就能马上互传。',
  )

  const guestHeadline = computed(() =>
    isConnected.value || isTransferring.value || hasAnyTransfers.value
      ? '现在两边都能传'
      : '正在接入会话',
  )
  const guestSubHeadline = computed(() =>
    isConnected.value || isTransferring.value || hasAnyTransfers.value
      ? '发出去和收进来的文件都会出现在这里'
      : '连上以后，双方都能直接发送文件',
  )

  const stageEyebrow = computed(() => {
    if (isTransferring.value) return '双向互传中'
    if (isConnected.value) return '双向会话已连上'
    if (hasQueuedOutgoingTransfers.value) return '等待加入'
    return '扫码建会话'
  })

  const stageTitle = computed(() => {
    if (isTransferring.value && currentTransfer.value) {
      return `${currentTransfer.value.label} 正在互传`
    }
    if (isConnected.value) return '现在双方都能直接发'
    if (hasQueuedOutgoingTransfers.value) return '文件已排队'
    return '扫码进会话'
  })

  const stageDescription = computed(() => {
    if (isTransferring.value) return '文件正在设备之间直连同步。'
    if (isConnected.value) return '连上后就能直接互传，不用安装或登录。'
    if (hasQueuedOutgoingTransfers.value) return '对方进来后会自动发出。'
    return '电脑亮码，对方一扫就能加入。'
  })

  const dialogTitle = computed(() => {
    if (isTransferring.value) return '正在双向互传'
    if (isConnected.value) return '双向会话已连上'
    if (hasQueuedOutgoingTransfers.value) return '文件已排队，等待对方进入'
    return '扫码加入双向会话'
  })

  const dialogDescription = computed(() => {
    if (isLocalAddress.value) {
      return '当前页面还是 localhost 地址。真机扫码前，请改成局域网 IP 打开这个页面。'
    }
    if (isTransferring.value) {
      return '文件正在两台设备之间直接同步。现在继续选文件，也不需要重新扫码。'
    }
    if (isConnected.value) {
      return '对方已经进来。现在电脑和手机两边都能直接发照片、视频和文档。'
    }
    if (hasQueuedOutgoingTransfers.value) {
      return '文件已经排队。对方一旦进入，这批文件会自动开始发送。'
    }
    return '把二维码亮给对方。手机浏览器和微信都能直接打开，进来后就会加入同一条双向会话。'
  })

  const statusHeadline = computed(() => (errorMessage.value ? errorMessage.value : sessionMessage.value))
  const statusDescription = computed(() => {
    if (isLocalAddress.value) return '请用局域网地址打开，手机才能扫到真实页面。'
    if (errorMessage.value) return '当前会话暂时不可用，页面会自动重试。'
    if (isTransferring.value) return '文件正在同步，双方都可以继续追加发送。'
    if (isConnected.value) return '会话已经连上，现在电脑和手机两边都能直接发。'
    if (hasQueuedOutgoingTransfers.value) return '对方加入后，这批文件会自动开始发送。'
    return '二维码已准备好，对方扫码后会直接加入这条会话。'
  })

  const guestPrimaryDescription = computed(() => {
    if (isTransferring.value && currentTransfer.value) {
      return `${currentTransfer.value.label} 正在互传`
    }
    if (isConnected.value) return '现在两边都能直接发文件'
    if (hasQueuedOutgoingTransfers.value) return '文件已经排队，等会话接通后自动发送'
    return '正在加入双向会话'
  })

  const guestPageDescription = computed(() => {
    if (isTransferring.value) {
      return '会话已经建立，当前文件正在直连同步。你可以继续追加文件，对方也能马上反向传回来。'
    }
    if (isConnected.value) {
      return '现在电脑和手机两边都能直接互传，谁先选文件谁先发。'
    }
    if (hasQueuedOutgoingTransfers.value) {
      return '文件已经选好，对方进来后会自动开始发送。'
    }
    return '先亮码把对方拉进来。连上以后，双方都能直接发送文件。'
  })

  const sessionNotice = computed(() => (errorMessage.value ? errorMessage.value : sessionMessage.value))

  function updateSessionState(nextState: SessionState, message: string) {
    sessionState.value = nextState
    sessionMessage.value = message
  }

  function resetTransferMetrics() {
    transferProgress.value = 0
    transferSpeedLabel.value = ''
    transferTotalBytes.value = 0
    transferCompletedBytes.value = 0
    activeTransferKey.value = null
  }

  function clearIncomingTracking() {
    incomingChunkBuffers.clear()
    incomingManifestFiles.clear()
    incomingReceivedBytes.clear()
  }

  function revokeDownloadLinks() {
    downloadUrls.forEach((downloadUrl) => URL.revokeObjectURL(downloadUrl))
    downloadUrls.clear()
  }

  async function refreshQrCode() {
    qrCodeDataUrl.value = await toDataURL(sessionLink.value, {
      width: 720,
      margin: 0,
      color: { dark: '#11131a', light: '#f7f3eb' },
    })
  }

  function buildPeerOptions() {
    return {
      host: hostname.value,
      port: resolvePeerPort(),
      path: '/peerjs',
      secure: window.location.protocol === 'https:',
      debug: 1,
      config: {
        iceServers: [
          { urls: 'stun:stun.l.google.com:19302' },
          { urls: 'stun:stun.cloudflare.com:3478' },
        ],
      },
    }
  }

  function clearReconnectTimer() {
    if (reconnectTimer !== null) {
      window.clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  function scheduleGuestReconnect() {
    clearReconnectTimer()
    reconnectTimer = window.setTimeout(() => {
      openGuestConnection()
    }, 1400)
  }

  function destroyConnection() {
    if (dataConnection.value) {
      dataConnection.value.close()
      dataConnection.value = null
    }
  }

  function destroyPeer() {
    if (peer.value) {
      peer.value.destroy()
      peer.value = null
    }
  }

  function resetConnectionState() {
    resetTransferMetrics()
    errorMessage.value = ''
    isConnected.value = false
    remoteDeviceName.value = isHost.value ? '待连接设备' : '会话主机'
    remoteDeviceRole.value = isHost.value ? '等待加入' : '会话主机'
  }

  function cleanupSession() {
    destroyConnection()
    clearIncomingTracking()
    destroyPeer()
  }

  function startPeerSession() {
    cleanupSession()
    resetConnectionState()

    const peerId = isHost.value ? hostPeerId.value : `relay-peer-${roomId.value}-${createShortRandomId()}`
    const instance = new Peer(peerId, buildPeerOptions())

    peer.value = instance

    updateSessionState(
      'booting',
      isHost.value ? '正在创建双向会话' : '正在加入双向会话',
    )

    instance.on('open', () => {
      errorMessage.value = ''

      if (isHost.value) {
        updateSessionState('ready', '会话已准备好，等待对方加入')
        return
      }

      openGuestConnection()
    })

    instance.on('connection', (incomingConnection) => {
      if (!isHost.value) {
        incomingConnection.close()
        return
      }

      bindConnection(incomingConnection)
    })

    instance.on('error', (error) => {
      errorMessage.value =
        error instanceof Error ? error.message : '连接暂时失败，请稍后重试'

      if (isGuest.value) {
        isConnected.value = false
        updateSessionState('booting', '会话暂时不可用，正在自动重试')
        scheduleGuestReconnect()
        return
      }

      updateSessionState('error', '当前会话暂时不可用')
    })
  }

  function openGuestConnection() {
    const currentPeer = peer.value

    if (!currentPeer || currentPeer.disconnected || dataConnection.value?.open) {
      return
    }

    if (dataConnection.value) {
      dataConnection.value.close()
      dataConnection.value = null
    }

    updateSessionState('booting', '正在加入会话')

    const connection = currentPeer.connect(hostPeerId.value, {
      reliable: true,
      serialization: 'binary',
      label: 'relay-transfer',
    })

    bindConnection(connection)
  }

  function mergeTransferRecords(nextRecords: TransferRecord[]) {
    const recordMap = new Map(transferRecords.value.map((record) => [record.key, record]))

    nextRecords.forEach((record) => {
      recordMap.set(record.key, record)
    })

    transferRecords.value = Array.from(recordMap.values())
  }

  function updateTransferRecord(key: string, patch: Partial<TransferRecord>) {
    transferRecords.value = transferRecords.value.map((record) =>
      record.key === key ? { ...record, ...patch } : record,
    )
  }

  function markTransferMetrics(completedBytes: number, totalBytes: number, startedAt: number) {
    transferCompletedBytes.value = completedBytes
    transferTotalBytes.value = totalBytes
    transferProgress.value = totalBytes
      ? Math.min(100, Math.round((completedBytes / totalBytes) * 100))
      : 0

    const elapsedSeconds = Math.max(1, (performance.now() - startedAt) / 1000)
    transferSpeedLabel.value = formatTransferSpeed(completedBytes / elapsedSeconds)
  }

  function sendHello() {
    if (!dataConnection.value?.open) return

    dataConnection.value.send({
      kind: 'hello',
      deviceName: currentDeviceName.value,
      mode: mode.value,
    } satisfies RelayPayload)
  }

  async function bindConnection(connection: DataConnection) {
    if (dataConnection.value && dataConnection.value !== connection) {
      dataConnection.value.close()
    }

    dataConnection.value = connection

    connection.on('open', () => {
      clearReconnectTimer()
      isConnected.value = true
      errorMessage.value = ''
      sendHello()

      updateSessionState(
        'connected',
        isHost.value
          ? hasQueuedOutgoingTransfers.value
            ? '对方已加入，排队文件会自动发送'
            : '对方已加入，现在两边都能发'
          : hasQueuedOutgoingTransfers.value
            ? '会话已连上，排队文件会自动发送'
            : '会话已连上，现在两边都能发',
      )

      void sendQueuedTransfers()
    })

    connection.on('data', (payload) => {
      void handlePayload(payload)
    })

    connection.on('close', () => {
      if (dataConnection.value === connection) {
        dataConnection.value = null
      }

      isConnected.value = false
      markInterruptedTransfers()
      clearIncomingTracking()
      resetTransferMetrics()

      if (isGuest.value) {
        updateSessionState('booting', '会话已断开，正在自动重连')
        scheduleGuestReconnect()
        return
      }

      updateSessionState('ready', '对方已离开，可重新扫码加入')
    })

    connection.on('error', () => {
      markInterruptedTransfers()
      clearIncomingTracking()

      if (isGuest.value) {
        isConnected.value = false
        updateSessionState('booting', '连接失败，正在重新接入')
        scheduleGuestReconnect()
        return
      }

      updateSessionState('error', '当前会话连接失败')
    })
  }

  function markInterruptedTransfers() {
    transferRecords.value = transferRecords.value.map((record) =>
      record.status === 'sending' || record.status === 'receiving'
        ? { ...record, status: 'failed' as const }
        : record,
    )
  }

  async function handlePayload(payload: unknown) {
    if (!isRelayPayload(payload)) return

    if (payload.kind === 'hello') {
      isConnected.value = true
      remoteDeviceName.value = payload.deviceName
      remoteDeviceRole.value = payload.mode === 'host' ? '会话主机' : '已连接设备'

      if (!isTransferring.value) {
        updateSessionState('connected', '会话已经连上，现在两边都能发')
      }

      if (pendingTransfers.value.length) {
        void sendQueuedTransfers()
      }

      return
    }

    if (payload.kind === 'manifest') {
      throughputStartedAt = performance.now()
      transferTotalBytes.value = payload.totalBytes
      transferCompletedBytes.value = 0
      transferProgress.value = 0
      transferSpeedLabel.value = ''

      mergeTransferRecords(
        payload.files.map((file) =>
          createQueuedTransferRecord(payload.transferId, file, 'incoming'),
        ),
      )

      payload.files.forEach((file) => {
        const recordKey = createTransferKey(payload.transferId, file.id)
        incomingManifestFiles.set(recordKey, file)
        incomingReceivedBytes.set(recordKey, 0)
      })

      updateSessionState('connected', '对方已经选中文件，正在准备发送')
      return
    }

    if (payload.kind === 'file-start') {
      const recordKey = createTransferKey(payload.transferId, payload.fileId)
      const transfer = transferRecords.value.find((record) => record.key === recordKey)

      activeTransferKey.value = recordKey
      incomingChunkBuffers.set(recordKey, [])
      updateTransferRecord(recordKey, { status: 'receiving', progress: 0 })
      updateSessionState('transferring', `${transfer?.label ?? '文件'} 正在传过来`)
      return
    }

    if (payload.kind === 'file-chunk') {
      const recordKey = createTransferKey(payload.transferId, payload.fileId)
      const chunkList = incomingChunkBuffers.get(recordKey)
      const manifestFile = incomingManifestFiles.get(recordKey)
      const normalizedChunk = await normalizeChunkPayload(payload.chunk)

      if (!chunkList || !manifestFile || !normalizedChunk) {
        return
      }

      chunkList.push(normalizedChunk)
      const nextReceivedBytes = (incomingReceivedBytes.get(recordKey) ?? 0) + normalizedChunk.byteLength

      incomingReceivedBytes.set(recordKey, nextReceivedBytes)
      updateTransferRecord(recordKey, {
        progress: Math.min(100, Math.round((nextReceivedBytes / manifestFile.size) * 100)),
      })
      markTransferMetrics(nextReceivedBytes, manifestFile.size, throughputStartedAt)
      return
    }

    if (payload.kind === 'file-end') {
      const recordKey = createTransferKey(payload.transferId, payload.fileId)
      const manifestFile = incomingManifestFiles.get(recordKey)
      const chunkList = incomingChunkBuffers.get(recordKey)

      if (!manifestFile || !chunkList) {
        return
      }

      const blob = new Blob(chunkList, {
        type: manifestFile.mimeType || 'application/octet-stream',
      })
      const downloadUrl = URL.createObjectURL(blob)

      downloadUrls.add(downloadUrl)
      incomingChunkBuffers.delete(recordKey)
      incomingManifestFiles.delete(recordKey)
      incomingReceivedBytes.delete(recordKey)

      updateTransferRecord(recordKey, {
        status: 'complete',
        progress: 100,
        downloadUrl,
      })

      if (activeTransferKey.value === recordKey) {
        activeTransferKey.value = null
      }

      if (!isTransferring.value) {
        updateSessionState('connected', '收到一批文件，可以继续互传')
      }

      return
    }

    if (payload.kind === 'transfer-complete') {
      activeTransferKey.value = null

      if (isTransferring.value) {
        updateSessionState('transferring', '文件仍在双向传输')
        return
      }

      updateSessionState('done', '这一批文件已经送达，可继续互传')
    }
  }

  async function waitForBufferDrain(connection: DataConnection) {
    while (connection.dataChannel && connection.dataChannel.bufferedAmount > 1024 * 1024) {
      await new Promise((resolve) => window.setTimeout(resolve, 16))
    }
  }

  async function sendQueuedTransfers() {
    const connection = dataConnection.value

    if (!connection || !connection.open || !pendingTransfers.value.length || sendingQueueLocked) {
      return
    }

    sendingQueueLocked = true

    try {
      while (pendingTransfers.value.length && dataConnection.value?.open) {
        const nextTransfer = pendingTransfers.value[0]
        const manifestFiles = nextTransfer.items.map(({ id, file }) => ({
          id,
          name: file.name,
          size: file.size,
          mimeType: file.type || 'application/octet-stream',
        }))

        transferTotalBytes.value = nextTransfer.totalBytes
        transferCompletedBytes.value = 0
        transferProgress.value = 0
        transferSpeedLabel.value = ''
        throughputStartedAt = performance.now()

        connection.send({
          kind: 'manifest',
          transferId: nextTransfer.transferId,
          files: manifestFiles,
          totalBytes: nextTransfer.totalBytes,
        } satisfies RelayPayload)

        let sentBytes = 0
        updateSessionState('transferring', '正在把文件直接发给对方')

        for (const queuedFile of nextTransfer.items) {
          const recordKey = createTransferKey(nextTransfer.transferId, queuedFile.id)

          activeTransferKey.value = recordKey
          updateTransferRecord(recordKey, { status: 'sending', progress: 0 })

          connection.send({
            kind: 'file-start',
            transferId: nextTransfer.transferId,
            fileId: queuedFile.id,
          } satisfies RelayPayload)

          for (let offset = 0; offset < queuedFile.file.size; offset += FILE_CHUNK_SIZE) {
            const chunk = new Uint8Array(
              await queuedFile.file.slice(offset, offset + FILE_CHUNK_SIZE).arrayBuffer(),
            )

            await waitForBufferDrain(connection)

            connection.send({
              kind: 'file-chunk',
              transferId: nextTransfer.transferId,
              fileId: queuedFile.id,
              chunk,
            } satisfies RelayPayload)

            sentBytes += chunk.byteLength

            updateTransferRecord(recordKey, {
              progress: Math.min(
                100,
                Math.round(((offset + chunk.byteLength) / queuedFile.file.size) * 100),
              ),
            })
            markTransferMetrics(sentBytes, nextTransfer.totalBytes, throughputStartedAt)
          }

          connection.send({
            kind: 'file-end',
            transferId: nextTransfer.transferId,
            fileId: queuedFile.id,
          } satisfies RelayPayload)

          updateTransferRecord(recordKey, { status: 'complete', progress: 100 })
        }

        activeTransferKey.value = null

        connection.send({
          kind: 'transfer-complete',
          transferId: nextTransfer.transferId,
        } satisfies RelayPayload)

        pendingTransfers.value.shift()

        updateSessionState(
          isTransferring.value ? 'transferring' : 'done',
          isTransferring.value
            ? '文件仍在双向传输'
            : '这一批文件已经发出，对方现在也可以回传',
        )
      }
    } catch {
      markInterruptedTransfers()
      updateSessionState('error', '发送中断，请重新选择文件')
    } finally {
      sendingQueueLocked = false
    }
  }

  function openDialog() {
    if (isHost.value) {
      dialogOpen.value = true
    }
  }

  function closeDialog() {
    dialogOpen.value = false
  }

  function triggerFileSelect() {
    fileInput.value?.click()
  }

  function handlePrimaryAction() {
    if (isConnected.value) {
      triggerFileSelect()
      return
    }

    openDialog()
  }

  function handleFileChange(event: Event) {
    const input = event.target as HTMLInputElement
    const files = Array.from(input.files ?? [])

    if (!files.length) {
      input.value = ''
      return
    }

    const transferId = createRoomId()
    const queuedFiles = files.map((file) => createQueuedFile(file))
    const totalBytes = queuedFiles.reduce((sum, item) => sum + item.file.size, 0)

    const nextRecords = queuedFiles.map(({ id, file }) =>
      createQueuedTransferRecord(
        transferId,
        {
          id,
          name: file.name,
          size: file.size,
          mimeType: file.type || 'application/octet-stream',
        },
        'outgoing',
      ),
    )

    mergeTransferRecords(nextRecords)

    pendingTransfers.value = [
      ...pendingTransfers.value,
      {
        transferId,
        items: queuedFiles,
        totalBytes,
      },
    ]

    activeTransferKey.value = nextRecords[0]?.key ?? null

    if (dataConnection.value?.open) {
      void sendQueuedTransfers()
    } else {
      updateSessionState(
        isHost.value ? 'ready' : 'booting',
        isHost.value
          ? '文件已经排队，等对方加入后自动发送'
          : '文件已经排队，等会话接通后自动发送',
      )
    }

    input.value = ''
  }

  function downloadTransfer(record: TransferRecord) {
    if (!record.downloadUrl) return

    const link = document.createElement('a')
    link.href = record.downloadUrl
    link.download = record.name
    link.click()
  }

  watch(sessionLink, async () => {
    if (isHost.value) {
      await refreshQrCode()
    }
  })

  onMounted(async () => {
    const currentUrl = new URL(window.location.href)
    const requestedRoomId = sanitizeRoomId(currentUrl.searchParams.get('room'))
    const forceHostMode = currentUrl.searchParams.get('mode') === 'host'
    const device = detectCurrentDevice()

    baseUrl.value = `${currentUrl.origin}${currentUrl.pathname}`
    hostname.value = currentUrl.hostname
    isLocalAddress.value = isLocalHost(currentUrl.hostname)
    mode.value = forceHostMode ? 'host' : requestedRoomId ? 'guest' : 'host'
    roomId.value = requestedRoomId || createRoomId()
    currentDeviceName.value = device.title
    remoteDeviceName.value = isHost.value ? '待连接设备' : '会话主机'
    remoteDeviceRole.value = isHost.value ? '等待加入' : '会话主机'

    if (isHost.value) {
      await refreshQrCode()
    }

    startPeerSession()
  })

  onBeforeUnmount(() => {
    clearReconnectTimer()
    revokeDownloadLinks()
    cleanupSession()
  })

  return {
    fileInput,
    dialogOpen,
    isGuest,
    isHost,
    isConnected,
    isLocalAddress,
    isLiveSession,
    hasAnyTransfers,
    hasIncomingLiveTransfers,
    currentDeviceName,
    remoteDeviceName,
    remoteDeviceRole,
    qrCodeDataUrl,
    sessionLink,
    transferProgress,
    transferSpeedLabel,
    currentTransfer,
    senderStageTransfers,
    receiverStageTransfers,
    guestOutgoingTransfers,
    guestIncomingTransfers,
    dialogTransfers,
    beamTransfers,
    heroChips: HERO_CHIPS,
    heroFlow: HERO_FLOW,
    connectionBadge,
    heroPrimaryTitle,
    heroPrimaryDescription,
    heroPrimaryBadge,
    heroHint,
    guestHeadline,
    guestSubHeadline,
    stageEyebrow,
    stageTitle,
    stageDescription,
    dialogTitle,
    dialogDescription,
    statusHeadline,
    statusDescription,
    guestPrimaryDescription,
    guestPageDescription,
    sessionNotice,
    sessionState,
    openDialog,
    closeDialog,
    triggerFileSelect,
    handlePrimaryAction,
    handleFileChange,
    downloadTransfer,
    formatTransferMeta,
    formatDirection,
  }
}
