<script setup lang="ts">
import GuestPanel from './components/GuestPanel.vue'
import HeroPanel from './components/HeroPanel.vue'
import ShareDialog from './components/ShareDialog.vue'
import StagePanel from './components/StagePanel.vue'
import { useRelayTransfer } from './composables/useRelayTransfer'

const {
  fileInput,
  dialogOpen,
  isGuest,
  isHost,
  isConnected,
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
  heroChips,
  heroFlow,
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
} = useRelayTransfer()

void fileInput
</script>

<template>
  <GuestPanel
    v-if="isGuest"
    :guest-headline="guestHeadline"
    :guest-sub-headline="guestSubHeadline"
    :guest-page-description="guestPageDescription"
    :guest-primary-description="guestPrimaryDescription"
    :session-notice="sessionNotice"
    :current-device-name="currentDeviceName"
    :remote-device-name="remoteDeviceName"
    :connection-badge="connectionBadge"
    :is-connected="isConnected"
    :guest-outgoing-transfers="guestOutgoingTransfers"
    :guest-incoming-transfers="guestIncomingTransfers"
    :format-transfer-meta="formatTransferMeta"
    @trigger-file="triggerFileSelect"
    @download="downloadTransfer"
  />

  <div v-else class="landing-shell">
    <main class="hero-layout">
      <HeroPanel
        :hero-primary-title="heroPrimaryTitle"
        :hero-primary-description="heroPrimaryDescription"
        :hero-primary-badge="heroPrimaryBadge"
        :hero-flow="heroFlow"
        :hero-hint="heroHint"
        :hero-chips="heroChips"
        @primary="handlePrimaryAction"
      />

      <StagePanel
        :stage-eyebrow="stageEyebrow"
        :stage-title="stageTitle"
        :stage-description="stageDescription"
        :is-host="isHost"
        :is-connected="isConnected"
        :is-live-session="isLiveSession"
        :has-incoming-live-transfers="hasIncomingLiveTransfers"
        :qr-code-data-url="qrCodeDataUrl"
        :current-transfer="currentTransfer"
        :session-state="sessionState"
        :transfer-speed-label="transferSpeedLabel"
        :status-headline="statusHeadline"
        :transfer-progress="transferProgress"
        :beam-transfers="beamTransfers"
        :current-device-name="currentDeviceName"
        :remote-device-name="remoteDeviceName"
        :remote-device-role="remoteDeviceRole"
        :sender-stage-transfers="senderStageTransfers"
        :receiver-stage-transfers="receiverStageTransfers"
        :format-transfer-meta="formatTransferMeta"
        @open-qr="openDialog"
        @trigger-file="triggerFileSelect"
        @download="downloadTransfer"
      />
    </main>

    <ShareDialog
      v-if="dialogOpen"
      :is-connected="isConnected"
      :session-state="sessionState"
      :guest-outgoing-transfers="guestOutgoingTransfers"
      :qr-code-data-url="qrCodeDataUrl"
      :session-link="sessionLink"
      :dialog-title="dialogTitle"
      :dialog-description="dialogDescription"
      :status-headline="statusHeadline"
      :status-description="statusDescription"
      :has-any-transfers="hasAnyTransfers"
      :dialog-transfers="dialogTransfers"
      :current-device-name="currentDeviceName"
      :remote-device-name="remoteDeviceName"
      :format-transfer-meta="formatTransferMeta"
      :format-direction="formatDirection"
      @close="closeDialog"
      @trigger-file="triggerFileSelect"
      @download="downloadTransfer"
    />
  </div>

  <input ref="fileInput" type="file" class="sr-only" multiple @change="handleFileChange" />
</template>
