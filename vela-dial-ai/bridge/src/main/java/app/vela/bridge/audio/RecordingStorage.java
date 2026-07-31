package app.vela.bridge.audio;

import app.vela.bridge.api.BridgeApiException;
import app.vela.bridge.config.BridgeProperties;
import jakarta.annotation.PostConstruct;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Instant;
import java.util.HexFormat;
import java.util.UUID;
import java.util.zip.CRC32;

@Component
public class RecordingStorage {

    public record StoredRecording(
            String fileName,
            Path absolutePath,
            long sizeBytes,
            String sha256,
            String crc32) {
    }

    private final Path directory;
    private final long maxBytes;

    public RecordingStorage(BridgeProperties properties) {
        this.directory = properties.getRecordings().getDirectory().toAbsolutePath().normalize();
        this.maxBytes = properties.getRecordings().getMaxBytes();
    }

    @PostConstruct
    void initialize() {
        try {
            Files.createDirectories(directory);
        } catch (IOException exception) {
            throw new IllegalStateException("Unable to create recording directory " + directory, exception);
        }
    }

    public StoredRecording saveWav(
            InputStream input,
            long contentLength,
            String declaredCrc32) {
        if (contentLength > maxBytes) {
            throw new BridgeApiException(
                    HttpStatus.PAYLOAD_TOO_LARGE,
                    "recording_too_large",
                    "Recording exceeds " + maxBytes + " bytes");
        }

        String normalizedDeclaredCrc = normalizeDeclaredCrc(declaredCrc32);
        String fileName = Instant.now().toEpochMilli() + "-" + UUID.randomUUID() + ".wav";
        Path temporary;
        try {
            temporary = Files.createTempFile(directory, "upload-", ".part");
        } catch (IOException exception) {
            throw storageFailure(exception);
        }

        MessageDigest digest = sha256();
        CRC32 crc32 = new CRC32();
        byte[] header = new byte[12];
        int headerCount = 0;
        long total = 0;

        try (OutputStream output = Files.newOutputStream(temporary)) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read == 0) {
                    continue;
                }
                total += read;
                if (total > maxBytes) {
                    throw new BridgeApiException(
                            HttpStatus.PAYLOAD_TOO_LARGE,
                            "recording_too_large",
                            "Recording exceeds " + maxBytes + " bytes");
                }
                int headerBytes = Math.min(read, header.length - headerCount);
                if (headerBytes > 0) {
                    System.arraycopy(buffer, 0, header, headerCount, headerBytes);
                    headerCount += headerBytes;
                }
                digest.update(buffer, 0, read);
                crc32.update(buffer, 0, read);
                output.write(buffer, 0, read);
            }

            if (!isWave(header, headerCount)) {
                throw new BridgeApiException(
                        HttpStatus.UNPROCESSABLE_ENTITY,
                        "invalid_wav",
                        "Body is not a RIFF/WAVE file");
            }

            String actualCrc = String.format("%08x", crc32.getValue());
            if (normalizedDeclaredCrc != null && !normalizedDeclaredCrc.equals(actualCrc)) {
                throw new BridgeApiException(
                        HttpStatus.UNPROCESSABLE_ENTITY,
                        "recording_crc_mismatch",
                        "Recording CRC32 does not match X-Vela-Wav-Crc32");
            }

            output.flush();
            validatePcmWave(temporary, total);
            Path target = directory.resolve(fileName);
            try {
                Files.move(temporary, target, StandardCopyOption.ATOMIC_MOVE);
            } catch (java.nio.file.AtomicMoveNotSupportedException exception) {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING);
            }
            return new StoredRecording(
                    fileName,
                    target,
                    total,
                    HexFormat.of().formatHex(digest.digest()),
                    actualCrc);
        } catch (BridgeApiException exception) {
            deleteQuietly(temporary);
            throw exception;
        } catch (IOException exception) {
            deleteQuietly(temporary);
            throw storageFailure(exception);
        }
    }

    public Path resolve(String fileName) {
        Path resolved = directory.resolve(fileName).normalize();
        if (!resolved.startsWith(directory) || !Files.isRegularFile(resolved)) {
            throw new BridgeApiException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "recording_missing",
                    "Persisted recording file is missing");
        }
        return resolved;
    }

    private static boolean isWave(byte[] header, int length) {
        return length >= 12
                && header[0] == 'R'
                && header[1] == 'I'
                && header[2] == 'F'
                && header[3] == 'F'
                && header[8] == 'W'
                && header[9] == 'A'
                && header[10] == 'V'
                && header[11] == 'E';
    }

    private static String normalizeDeclaredCrc(String value) {
        if (value == null || value.isBlank()) {
            return null;
        }
        String normalized = value.trim().toLowerCase();
        if (normalized.startsWith("0x")) {
            normalized = normalized.substring(2);
        }
        try {
            return String.format("%08x", Long.parseUnsignedLong(normalized, 16));
        } catch (NumberFormatException exception) {
            throw new BridgeApiException(
                    HttpStatus.UNPROCESSABLE_ENTITY,
                    "invalid_wav_crc32",
                    "X-Vela-Wav-Crc32 must be an unsigned hexadecimal CRC32");
        }
    }

    private static void validatePcmWave(Path path, long totalSize) throws IOException {
        if (totalSize < 44) {
            throw invalidWave("WAV file is shorter than the canonical PCM header");
        }
        try (FileChannel channel = FileChannel.open(path)) {
            ByteBuffer riff = read(channel, 0, 12);
            long riffPayloadSize = Integer.toUnsignedLong(riff.order(ByteOrder.LITTLE_ENDIAN).getInt(4));
            if (riffPayloadSize + 8 != totalSize) {
                throw invalidWave("RIFF committed length does not match the HTTP body");
            }

            boolean validFormat = false;
            boolean foundData = false;
            long offset = 12;
            while (offset + 8 <= totalSize) {
                ByteBuffer chunkHeader = read(channel, offset, 8).order(ByteOrder.LITTLE_ENDIAN);
                byte[] idBytes = new byte[4];
                chunkHeader.get(idBytes);
                String chunkId = new String(idBytes, java.nio.charset.StandardCharsets.US_ASCII);
                long chunkSize = Integer.toUnsignedLong(chunkHeader.getInt());
                long payloadOffset = offset + 8;
                long paddedEnd = payloadOffset + chunkSize + (chunkSize & 1);
                if (payloadOffset + chunkSize > totalSize || paddedEnd > totalSize) {
                    throw invalidWave("WAV chunk is truncated");
                }

                if ("fmt ".equals(chunkId)) {
                    if (chunkSize < 16) {
                        throw invalidWave("WAV fmt chunk is too short");
                    }
                    ByteBuffer format = read(channel, payloadOffset, 16).order(ByteOrder.LITTLE_ENDIAN);
                    int audioFormat = Short.toUnsignedInt(format.getShort());
                    int channels = Short.toUnsignedInt(format.getShort());
                    long sampleRate = Integer.toUnsignedLong(format.getInt());
                    long byteRate = Integer.toUnsignedLong(format.getInt());
                    int blockAlign = Short.toUnsignedInt(format.getShort());
                    int bitsPerSample = Short.toUnsignedInt(format.getShort());
                    validFormat = audioFormat == 1
                            && channels == 1
                            && sampleRate == 24_000
                            && byteRate == 48_000
                            && blockAlign == 2
                            && bitsPerSample == 16;
                    if (!validFormat) {
                        throw invalidWave(
                                "WAV must be PCM mono, 24 kHz, signed 16-bit, blockAlign 2, byteRate 48000");
                    }
                } else if ("data".equals(chunkId)) {
                    foundData = true;
                    if (payloadOffset + chunkSize != totalSize) {
                        throw invalidWave("WAV data length does not match the committed body length");
                    }
                }
                offset = paddedEnd;
            }
            if (!validFormat || !foundData) {
                throw invalidWave("WAV must contain valid fmt and data chunks");
            }
        }
    }

    private static ByteBuffer read(FileChannel channel, long offset, int length) throws IOException {
        ByteBuffer buffer = ByteBuffer.allocate(length);
        channel.position(offset);
        while (buffer.hasRemaining()) {
            if (channel.read(buffer) < 0) {
                throw invalidWave("WAV file is truncated");
            }
        }
        buffer.flip();
        return buffer;
    }

    private static BridgeApiException invalidWave(String message) {
        return new BridgeApiException(HttpStatus.UNPROCESSABLE_ENTITY, "invalid_wav", message);
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        }
    }

    private static BridgeApiException storageFailure(IOException exception) {
        return new BridgeApiException(
                HttpStatus.INTERNAL_SERVER_ERROR,
                "recording_write_failed",
                "Unable to persist recording",
                exception);
    }

    private static void deleteQuietly(Path path) {
        try {
            Files.deleteIfExists(path);
        } catch (IOException ignored) {
            // The next startup or operator cleanup can remove a stranded .part file.
        }
    }
}
