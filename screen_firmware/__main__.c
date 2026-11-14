#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "bsp_st77916.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "video_player.h"

static bsp_display_interface_t *display_interface = NULL;
static bsp_display_info_t display_info;

static bool init_display(void) {
    memset(&display_info, 0, sizeof(display_info));

    /*
     * These are the fields used by your bsp_st77916.c implementation.
     * If bsp_display.h defines additional mandatory fields, copy their
     * initial values from the Waveshare display example.
     */
    display_info.x_offset = 0;
    display_info.y_offset = 0;
    display_info.rotation = 0;
    display_info.brightness = 80;

    /* This is the key integration point: the ST77916 driver invokes it
     * from bsp_st77916_flush_dma_done_cb() after each DMA flush. */
    display_info.dma_flush_done_cb = video_player_notify_dma_complete;

    if (!bsp_display_new_st77916(&display_interface, &display_info)) {
        printf("Failed to create ST77916 display interface\n");
        return false;
    }

    if (display_interface == NULL || display_interface->init == NULL) {
        printf("ST77916 display interface is invalid\n");
        return false;
    }

    display_interface->init();

    printf("ST77916 display initialized\n");
    return true;
}

static bool init_sd_card(void) {
    /* The FatFs SD library configures the actual SD pins in its
     * sd_driver/hw_config.c file. This call prepares that driver. */
    sd_init_driver();

    printf("SD-card driver initialized\n");
    return true;
}

int main(void) {
    stdio_init_all();

    /* Give USB CDC time to enumerate when debugging over USB serial. */
    sleep_ms(2000);

    printf("\nRP2350-Touch-LCD-1.85C video player\n");

    if (!init_display()) {
        printf("Fatal: display initialization failed\n");

        while (true) {
            tight_loop_contents();
        }
    }

    if (!init_sd_card()) {
        printf("Fatal: SD-card initialization failed\n");

        while (true) {
            tight_loop_contents();
        }
    }

    if (!video_player_init(display_interface, &display_info)) {
        printf("Fatal: video player initialization failed\n");

        while (true) {
            tight_loop_contents();
        }
    }

    printf("Entering video playback loop\n");

    while (true) {
        video_player_task();
    }

    return 0;
}
