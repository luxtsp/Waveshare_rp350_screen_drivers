#include "sd_video_store.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"

static bool is_action_clip_name(const char *name) {
    const size_t prefix_length = sizeof(VIDEO_ACTION_CLIP_PREFIX) - 1u;
    return strncmp(name, VIDEO_ACTION_CLIP_PREFIX, prefix_length) == 0;
}

static bool is_frame_name(const char *name) {
    const size_t prefix_length = sizeof(VIDEO_FRAME_NAME_PREFIX) - 1u;
    return strncmp(name, VIDEO_FRAME_NAME_PREFIX, prefix_length) == 0;
}

static bool is_regular_file(const FILINFO *info) {
    return (info->fattrib & AM_DIR) == 0u;
}

static bool build_clip_path(
    const video_store_t *store,
    const char *clip_name,
    char *out_path,
    size_t out_path_size
) {
    const int written = snprintf(
        out_path,
        out_path_size,
        "%s/%s",
        store->mount_point,
        clip_name
    );

    return (written >= 0) && ((size_t)written < out_path_size);
}

static uint32_t count_clip_frames(
    const video_store_t *store,
    const video_clip_info_t *clip
) {
    char directory_path[VIDEO_MAX_PATH_LEN];
    DIR directory;
    FILINFO info;
    uint32_t count = 0u;

    if (!build_clip_path(store, clip->name, directory_path, sizeof(directory_path))) {
        return 0u;
    }

    if (f_opendir(&directory, directory_path) != FR_OK) {
        printf("Cannot open clip directory: %s\n", directory_path);
        return 0u;
    }

    while (f_readdir(&directory, &info) == FR_OK && info.fname[0] != '\0') {
        if (is_regular_file(&info) && is_frame_name(info.fname)) {
            count++;
        }
    }

    (void)f_closedir(&directory);

    return count;
}

static void copy_clip_name(video_clip_info_t *destination, const char *source) {
    const size_t copy_length = strlen(source);

    if (copy_length >= VIDEO_MAX_CLIP_NAME_LEN) {
        /* Truncate safely; such a clip is still usable if its prefix is valid. */
        copy_length = VIDEO_MAX_CLIP_NAME_LEN - 1u;
    }

    memcpy(destination->name, source, copy_length);
    destination->name[copy_length] = '\0';
}

static bool discover_idle_clip(video_store_t *store) {
    memset(&store->idle, 0, sizeof(store->idle));
    copy_clip_name(&store->idle, VIDEO_IDLE_CLIP_NAME);

    store->idle.frame_count = count_clip_frames(store, &store->idle);

    if (store->idle.frame_count == 0u) {
        printf("idle_vid not found or contains no usable frames\n");
        return false;
    }

    printf("Found idle_vid with %lu frames\n",
           (unsigned long)store->idle.frame_count);

    return true;
}

static void sort_action_clips(video_store_t *store) {
    for (uint32_t i = 1u; i < store->action_count; i++) {
        video_clip_info_t key = store->actions[i];
        uint32_t j = i;

        while (j > 0u && strcmp(store->actions[j - 1u].name, key.name) > 0) {
            store->actions[j] = store->actions[j - 1u];
            j--;
        }

        store->actions[j] = key;
    }
}

static bool discover_action_clips(video_store_t *store) {
    DIR root_directory;
    FILINFO info;
    bool result = true;

    store->action_count = 0u;

    if (f_opendir(&root_directory, store->mount_point) != FR_OK) {
        printf("Cannot open SD-card root directory: %s\n", store->mount_point);
        return false;
    }

    while (f_readdir(&root_directory, &info) == FR_OK && info.fname[0] != '\0') {
        if (!is_action_clip_name(info.fname) || (info.fattrib & AM_DIR) == 0u) {
            continue;
        }

        if (store->action_count >= VIDEO_MAX_CLIPS) {
            printf("Warning: action clip limit reached (%u); ignoring %s\n",
                   (unsigned int)VIDEO_MAX_CLIPS,
                   info.fname);
            break;
        }

        video_clip_info_t *clip = &store->actions[store->action_count];
        memset(clip, 0, sizeof(*clip));
        copy_clip_name(clip, info.fname);

        clip->frame_count = count_clip_frames(store, clip);

        if (clip->frame_count == 0u) {
            printf("Skipping empty or unreadable action clip: %s\n", clip->name);
            continue;
        }

        store->action_count++;
    }

    if (f_closedir(&root_directory) != FR_OK) {
        result = false;
    }

    sort_action_clips(store);

    printf("Found %lu action clips\n",
           (unsigned long)store->action_count);

    return result;
}

bool video_store_init(video_store_t *store) {
    if (store == NULL) {
        return false;
    }

    memset(store, 0, sizeof(*store));

    const size_t mount_length = sizeof(VIDEO_SD_MOUNT_POINT) - 1u;
    if (mount_length >= sizeof(store->mount_point)) {
        printf("VIDEO_SD_MOUNT_POINT is too long\n");
        return false;
    }

    memcpy(store->mount_point, VIDEO_SD_MOUNT_POINT, mount_length + 1u);

    for (uint32_t attempt = 1u; attempt <= VIDEO_SD_MOUNT_RETRIES; attempt++) {
        FRESULT mount_result = f_mount(
            &store->fatfs,
            store->mount_point,
            1
        );

        if (mount_result == FR_OK) {
            store->mounted = true;
            printf("SD mount succeeded: %s\n", store->mount_point);

            if (!discover_idle_clip(store)) {
                return false;
            }

            if (!discover_action_clips(store)) {
                return false;
            }

            return true;
        }

        printf("SD mount failed (attempt %lu/%lu): %d\n",
               (unsigned long)attempt,
               (unsigned long)VIDEO_SD_MOUNT_RETRIES,
               (int)mount_result);

        if (attempt < VIDEO_SD_MOUNT_RETRIES) {
            sleep_ms(VIDEO_SD_RETRY_DELAY_MS);
        }
    }

    return false;
}

bool video_store_get_frame_path(
    const video_store_t *store,
    const video_clip_info_t *clip,
    uint32_t frame_index,
    char *out_path,
    size_t out_path_size
) {
    if (store == NULL || clip == NULL || out_path == NULL) {
        return false;
    }

    const int written = snprintf(
        out_path,
        out_path_size,
        "%s/%s/%s%lu",
        store->mount_point,
        clip->name,
        VIDEO_FRAME_NAME_PREFIX,
        (unsigned long)frame_index
    );

    return (written >= 0) && ((size_t)written < out_path_size);
}

bool video_store_get_frame_path_by_name(
    const video_store_t *store,
    const video_clip_info_t *clip,
    const char *frame_name,
    char *out_path,
    size_t out_path_size
) {
    if (store == NULL || clip == NULL || frame_name == NULL || out_path == NULL) {
        return false;
    }

    const int written = snprintf(
        out_path,
        out_path_size,
        "%s/%s/%s",
        store->mount_point,
        clip->name,
        frame_name
    );

    return (written >= 0) && ((size_t)written < out_path_size);
}

bool video_store_list_frames(
    const video_store_t *store,
    const video_clip_info_t *clip,
    char out_names[][VIDEO_MAX_FRAME_NAME_LEN],
    uint32_t max_names,
    uint32_t *out_count
) {
    if (store == NULL || clip == NULL || out_names == NULL || out_count == NULL) {
        return false;
    }

    char directory_path[VIDEO_MAX_PATH_LEN];
    DIR directory;
    FILINFO info;
    uint32_t count = 0u;

    *out_count = 0u;

    if (!build_clip_path(store, clip->name, directory_path, sizeof(directory_path))) {
        return false;
    }

    if (f_opendir(&directory, directory_path) != FR_OK) {
        printf("Cannot open clip directory: %s\n", directory_path);
        return false;
    }

    while (f_readdir(&directory, &info) == FR_OK && info.fname[0] != '\0') {
        if (!is_regular_file(&info) || !is_frame_name(info.fname)) {
            continue;
        }

        if (count >= max_names) {
            printf("Too many frames in %s; limit is %lu\n",
                   clip->name,
                   (unsigned long)max_names);
            (void)f_closedir(&directory);
            return false;
        }

        if (strlen(info.fname) >= VIDEO_MAX_FRAME_NAME_LEN) {
            printf("Frame name too long in %s: %s\n", clip->name, info.fname);
            (void)f_closedir(&directory);
            return false;
        }

        strcpy(out_names[count], info.fname);
        count++;
    }

    if (f_closedir(&directory) != FR_OK) {
        return false;
    }

    /* Lexicographic sort: img_0, img_1, img_10, img_11, img_2, ... */
    for (uint32_t i = 1u; i < count; i++) {
        char key[VIDEO_MAX_FRAME_NAME_LEN];
        strcpy(key, out_names[i]);

        uint32_t j = i;
        while (j > 0u && strcmp(out_names[j - 1u], key) > 0) {
            strcpy(out_names[j], out_names[j - 1u]);
            j--;
        }

        strcpy(out_names[j], key);
    }

    *out_count = count;

    return count > 0u;
}
