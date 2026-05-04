import type {
  DeviceDescriptor,
  IncomingManifestFile,
  TransferDirection,
  TransferMetaRole,
  TransferRecord,
} from '../types/relay'

export const HERO_CHIPS = ['免安装', '微信内可用', '电脑手机都能传']

export const HERO_FLOW = ['电脑亮码', '手机扫码', '直接互传']

export const DEMO_FILE_NAMES = ['品牌提案.pdf', '旅行短片.mov', '合同扫描件.zip']

export const DEMO_BEAM_TRANSFERS = [
  { name: '品牌提案.pdf', direction: 'outgoing' },
  { name: '旅行短片.mov', direction: 'incoming' },
  { name: '合同扫描件.zip', direction: 'outgoing' },
] as const

export const FILE_CHUNK_SIZE = 24 * 1024

export function sanitizeRoomId(value: string | null): string {
  return value ? value.replace(/[^a-zA-Z0-9_-]/g, '').slice(0, 32) : ''
}

export function createRoomId(): string {
  const randomId =
    typeof crypto !== 'undefined' && 'randomUUID' in crypto
      ? crypto.randomUUID().replace(/-/g, '')
      : `${Date.now().toString(36)}${Math.random().toString(36).slice(2)}`

  return randomId.slice(0, 10)
}

export function createShortRandomId(): string {
  return Math.random().toString(36).slice(2, 8)
}

export function createHash(input: string): number {
  let hash = 0

  for (const char of input) {
    hash = (hash * 33 + char.charCodeAt(0)) >>> 0
  }

  return hash >>> 0
}

export function generatePreviewSizeLabel(fileName: string): string {
  const hash = createHash(fileName)
  const extension = fileName.split('.').pop()?.toLowerCase() ?? ''

  if (['mov', 'mp4', 'mkv', 'zip'].includes(extension)) {
    return `${(0.9 + (hash % 14) / 10).toFixed(1)} GB`
  }

  if (['pdf', 'docx', 'psd', 'key'].includes(extension)) {
    return `${18 + (hash % 56)} MB`
  }

  return `${12 + (hash % 24)} MB`
}

export function formatBytes(bytes: number): string {
  if (bytes >= 1024 * 1024 * 1024) {
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`
  }

  if (bytes >= 1024 * 1024) {
    return `${Math.max(1, Math.round(bytes / (1024 * 1024))).toString()} MB`
  }

  return `${Math.max(1, Math.round(bytes / 1024)).toString()} KB`
}

export function formatTransferSpeed(bytesPerSecond: number): string {
  if (bytesPerSecond >= 1024 * 1024) {
    return `${(bytesPerSecond / 1024 / 1024).toFixed(1)} MB/s`
  }

  if (bytesPerSecond >= 1024) {
    return `${Math.max(1, Math.round(bytesPerSecond / 1024)).toString()} KB/s`
  }

  return `${Math.max(1, Math.round(bytesPerSecond)).toString()} B/s`
}

export function abbreviateFileName(fileName: string): string {
  if (fileName.length <= 16) {
    return fileName
  }

  const extensionIndex = fileName.lastIndexOf('.')

  if (extensionIndex <= 1 || extensionIndex >= fileName.length - 6) {
    return `${fileName.slice(0, 8)}…${fileName.slice(-5)}`
  }

  return `${fileName.slice(0, 8)}…${fileName.slice(extensionIndex)}`
}

export function isLocalHost(hostname: string): boolean {
  return ['127.0.0.1', 'localhost', '::1'].includes(hostname)
}

export function detectCurrentDevice(): DeviceDescriptor {
  if (typeof navigator === 'undefined') {
    return { title: '当前设备', meta: '这台设备' }
  }

  const userAgent = navigator.userAgent.toLowerCase()

  if (/ipad|tablet/.test(userAgent)) {
    return { title: '当前平板', meta: '这台平板' }
  }

  if (/iphone|android|mobile/.test(userAgent)) {
    return { title: '当前手机', meta: '这台手机' }
  }

  return { title: '当前电脑', meta: '这台电脑' }
}

export function createTransferKey(transferId: string, fileId: string): string {
  return `${transferId}:${fileId}`
}

export function createPreviewTransferRecord(
  fileName: string,
  direction: TransferDirection,
): TransferRecord {
  const previewKey = `preview-${direction}-${createHash(fileName)}`

  return {
    key: previewKey,
    transferId: `preview-${direction}`,
    id: previewKey,
    fileId: previewKey,
    name: fileName,
    label: abbreviateFileName(fileName),
    sizeBytes: 0,
    sizeLabel: generatePreviewSizeLabel(fileName),
    mimeType: '',
    direction,
    status: 'preview',
    progress: 0,
  }
}

export function createQueuedTransferRecord(
  transferId: string,
  file: IncomingManifestFile,
  direction: TransferDirection,
): TransferRecord {
  const key = createTransferKey(transferId, file.id)

  return {
    key,
    transferId,
    id: key,
    fileId: file.id,
    name: file.name,
    label: abbreviateFileName(file.name),
    sizeBytes: file.size,
    sizeLabel: formatBytes(file.size),
    mimeType: file.mimeType,
    direction,
    status: 'queued',
    progress: 0,
  }
}

export function createQueuedFile(file: File): { id: string; file: File } {
  return {
    id: `file-${createHash(`${file.name}-${file.size}-${file.lastModified}`)}`,
    file,
  }
}

export function formatTransferMeta(
  transfer: TransferRecord,
  role: TransferMetaRole = 'sender',
): string {
  if (transfer.status === 'complete' && transfer.downloadUrl && role === 'receiver') {
    return '可下载'
  }

  if (transfer.status === 'complete') {
    return role === 'receiver' ? '已送达' : '已发出'
  }

  if (transfer.status === 'sending' || transfer.status === 'receiving') {
    return `${transfer.progress}%`
  }

  if (transfer.status === 'queued') {
    return '待开始'
  }

  if (transfer.status === 'failed') {
    return '失败'
  }

  return transfer.sizeLabel
}

export function formatDirection(direction: TransferDirection): string {
  return direction === 'incoming' ? '收到' : '发出'
}
