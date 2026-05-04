export type RelayMode = 'host' | 'guest'

export type SessionState =
  | 'booting'
  | 'ready'
  | 'connected'
  | 'transferring'
  | 'done'
  | 'error'

export type TransferDirection = 'outgoing' | 'incoming'
export type TransferMetaRole = 'sender' | 'receiver'

export type TransferStatus =
  | 'preview'
  | 'queued'
  | 'sending'
  | 'receiving'
  | 'complete'
  | 'failed'

export interface TransferRecord {
  key: string
  transferId: string
  id: string
  fileId: string
  name: string
  label: string
  sizeBytes: number
  sizeLabel: string
  mimeType: string
  direction: TransferDirection
  status: TransferStatus
  progress: number
  downloadUrl?: string
}

export type TransferMetaFormatter = (
  transfer: TransferRecord,
  role?: TransferMetaRole,
) => string

export type TransferDirectionFormatter = (direction: TransferDirection) => string

export interface QueuedFileItem {
  id: string
  file: File
}

export interface PendingTransfer {
  transferId: string
  items: QueuedFileItem[]
  totalBytes: number
}

export interface IncomingManifestFile {
  id: string
  name: string
  size: number
  mimeType: string
}

export interface RelayPayloadHello {
  kind: 'hello'
  deviceName: string
  mode: RelayMode
}

export interface RelayPayloadManifest {
  kind: 'manifest'
  transferId: string
  files: IncomingManifestFile[]
  totalBytes: number
}

export interface RelayPayloadFileStart {
  kind: 'file-start'
  transferId: string
  fileId: string
}

export interface RelayPayloadFileChunk {
  kind: 'file-chunk'
  transferId: string
  fileId: string
  chunk: Uint8Array | ArrayBuffer | Blob
}

export interface RelayPayloadFileEnd {
  kind: 'file-end'
  transferId: string
  fileId: string
}

export interface RelayPayloadTransferComplete {
  kind: 'transfer-complete'
  transferId: string
}

export type RelayPayload =
  | RelayPayloadHello
  | RelayPayloadManifest
  | RelayPayloadFileStart
  | RelayPayloadFileChunk
  | RelayPayloadFileEnd
  | RelayPayloadTransferComplete

export interface DeviceDescriptor {
  title: string
  meta: string
}
