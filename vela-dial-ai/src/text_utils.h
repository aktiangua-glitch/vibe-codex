#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline void vela_copy_utf8(
    char *destination,
    size_t capacity,
    const char *source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    size_t read_index = 0;
    size_t write_index = 0;
    while (source[read_index] != '\0' &&
           write_index + 1U < capacity) {
        const uint8_t lead =
            static_cast<uint8_t>(source[read_index]);
        size_t sequence_bytes = 1;
        if (lead < 0x80U) {
            sequence_bytes = 1;
        } else if (lead >= 0xC2U && lead <= 0xDFU) {
            sequence_bytes = 2;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            sequence_bytes = 3;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            sequence_bytes = 4;
        } else {
            destination[write_index++] = '?';
            ++read_index;
            continue;
        }

        bool complete = true;
        for (size_t offset = 1;
             offset < sequence_bytes;
             ++offset) {
            const uint8_t next = static_cast<uint8_t>(
                source[read_index + offset]);
            if (next == 0U ||
                (next & 0xC0U) != 0x80U) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            destination[write_index++] = '?';
            ++read_index;
            continue;
        }
        if (write_index + sequence_bytes >= capacity) {
            break;
        }

        memcpy(
            destination + write_index,
            source + read_index,
            sequence_bytes);
        write_index += sequence_bytes;
        read_index += sequence_bytes;
    }
    destination[write_index] = '\0';
}
