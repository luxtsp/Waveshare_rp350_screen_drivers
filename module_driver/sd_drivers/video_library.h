#ifndef VIDEO_LIBRARY_H
#define VIDEO_LIBRARY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hw_drivers/sd_card.h"

#define VIDEO_MAX_CLIPS       16
#define VIDEO_MAX_FRAMES      512
#define VIDEO_MAX_PATH_LEN    128
#define VIDEO_MAX_NAME_LEN    64

typedef enum {
    VIDEO_CLIP_IDLE = 0,
    VIDEO_CLIP_ACTION = 1
} video_clip_type_t;

typedef struct {
    char path[VIDEO_MAX_PATH_LEN];  // Full path, e.g. "0:/idle_video/rawrgb/frame_0001.raw"
    uint32_t size_bytes;
} video_frame_t;

typedef struct {
    video_clip_type_t type;
    uint8_t action_index;           // 0 for idle_video, 1..N for action_video_x
    char name[VIDEO_MAX_NAME_LEN];  // "idle_video" or "action_video_1"
    char raw_dir[VIDEO_MAX_PATH_LEN];
    video_frame_t frames[VIDEO_MAX_FRAMES];
    uint16_t frame_count;
} video_clip_t;

typedef struct {
    video_clip_t clips[VIDEO_MAX_CLIPS];
    uint8_t clip_count;
    bool mounted;
} video_library_t;

bool video_library_mount_and_scan(video_library_t *lib, sd_card_t *sd_card_p);
video_clip_t *video_library_find_clip(video_library_t *lib,
                                      video_clip_type_t type,
                                      uint8_t action_index);
bool video_library_read_frame(video_library_t *lib,
                              video_clip_t *clip,
                              uint16_t frame_index,
                              uint8_t *destination,
                              size_t destination_size);

#endif
