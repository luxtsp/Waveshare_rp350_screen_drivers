#include <string.h>
#include <stdio.h>

#include "video_library.h"
#include "hw_drivers/sd_card.h"

static bool is_action_video_name(const char *name, uint8_t *index_out) {
    unsigned int index;

    if (strncmp(name, "action_video_", 13) != 0) {
        return false;
    }

    if (sscanf(name + 13, "%u", &index) != 1) {
        return false;
    }

    if (index == 0 || index > 255) {
        return false;
    }

    *index_out = (uint8_t)index;
    return true;
}

static bool add_frame(video_clip_t *clip, const char *path, uint32_t size) {
    if (clip->frame_count >= VIDEO_MAX_FRAMES) {
        return false;
    }

    video_frame_t *frame = &clip->frames[clip->frame_count];

    strncpy(frame->path, path, sizeof(frame->path) - 1);
    frame->path[sizeof(frame->path) - 1] = '\0';
    frame->size_bytes = size;

    clip->frame_count++;
    return true;
}

static bool scan_rawrgb_folder(video_clip_t *clip) {
    DIR dir;
    FILINFO info;

    if (f_opendir(&dir, clip->raw_dir) != FR_OK) {
        return false;
    }

    while (true) {
        FRESULT result = f_readdir(&dir, &info);

        if (result != FR_OK || info.fname[0] == '\0') {
            break;
        }

        if (info.fattrib & AM_DIR) {
            continue;
        }

        // Accept only .raw files; change this if your files use another extension.
        const char *dot = strrchr(info.fname, '.');

        if (!dot || strcasecmp(dot, ".raw") != 0) {
            continue;
        }

        char path[VIDEO_MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", clip->raw_dir, info.fname);

        if (!add_frame(clip, path, (uint32_t)info.fsize)) {
            break;
        }
    }

    f_closedir(&dir);

    // Sort frames lexicographically. This works well if filenames are
    // zero-padded, such as frame_0001.raw, frame_0002.raw, etc.
    for (uint16_t i = 0; i + 1 < clip->frame_count; i++) {
        for (uint16_t j = 0; j + 1 < clip->frame_count - i; j++) {
            if (strcmp(clip->frames[j].path,
                       clip->frames[j + 1].path) > 0) {
                video_frame_t temporary = clip->frames[j];
                clip->frames[j] = clip->frames[j + 1];
                clip->frames[j + 1] = temporary;
            }
        }
    }

    return clip->frame_count > 0;
}

static bool add_clip(video_library_t *lib,
                     const char *folder_name,
                     video_clip_type_t type,
                     uint8_t action_index) {
    if (lib->clip_count >= VIDEO_MAX_CLIPS) {
        return false;
    }

    video_clip_t *clip = &lib->clips[lib->clip_count];

    memset(clip, 0, sizeof(*clip));

    clip->type = type;
    clip->action_index = action_index;

    strncpy(clip->name, folder_name, sizeof(clip->name) - 1);
    snprintf(clip->raw_dir, sizeof(clip->raw_dir),
             "%s:/%s/rawrgb",
             sd_get_drive_prefix(sd_get_by_drive_prefix("0:")),
             folder_name);

    if (!scan_rawrgb_folder(clip)) {
        return false;
    }

    lib->clip_count++;
    return true;
}

bool video_library_mount_and_scan(video_library_t *lib, sd_card_t *sd_card_p) {
    memset(lib, 0, sizeof(*lib));

    if (!sd_card_p) {
        return false;
    }

    if (!sd_init_driver()) {
        return false;
    }

    if (!sd_card_detect(sd_card_p)) {
        return false;
    }

    // Mount the FatFS volume associated with this physical SD card.
    // The driver assigns the drive prefix, normally "0:" for the first card.
    const char *drive_prefix = sd_get_drive_prefix(sd_card_p);

    FRESULT mount_result = f_mount(&sd_card_p->state.fatfs,
                                   drive_prefix,
                                   1);

    if (mount_result != FR_OK) {
        return false;
    }

    lib->mounted = true;

    DIR root;
    FILINFO info;

    if (f_opendir(&root, drive_prefix) != FR_OK) {
        lib->mounted = false;
        return false;
    }

    while (true) {
        FRESULT result = f_readdir(&root, &info);

        if (result != FR_OK || info.fname[0] == '\0') {
            break;
        }

        if (!(info.fattrib & AM_DIR)) {
            continue;
        }

        uint8_t action_index;

        if (strcmp(info.fname, "idle_video") == 0) {
            add_clip(lib, info.fname, VIDEO_CLIP_IDLE, 0);
        } else if (is_action_video_name(info.fname, &action_index)) {
            add_clip(lib, info.fname, VIDEO_CLIP_ACTION, action_index);
        }
    }

    f_closedir(&root);

    return lib->clip_count > 0;
}

video_clip_t *video_library_find_clip(video_library_t *lib,
                                      video_clip_type_t type,
                                      uint8_t action_index) {
    for (uint8_t i = 0; i < lib->clip_count; i++) {
        video_clip_t *clip = &lib->clips[i];

        if (clip->type == type && clip->action_index == action_index) {
            return clip;
        }
    }

    return NULL;
}

bool video_library_read_frame(video_library_t *lib,
                              video_clip_t *clip,
                              uint16_t frame_index,
                              uint8_t *destination,
                              size_t destination_size) {
    if (!lib || !lib->mounted || !clip || !destination) {
        return false;
    }

    if (frame_index >= clip->frame_count) {
        return false;
    }

    video_frame_t *frame = &clip->frames[frame_index];

    if (frame->size_bytes > destination_size) {
        return false;
    }

    FIL file;

    if (f_open(&file, frame->path, FA_READ) != FR_OK) {
        return false;
    }

    UINT bytes_read = 0;
    FRESULT result = f_read(&file, destination, frame->size_bytes, &bytes_read);

    f_close(&file);

    return result == FR_OK && bytes_read == frame->size_bytes;
}
