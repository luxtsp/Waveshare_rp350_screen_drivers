#ifndef SD_VIDEO_STORE_H
#define SD_VIDEO_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"
#include "video_config.h"

typedef struct {
    char name[VIDEO_MAX_CLIP_NAME_LEN];
    uint32_t frame_count;
} video_clip_info_t;

typedef struct {
    FATFS fatfs;
    char mount_point[8];

    video_clip_info_t idle;

    video_clip_info_t actions[VIDEO_MAX_CLIPS];
    uint32_t action_count;

    bool mounted;
} video_store_t;

/**
 * Mounts the SD card and discovers idle/action clips.
 *
 * A clip is accepted only if it is a directory containing at least one
 * regular file whose name begins with VIDEO_FRAME_NAME_PREFIX.
 */
bool video_store_init(video_store_t *store);

/**
 * Builds a path for the canonical frame name img_<frame_index>.
 *
 * The player normally uses video_store_list_frames() instead, because that
 * function preserves the required lexicographic frame ordering.
 */
bool video_store_get_frame_path(
    const video_store_t *store,
    const video_clip_info_t *clip,
    uint32_t frame_index,
    char *out_path,
    size_t out_path_size
);

/**
 * Builds a path for an explicitly selected, already sorted frame filename.
 */
bool video_store_get_frame_path_by_name(
    const video_store_t *store,
    const video_clip_info_t *clip,
    const char *frame_name,
    char *out_path,
    size_t out_path_size
);

/**
 * Reads and lexicographically sorts the frame filenames in a clip.
 *
 * out_names must point to storage for at least max_names entries, each
 * VIDEO_MAX_FRAME_NAME_LEN bytes long.
 */
bool video_store_list_frames(
    const video_store_t *store,
    const video_clip_info_t *clip,
    char out_names[][VIDEO_MAX_FRAME_NAME_LEN],
    uint32_t max_names,
    uint32_t *out_count
);

#endif /* SD_VIDEO_STORE_H */
