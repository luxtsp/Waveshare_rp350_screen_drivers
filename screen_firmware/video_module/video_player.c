#include "video_player.h"

#include <stdio.h>
#include <string.h>

#include "frame_loader.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "sd_video_store.h"
#include "video_config.h"

#if VIDEO_USE_DOUBLE_BUFFER
#define VIDEO_PLAYER_BUFFER_COUNT 2u
#else
#define VIDEO_PLAYER_BUFFER_COUNT 1u
#endif

typedef enum {
    CLIP_TYPE_IDLE,
    CLIP_TYPE_ACTION
} clip_type_t;

typedef struct {
    bsp_display_interface_t *display;
    bsp_display_info_t *display_info;

    bsp_display_area_t area;

    video_store_t store;

    video_frame_buffer_t buffers[VIDEO_PLAYER_BUFFER_COUNT];
    uint16_t framebuffer_storage[VIDEO_PLAYER_BUFFER_COUNT][VIDEO_FRAMEBUFFER_PIXELS];

    const video_clip_info_t *current_clip;
    clip_type_t current_clip_type;

    char frame_names[VIDEO_MAX_FRAMES_PER_CLIP][VIDEO_MAX_FRAME_NAME_LEN];
    uint32_t current_frame_count;
    uint32_t current_frame_index;

    uint32_t display_buffer_index;
    uint32_t load_buffer_index;

    volatile bool dma_busy;
    volatile bool frame_just_completed;

    uint64_t next_frame_due_us;
    uint64_t next_action_due_us;
    bool action_timer_started;

    int32_t last_action_index;
    uint32_t rng_state;

    video_state_t state;
} video_player_t;

/* The DMA ISR has no context parameter, so one player instance is required. */
static video_player_t *s_player = NULL;

static uint32_t next_random(video_player_t *player) {
    player->rng_state =
        (player->rng_state * 1664525u) + 1013904223u;

    return player->rng_state;
}

static uint32_t random_range_inclusive(
    video_player_t *player,
    uint32_t minimum,
    uint32_t maximum
) {
    if (maximum < minimum) {
        const uint32_t temporary = minimum;
        minimum = maximum;
        maximum = temporary;
    }

    const uint32_t range = (maximum - minimum) + 1u;

    if (range == 0u) {
        return minimum;
    }

    return minimum + (next_random(player) % range);
}

static void schedule_next_action(video_player_t *player, uint64_t now_us) {
    if (player->store.action_count == 0u) {
        player->action_timer_started = false;
        return;
    }

    const uint32_t delay_ms = random_range_inclusive(
        player,
        ACTION_MIN_DELAY_MS,
        ACTION_MAX_DELAY_MS
    );

    player->next_action_due_us = now_us + ((uint64_t)delay_ms * 1000u);
    player->action_timer_started = true;

    printf("Next action in %lu ms\n", (unsigned long)delay_ms);
}

static void make_full_screen_area(bsp_display_area_t *area) {
    area->x = 0;
    area->y = 0;
    area->width = VIDEO_WIDTH;
    area->height = VIDEO_HEIGHT;
}

static void show_error_screen(video_player_t *player, uint16_t color) {
    for (uint32_t i = 0u; i < VIDEO_FRAMEBUFFER_PIXELS; i++) {
        player->buffers[0].pixels[i] = color;
    }

    if (player->display != NULL && player->display->flush != NULL) {
        player->display->flush(&player->area, player->buffers[0].pixels);
    }

    player->state = VIDEO_STATE_ERROR;
}

static bool load_and_show_frame(
    video_player_t *player,
    const video_clip_info_t *clip,
    uint32_t frame_index
) {
    char frame_path[VIDEO_MAX_PATH_LEN];
    video_frame_buffer_t *buffer = &player->buffers[player->load_buffer_index];

    if (frame_index >= player->current_frame_count) {
        return false;
    }

    if (!video_store_get_frame_path_by_name(
            &player->store,
            clip,
            player->frame_names[frame_index],
            frame_path,
            sizeof(frame_path)
        )) {
        printf("Frame path too long: %s/%s\n", clip->name, player->frame_names[frame_index]);
        return false;
    }

    if (!frame_loader_load(frame_path, buffer)) {
        return false;
    }

    if (player->display == NULL || player->display->flush_dma == NULL) {
        printf("Display DMA interface is unavailable\n");
        return false;
    }

    player->current_frame_index = frame_index;
    player->display_buffer_index = player->load_buffer_index;
    player->frame_just_completed = false;
    player->dma_busy = true;

    const bool flush_started = player->display->flush_dma(
        &player->area,
        buffer->pixels
    );

    if (!flush_started) {
        player->dma_busy = false;
        printf("Display DMA flush failed for %s\n", frame_path);
        return false;
    }

#if VIDEO_USE_DOUBLE_BUFFER
    player->load_buffer_index ^= 1u;
#endif

    return true;
}

static bool prepare_clip(
    video_player_t *player,
    const video_clip_info_t *clip,
    clip_type_t clip_type
) {
    if (clip == NULL || clip->frame_count == 0u) {
        return false;
    }

    if (!video_store_list_frames(
            &player->store,
            clip,
            player->frame_names,
            VIDEO_MAX_FRAMES_PER_CLIP,
            &player->current_frame_count
        )) {
        printf("Cannot enumerate frames for clip: %s\n", clip->name);
        return false;
    }

    player->current_clip = clip;
    player->current_clip_type = clip_type;
    player->current_frame_index = 0u;

    return true;
}

static bool start_idle_clip(video_player_t *player) {
    if (!prepare_clip(player, &player->store.idle, CLIP_TYPE_IDLE)) {
        return false;
    }

    return load_and_show_frame(player, &player->store.idle, 0u);
}

static int32_t choose_action_index(video_player_t *player) {
    const uint32_t action_count = player->store.action_count;

    if (action_count == 0u) {
        return -1;
    }

    if (action_count == 1u) {
        return 0;
    }

    uint32_t selected;

    do {
        selected = next_random(player) % action_count;
    } while ((int32_t)selected == player->last_action_index);

    return (int32_t)selected;
}

static bool start_action_clip(video_player_t *player, uint64_t now_us) {
    const int32_t action_index = choose_action_index(player);

    if (action_index < 0) {
        return false;
    }

    const video_clip_info_t *action_clip =
        &player->store.actions[(uint32_t)action_index];

    if (!prepare_clip(player, action_clip, CLIP_TYPE_ACTION)) {
        return false;
    }

    player->last_action_index = action_index;
    player->state = VIDEO_STATE_ACTION;

    printf("Playing %s\n", action_clip->name);

    if (!load_and_show_frame(player, action_clip, 0u)) {
        printf("Failed to start action clip: %s\n", action_clip->name);
        return false;
    }

    return true;
}

static void handle_frame_completion(video_player_t *player, uint64_t now_us) {
    const bool is_final_action_frame =
        (player->state == VIDEO_STATE_ACTION) &&
        (player->current_frame_index + 1u >= player->current_frame_count);

    if (is_final_action_frame) {
        printf("Action complete: %s\n", player->current_clip->name);

        player->state = VIDEO_STATE_IDLE;
        schedule_next_action(player, now_us);

        if (!start_idle_clip(player)) {
            printf("Failed to return to idle_vid\n");
            show_error_screen(player, 0xF81F);
        }

        return;
    }

    player->next_frame_due_us =
        now_us + ((uint64_t)VIDEO_DEFAULT_FRAME_MS * 1000u);
}

static void advance_idle_or_start_action(
    video_player_t *player,
    uint64_t now_us
) {
    if (player->state == VIDEO_STATE_IDLE &&
        player->action_timer_started &&
        now_us >= player->next_action_due_us) {

        if (!start_action_clip(player, now_us)) {
            /* A failed action must not freeze idle playback. */
            schedule_next_action(player, now_us);

            if (!load_and_show_frame(
                    player,
                    player->current_clip,
                    player->current_frame_index
                )) {
                show_error_screen(player, 0xF81F);
            }
        }

        return;
    }

    uint32_t next_index = player->current_frame_index + 1u;

    if (player->state == VIDEO_STATE_IDLE &&
        next_index >= player->current_frame_count) {
        next_index = 0u;
    }

    if (next_index >= player->current_frame_count) {
        /* An action clip should already have returned to idle; treat this as
         * a recovery path rather than allowing an out-of-range access. */
        if (!start_idle_clip(player)) {
            show_error_screen(player, 0xF81F);
        }

        return;
    }

    if (!load_and_show_frame(
            player,
            player->current_clip,
            next_index
        )) {
        printf("Frame load failed: %s index %lu\n",
               player->current_clip->name,
               (unsigned long)next_index);

        if (player->state == VIDEO_STATE_ACTION) {
            /* Return to idle instead of freezing on a damaged action frame. */
            player->state = VIDEO_STATE_IDLE;
            schedule_next_action(player, now_us);

            if (!start_idle_clip(player)) {
                show_error_screen(player, 0xF81F);
            }

            return;
        }

        /* Skip a damaged idle frame when possible. */
        if (player->current_frame_count > 1u) {
            next_index = (next_index + 1u) % player->current_frame_count;

            if (!load_and_show_frame(
                    player,
                    player->current_clip,
                    next_index
                )) {
                show_error_screen(player, 0xF81F);
            }
        } else {
            show_error_screen(player, 0xF81F);
        }
    }
}

bool video_player_init(
    bsp_display_interface_t *display,
    bsp_display_info_t *display_info
) {
    if (display == NULL || display_info == NULL) {
        printf("video_player_init: display interface is missing\n");
        return false;
    }

    if (s_player != NULL) {
        printf("video_player_init: player already initialized\n");
        return true;
    }

    static video_player_t player;
    memset(&player, 0, sizeof(player));

    player.display = display;
    player.display_info = display_info;
    player.rng_state = (uint32_t)(time_us_64() ^ 0x5DEECE66Du);
    player.last_action_index = -1;

    make_full_screen_area(&player.area);

    for (uint32_t i = 0u; i < VIDEO_PLAYER_BUFFER_COUNT; i++) {
        player.buffers[i].pixels = player.framebuffer_storage[i];
        player.buffers[i].size_bytes = VIDEO_FRAMEBUFFER_BYTES;
    }

    if (!frame_loader_init()) {
        printf("Frame loader initialization failed\n");
        return false;
    }

    if (!video_store_init(&player.store)) {
        show_error_screen(&player, 0xF800);
        printf("SD video store initialization failed\n");
        return false;
    }

    s_player = &player;

    if (!start_idle_clip(&player)) {
        show_error_screen(&player, 0xF800);
        printf("idle_vid could not be started\n");
        return false;
    }

    printf("Video player initialized; state=IDLE\n");

    return true;
}

void video_player_task(void) {
    video_player_t *player = s_player;

    if (player == NULL || player->state == VIDEO_STATE_ERROR) {
        return;
    }

    const uint64_t now_us = time_us_64();

    if (player->dma_busy) {
        return;
    }

    if (player->frame_just_completed) {
        player->frame_just_completed = false;
        handle_frame_completion(player, now_us);

        if (player->state == VIDEO_STATE_ERROR || player->dma_busy) {
            return;
        }
    }

    if (now_us >= player->next_frame_due_us) {
        advance_idle_or_start_action(player, now_us);
    }
}

void video_player_notify_dma_complete(void) {
    video_player_t *player = s_player;

    if (player == NULL || !player->dma_busy) {
        return;
    }

    player->dma_busy = false;
    player->frame_just_completed = true;
}
