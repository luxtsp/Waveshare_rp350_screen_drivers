#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <stdbool.h>

#include "bsp_display.h"

typedef enum {
    VIDEO_STATE_IDLE,
    VIDEO_STATE_ACTION,
    VIDEO_STATE_ERROR
} video_state_t;

/**
 * Initializes the video store, frame buffers, scheduler, and display area.
 *
 * The display driver must already be initialized before this call.
 */
bool video_player_init(
    bsp_display_interface_t *display,
    bsp_display_info_t *display_info
);

/** Non-blocking periodic scheduler; call it repeatedly from the main loop. */
void video_player_task(void);

/**
 * Called by the existing DMA completion callback after a display flush ends.
 *
 * This function performs only minimal ISR-safe work.
 */
void video_player_notify_dma_complete(void);

#endif /* VIDEO_PLAYER_H */
