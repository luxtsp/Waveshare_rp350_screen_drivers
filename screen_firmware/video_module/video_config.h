#ifndef VIDEO_CONFIG_H
#define VIDEO_CONFIG_H

#include <stdint.h>

/* Display geometry. The ST77916 driver is configured for 360 x 360 RGB565. */
#define VIDEO_WIDTH 360
#define VIDEO_HEIGHT 360

#define VIDEO_FRAMEBUFFER_PIXELS (VIDEO_WIDTH * VIDEO_HEIGHT)
#define VIDEO_FRAMEBUFFER_BYTES (VIDEO_FRAMEBUFFER_PIXELS * 2u)

/* Playback configuration. */
#define VIDEO_DEFAULT_FRAME_MS 100u

#define ACTION_MIN_DELAY_MS 5000u
#define ACTION_MAX_DELAY_MS 20000u

/* Filesystem configuration. Change the drive prefix if your SD stack uses
 * another FatFs logical drive, for example "1:" or a custom prefix. */
#define VIDEO_SD_MOUNT_POINT "0:"

#define VIDEO_IDLE_CLIP_NAME "idle_vid"
#define VIDEO_ACTION_CLIP_PREFIX "action_vid_"
#define VIDEO_FRAME_NAME_PREFIX "img_"

/* Safety limits. */
#define VIDEO_MAX_PATH_LEN 256u
#define VIDEO_MAX_CLIP_NAME_LEN 64u
#define VIDEO_MAX_CLIPS 32u
#define VIDEO_MAX_FRAME_NAME_LEN 32u
#define VIDEO_MAX_FRAMES_PER_CLIP 512u

/* SD-card mount retries during startup only. */
#define VIDEO_SD_MOUNT_RETRIES 3u
#define VIDEO_SD_RETRY_DELAY_MS 500u

/* Set to 1 only if your QSPI display path requires byte-swapped RGB565. */
#ifndef VIDEO_SWAP_RGB565_BYTES
#define VIDEO_SWAP_RGB565_BYTES 0
#endif

/* Set to 1 if your board has enough free RAM for two 259,200-byte buffers.
 * Double buffering overlaps SD-card loading with DMA display transmission. */
#ifndef VIDEO_USE_DOUBLE_BUFFER
#define VIDEO_USE_DOUBLE_BUFFER 0
#endif

/* SD reads are performed in chunks to avoid requiring one huge stack buffer. */
#define VIDEO_FRAME_READ_CHUNK_BYTES 4096u

#endif /* VIDEO_CONFIG_H */
