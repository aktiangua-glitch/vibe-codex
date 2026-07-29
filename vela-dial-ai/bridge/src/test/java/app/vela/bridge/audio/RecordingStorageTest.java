package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.zip.CRC32;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

class RecordingStorageTest {

    @TempDir
    Path temporaryDirectory;

    @Test
    void acceptsOnlyCommittedProductPcmFormatAndChecksCrc() {
        RecordingStorage storage = storage();
        byte[] wav = wave(24_000, new byte[480]);
        CRC32 crc = new CRC32();
        crc.update(wav);

        RecordingStorage.StoredRecording saved = storage.saveWav(
                new ByteArrayInputStream(wav),
                wav.length,
                String.format("%08x", crc.getValue()));

        assertThat(saved.sizeBytes()).isEqualTo(wav.length);
        assertThat(saved.absolutePath()).exists();
        assertThat(saved.crc32()).hasSize(8);
    }

    @Test
    void rejectsWrongSampleRateAndTrailingGarbage() {
        RecordingStorage storage = storage();
        byte[] wrongRate = wave(16_000, new byte[320]);
        assertThatThrownBy(() -> storage.saveWav(
                new ByteArrayInputStream(wrongRate), wrongRate.length, null))
                .isInstanceOf(BridgeApiException.class)
                .hasMessageContaining("24 kHz");

        byte[] valid = wave(24_000, new byte[480]);
        byte[] trailing = java.util.Arrays.copyOf(valid, valid.length + 1);
        assertThatThrownBy(() -> storage.saveWav(
                new ByteArrayInputStream(trailing), trailing.length, null))
                .isInstanceOf(BridgeApiException.class)
                .hasMessageContaining("RIFF committed length");
    }

    @Test
    void invalidCrcHeaderIsAClientError() {
        RecordingStorage storage = storage();
        byte[] wav = wave(24_000, new byte[480]);
        assertThatThrownBy(() -> storage.saveWav(
                new ByteArrayInputStream(wav), wav.length, "not-hex"))
                .isInstanceOf(BridgeApiException.class)
                .hasMessageContaining("CRC32");
    }

    private RecordingStorage storage() {
        BridgeProperties properties = new BridgeProperties();
        properties.getRecordings().setDirectory(temporaryDirectory);
        RecordingStorage storage = new RecordingStorage(properties);
        storage.initialize();
        return storage;
    }

    private static byte[] wave(int sampleRate, byte[] pcm) {
        ByteBuffer buffer = ByteBuffer.allocate(44 + pcm.length).order(ByteOrder.LITTLE_ENDIAN);
        buffer.put(new byte[]{'R', 'I', 'F', 'F'});
        buffer.putInt(36 + pcm.length);
        buffer.put(new byte[]{'W', 'A', 'V', 'E'});
        buffer.put(new byte[]{'f', 'm', 't', ' '});
        buffer.putInt(16);
        buffer.putShort((short) 1);
        buffer.putShort((short) 1);
        buffer.putInt(sampleRate);
        buffer.putInt(sampleRate * 2);
        buffer.putShort((short) 2);
        buffer.putShort((short) 16);
        buffer.put(new byte[]{'d', 'a', 't', 'a'});
        buffer.putInt(pcm.length);
        buffer.put(pcm);
        return buffer.array();
    }
}
