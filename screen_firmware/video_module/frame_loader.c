#include "frame_loader.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"

bool frame_loader_init(void) {
    /* No dynamic allocation or persistent file handles are required.
     * The expected frame geometry is validated on every load. */
    return VIDEO_FRAMEBUFFER_BYTES > 0u;
}

static void swap_rgb565_bytes(uint16_t *pixels, uint32_t pixel_count) {
    for (uint32_t i = 0u; i < pixel_count; i++) {
        const uint16_t value = pixels[i];
        pixels[i] = (uint16_t)((value >> 8) | (value << 8));
    }
}

bool frame_loader_load(const char *path, video_frame_buffer_t *buffer) {
    if (path == NULL || buffer == NULL || buffer->pixels == NULL) {
        return false;
    }

    if (buffer->size_bytes != VIDEO_FRAMEBUFFER_BYTES) {
        printf("Frame buffer has invalid size: expected %lu, got %lu\n",
               (unsigned long)VIDEO_FRAMEBUFFER_BYTES,
               (unsigned long)buffer->size_bytes);
        return false;
    }

    FIL file;
    FRESULT open_result = f_open(&file, path, FA_READ);

    if (open_result != FR_OK) {
        printf("Cannot open frame: %s (FatFs result %d)\n", path, (int)open_result);
        return false;
    }

    const FSIZE_t actual_size = f_size(&file);

    if (actual_size != (FSIZE_t)VIDEO_FRAMEBUFFER_BYTES) {
        printf("Frame size mismatch: expected %lu, got %lu (%s)\n",
               (unsigned long)VIDEO_FRAMEBUFFER_BYTES,
               (unsigned long)actual_size,
               path);

        (void)f_close(&file);
        return false;
    }

    uint8_t *byte_buffer = (uint8_t *)buffer->pixels;
    uint32_t bytes_remaining = VIDEO_FRAMEBUFFER_BYTES;
    uint32_t byte_offset = 0u;

    while (bytes_remaining > 0u) {
        const uint32_t requested = (bytes_remaining < VIDEO_FRAME_READ_CHUNK_BYTES)
            ? bytes_remaining
            : VIDEO_FRAME_READ_CHUNK_BYTES;

        UINT bytes_read = 0u;
        FRESULT read_result = f_read(
            &file,
            &byte_buffer[byte_offset],
            requested,
            &bytes_read
        );

        if (read_result != FR_OK || bytes_read != requested) {
            printf("SD read failed for %s (result %d, requested %lu, read %u)\n",
                   path,
                   (int)read_result,
                   (unsigned long)requested,
                   (unsigned int)bytes_read);

            (void)f_close(&file);
            return false;
        }

        byte_offset += bytes_read;
        bytes_remaining -= bytes_read;
    }

    if (f_close(&file) != FR_OK) {
        printf("Failed to close frame file: %s\n", path);
        return false;
    }

#if VIDEO_SWAP_RGB565_BYTES
    swap_rgb565_bytes(buffer->pixels, VIDEO_FRAMEBUFFER_PIXELS);
#endif

    return true;
}
